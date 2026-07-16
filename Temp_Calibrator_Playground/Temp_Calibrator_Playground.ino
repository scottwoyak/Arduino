//
// Calibrates and displays temperature readings for 8 multiplexed sensors and uploads calibration telemetry.
//
// Uses the ESP32-S3 Playground board with rotary encoder and TFT display instead of buttons.
// Each sensor is sampled on its own 100ms cadence via a SensorData, which maintains a short
// (10 sample) rolling average and a long (2 minute) timed average. The display shows a table
// of per-sensor short average, long average, and correction (delta from sensor 0's long
// average), plus three scatter plots - one per table column - showing recent history for
// every detected sensor. On startup, saved calibration deltas are loaded from Preferences,
// displayed, and printed to Serial. Correction factors are periodically saved to Preferences,
// reported to Serial (as a timestamped table) once per minute, and telemetry is written to
// InfluxDB with reconnect and buffering.
//
// Each SensorData's long (timed) average is also fed into a shared TempCalibrator, which automatically
// records every sensor's correction factor each time the baseline (sensor 0) reaches a new
// whole-number temperature; each time a new entry is recorded, the full calibration point
// table is immediately saved to Preferences (as a binary blob) for later analysis. Press the
// encoder A button to print the recorded calibration-vs-temperature table to Serial; use
// loadCalibrationPoints() to read the saved table back. If calibration points were saved on a
// previous run, they are displayed on startup as a scatter plot of correction vs. baseline
// temperature (one line per sensor) until the encoder A button is pressed.
//
// Press the rotary encoder button to clear the display.
//
#include <Wire.h>

#include "ESP32_S3_Playground.h"
#include "Timer.h"
#include "TempSensor.h"
#include "Multiplexer.h"
#include "SerialX.h"
#include "Influx.h"
#include "ScatterPlot.h"
#include "TimedScatterPlot.h"
#include "RollingAverage.h"
#include "TimedAverage.h"
#include "TempCalibrator.h"

#include "WiFiSettings.h"

ESP32_S3_Playground arduino;
constexpr uint8_t NUM_SENSORS = 8;

TempSensor sensors[] =
{
   TempSensor(),
   TempSensor(),
   TempSensor(),
   TempSensor(),
   TempSensor(),
   TempSensor(),
   TempSensor(),
   TempSensor(),
};

// Sensor and telemetry timing
constexpr uint16_t SAMPLE_INTERVAL_MS = 100;  // how often each SensorData takes a raw reading, and how often the outer loop drives sensor updates
constexpr uint16_t TIMED_AVERAGE_DURATION_S = 5 * 60;  // 5 minute averaging window
constexpr uint16_t INFLUX_INTERVAL_S = 10;
constexpr uint16_t PREFS_INTERVAL_S = 60;
constexpr uint16_t SERIAL_REPORT_INTERVAL_S = 60;  // 1 minute
constexpr uint16_t WIFI_RESET_DELAY_S = 10;
constexpr uint8_t BATCH_SIZE_PER_SENSOR = 3;  // InfluxDB write batch size, per detected sensor
constexpr uint16_t INFLUX_INIT_DELAY_MS = 1000;  // pause after InfluxDB init to show "Init" screen

constexpr const char* CALIBRATOR_PREFS_NAMESPACE = "Calibrator";
constexpr const char* TEMP_KEY_PREFIX = "Temp ";
constexpr const char* ID_KEY_PREFIX = "ID ";
constexpr const char* CAL_POINT_COUNT_KEY = "CalCount";
constexpr const char* CAL_POINTS_KEY = "CalPoints";

///
/// <summary>
/// Samples a single multiplexed TempSensor on its own 100ms cadence and maintains a
/// rolling buffer of the last 10 readings, exposing their average as the sensor's current
/// value. Call ready() as often as possible from loop(), and call read() whenever it
/// returns true; ready() internally gates actual sampling (including the
/// Multiplexer::select() call) to once every SAMPLE_INTERVAL_MS.
/// </summary>
///
class SensorData
{
private:
   static constexpr uint8_t BUFFER_SIZE = 10;

   TempSensor* _sensor;
   uint8_t _muxIndex;
   Timer _sampleTimer;

   RollingAverage _shortAverage;
   TimedAverage _longAverage;

public:
   ///
   /// <summary>
   /// Constructs a SensorData for the given sensor at the given multiplexer channel.
   /// </summary>
   /// <param name="sensor">Sensor to sample.</param>
   /// <param name="muxIndex">Multiplexer channel this sensor is wired to.</param>
   ///
   SensorData(TempSensor* sensor, uint8_t muxIndex)
      : _sensor(sensor), _muxIndex(muxIndex), _sampleTimer(SAMPLE_INTERVAL_MS),
        _shortAverage(BUFFER_SIZE), _longAverage(TIMED_AVERAGE_DURATION_S * 1000UL)
   {
   }

   ///
   /// <summary>
   /// Reports whether the sensor exists and SAMPLE_INTERVAL_MS has elapsed since the last
   /// reading, meaning it's time to call read().
   /// </summary>
   /// <returns>True when a new reading should be taken via read().</returns>
   ///
   bool ready()
   {
      return _sensor->exists() && _sampleTimer.ready();
   }

   ///
   /// <summary>
   /// Takes a new reading and stores it in the rolling buffer and the timed average.
   /// Should only be called when ready() returns true.
   /// </summary>
   ///
   void read()
   {
      Multiplexer::select(_muxIndex);
      float temp = _sensor->readTemperatureF();

      _shortAverage.set(temp);
      _longAverage.set(temp);
   }

   ///
   /// <summary>
   /// Gets the average of the readings currently in the rolling buffer (10 samples).
   /// </summary>
   /// <returns>Average temperature in Fahrenheit, or NaN if no readings have been taken yet.</returns>
   ///
   float getShortAvgValue() const
   {
      return _shortAverage.average();
   }

   ///
   /// <summary>
   /// Gets the timed average over the configured TIMED_AVERAGE_DURATION_S window (5 minutes).
   /// </summary>
   /// <returns>Average temperature in Fahrenheit, or NaN if no readings have been taken yet.</returns>
   ///
   float getLongAvgValue()
   {
      return _longAverage.average();
   }

   ///
   /// <summary>
   /// Reports whether the long (timed) average has accumulated a full
   /// TIMED_AVERAGE_DURATION_S window of data.
   /// </summary>
   /// <returns>True once the long average window is fully populated.</returns>
   ///
   bool isLongAvgFull()
   {
      return _longAverage.isFull();
   }
};

// Sensor data trackers, one per sensor, each maintaining a rolling buffer of recent readings
SensorData* sensorData[NUM_SENSORS] = { nullptr };

// Records each sensor's correction factor every time sensor 0's temperature reaches a new
// whole-number value, building a table of correction-vs-temperature for later interpolation.
TempCalibrator calibrator(NUM_SENSORS, TempCalibrator::BaselineMode::FIRST_SENSOR);

// Tracks calibrator.getCalibrationPointCount() as of the last loop() iteration, so a new
// calibration point can be detected and immediately saved to Preferences.
uint8_t lastCalibrationPointCount = 0;

///
/// <summary>
/// Saves all recorded calibration points (baseline + per-sensor corrections) to Preferences
/// as a single binary blob, so they can be loaded and analyzed later.
/// </summary>
///
void saveCalibrationPoints()
{
   uint8_t count = calibrator.getCalibrationPointCount();
   if (count == 0)
   {
      Serial.println("No calibration points recorded yet, nothing saved.");
      return;
   }

   TempCalibrator::CalibrationPoint* points = new TempCalibrator::CalibrationPoint[count];
   for (uint8_t i = 0; i < count; i++)
   {
      points[i] = calibrator.getCalibrationPoint(i);
   }

   arduino.preferences.begin(CALIBRATOR_PREFS_NAMESPACE, false);
   arduino.preferences.putUChar(CAL_POINT_COUNT_KEY, count);
   arduino.preferences.putBytes(CAL_POINTS_KEY, points, count * sizeof(TempCalibrator::CalibrationPoint));
   arduino.preferences.end();

   delete[] points;

   Serial.print("Saved ");
   Serial.print(count);
   Serial.println(" calibration points to Preferences.");
}

///
/// <summary>
/// Loads calibration points previously saved with saveCalibrationPoints() from Preferences.
/// The caller owns the returned array and must delete[] it when done.
/// </summary>
/// <param name="count">Set to the number of points loaded (0 if none were saved).</param>
/// <returns>Newly allocated array of loaded calibration points, or nullptr if none were saved.</returns>
///
TempCalibrator::CalibrationPoint* loadCalibrationPoints(uint8_t& count)
{
   arduino.preferences.begin(CALIBRATOR_PREFS_NAMESPACE, true);
   count = arduino.preferences.getUChar(CAL_POINT_COUNT_KEY, 0);

   if (count == 0)
   {
      arduino.preferences.end();
      return nullptr;
   }

   TempCalibrator::CalibrationPoint* points = new TempCalibrator::CalibrationPoint[count];
   arduino.preferences.getBytes(CAL_POINTS_KEY, points, count * sizeof(TempCalibrator::CalibrationPoint));
   arduino.preferences.end();

   return points;
}

Format shortAvgFormat("###.##", 13, Format::Alignment::RIGHT);
Format longAvgFormat("###.###", 10, Format::Alignment::RIGHT);
Format correctionFormat("+#.###", 10, Format::Alignment::RIGHT);

// Column widths and colors for the sensor table (data colors match their column heading)
constexpr uint8_t COL_NUM_WIDTH = 4;
constexpr uint8_t COL_TYPE_WIDTH = 9;
constexpr Color COL_NUM_COLOR = Color::LABEL;
constexpr Color COL_TYPE_COLOR = Color::LABEL;
constexpr Color COL_SHORT_AVG_COLOR = Color::VALUE;
constexpr Color COL_LONG_AVG_COLOR = Color::VALUE2;
constexpr Color COL_CORRECTION_COLOR = Color::VALUE3;

// Time window for the sensor plot
constexpr unsigned long TIME_WINDOW_PLOT_MS = 60000;   // 1 minute

// Distinct colors for each sensor's line, cycled if there are more sensors than colors.
constexpr Color SENSOR_PLOT_COLORS[] =
{
   Color::WHITE,
   Color::YELLOW,
   Color::CYAN,
   Color::GREEN,
   Color::ORANGE,
   Color::MAGENTA,
   Color::RED,
   Color::BLUE,
};

// Scatter plot renderers showing every detected sensor as a line plot, one plot per
// table column: 10 Sample Avg, 5 Min Avg, and Correction (left to right).
TimedScatterPlot* shortAvgPlot = nullptr;
TimedScatterPlot* longAvgPlot = nullptr;
TimedScatterPlot* correctionPlot = nullptr;
TimedScatterPlotSeries* shortAvgSeries[NUM_SENSORS] = { nullptr };
TimedScatterPlotSeries* longAvgSeries[NUM_SENSORS] = { nullptr };
TimedScatterPlotSeries* correctionSeries[NUM_SENSORS] = { nullptr };

Timer sensorReadTrigger(SAMPLE_INTERVAL_MS);
TimerSecs influxTrigger(INFLUX_INTERVAL_S);
TimerSecs prefsTrigger(PREFS_INTERVAL_S);
TimerSecs serialReportTrigger(SERIAL_REPORT_INTERVAL_S);

InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);
Influx influx(WIFI_SSID, WIFI_PASSWORD, &client);

InfluxPoint* shortAvgPoints[NUM_SENSORS] = { nullptr };
InfluxPoint* longAvgPoints[NUM_SENSORS] = { nullptr };
InfluxPoint* correctionPoints[NUM_SENSORS] = { nullptr };

InfluxField* shortAvgFields[NUM_SENSORS] = { nullptr };
InfluxField* longAvgFields[NUM_SENSORS] = { nullptr };
InfluxField* correctionFields[NUM_SENSORS] = { nullptr };

// Tracks values collected since the last InfluxDB upload, for the upload log line
long sampleCount = 0;

///
/// <summary>
/// Formats a number as a two-digit, zero-padded string (e.g. 5 -> "05").
/// </summary>
/// <param name="value">Number to format, expected to be in the range 0-99.</param>
/// <returns>Zero-padded two-digit string.</returns>
///
String _twoDigits(int value)
{
   return (value < 10 ? "0" : "") + String(value);
}

///
/// <summary>
/// Prints the currently saved per-sensor correction factors and IDs to Serial, formatted
/// for easy pasting into libraries\Woyak\TempSensorCallibration.h.
/// </summary>
///
void printCalibrationCodeToSerial()
{
   // load saved correction factors and IDs
   arduino.preferences.begin(CALIBRATOR_PREFS_NAMESPACE, false);
   float tempCorrections[NUM_SENSORS];
   std::string ids[NUM_SENSORS];

   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      tempCorrections[i] = arduino.preferences.getFloat((String(TEMP_KEY_PREFIX) + i).c_str());
      ids[i] = arduino.preferences.getString((String(ID_KEY_PREFIX) + i).c_str()).c_str();
   }
   arduino.preferences.end();

   // load the full recorded calibration point history, used both to fit a correction
   // curve per sensor and to print the full table as a comment for future reference
   uint8_t pointCount = 0;
   TempCalibrator::CalibrationPoint* points = loadCalibrationPoints(pointCount);

   TempCalibrator::RegressionFit fits[NUM_SENSORS];
   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      fits[i] = TempCalibrator::getBestFit(points, pointCount, i);
   }

   // Separate this report from any prior Serial output with a blank line and a timestamped title
   time_t now = time(nullptr);
   struct tm timeinfo;
   localtime_r(&now, &timeinfo);
   String timestamp = String(timeinfo.tm_year + 1900) + "-" + _twoDigits(timeinfo.tm_mon + 1) + "-" + _twoDigits(timeinfo.tm_mday) +
      " " + _twoDigits(timeinfo.tm_hour) + ":" + _twoDigits(timeinfo.tm_min) + ":" + _twoDigits(timeinfo.tm_sec);

   Serial.println();
   Serial.print("Calibration Report - ");
   Serial.println(timestamp);

   // print saved factors for easy paste into TempSensorCallibration.h. Use the best-fit
   // curve (a + b*T + c*T^2) when enough calibration history exists for a sensor;
   // otherwise fall back to the flat correction factor recorded at the last baseline.
   Serial.println("Copy this data to libraries\\Woyak\\TempSensorCallibration.h");
   Serial.println(String("// Based on averaging data points over the last ") + TIMED_AVERAGE_DURATION_S + " seconds at a sample rate of " + (1000 / SAMPLE_INTERVAL_MS) + "/s");

   // Column widths sized to line up the id, tempA (3 decimals), tempB, and tempC (6
   // decimals) columns regardless of sign or digit count, matching the style of the
   // hand-formatted table in TempSensorCallibration.h.
   static const Format idColFormat(16, Format::Alignment::LEFT);
   static const Format tempAColFormat("+#.###", 7, Format::Alignment::RIGHT);
   static const Format tempBCColFormat("+#.######", 10, Format::Alignment::RIGHT);
   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      std::string idField = "\"" + ids[i] + "\",";
      Serial.print(idColFormat.toString(idField).c_str());

      if (fits[i].valid)
      {
         Serial.print(tempAColFormat.toString(fits[i].a).c_str());
         Serial.print(", ");
         Serial.print(tempBCColFormat.toString(fits[i].b).c_str());
         Serial.print(", ");
         Serial.print(tempBCColFormat.toString(fits[i].c).c_str());
      }
      else
      {
         Serial.print(tempAColFormat.toString(tempCorrections[i]).c_str());
         Serial.print(", ");
         Serial.print(tempBCColFormat.toString(0.0).c_str());
         Serial.print(", ");
         Serial.print(tempBCColFormat.toString(0.0).c_str());
      }
      Serial.println(",");
   }

   // Include the full calibration point history as comments, for future reference, so
   // the raw data behind the fitted curves above isn't lost.
   if (pointCount > 0)
   {
      Serial.println();
      Serial.println("// Full calibration history (baseline temp, correction per sensor):");

      static const Format baselineColFormat("###", Format::Alignment::RIGHT);
      static const Format sensorColFormat("+#.###", 8, Format::Alignment::RIGHT);

      Serial.println("// Base    S0       S1       S2       S3       S4       S5       S6       S7");

      for (uint8_t i = 0; i < pointCount; i++)
      {
         const TempCalibrator::CalibrationPoint& point = points[i];
         String row = "// " + String(baselineColFormat.toString(point.baseline).c_str());
         for (uint8_t s = 0; s < NUM_SENSORS; s++)
         {
            row += " " + String(sensorColFormat.toString(point.corrections[s]).c_str());
         }
         Serial.println(row);
      }
   }

   delete[] points;

   // Also display the recorded correction factors in a table for easy reading, one
   // column per detected sensor with the current baseline temperature in the first
   // column. Sensors that were never recorded to Preferences (no ID saved) are skipped
   // entirely rather than shown as an empty placeholder column. Skip the table entirely
   // if no saved correction run exists yet.
   uint8_t existingSensors[NUM_SENSORS];
   uint8_t existingSensorCount = 0;
   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      if (!ids[i].empty())
      {
         existingSensors[existingSensorCount++] = i;
      }
   }

   if (existingSensorCount == 0)
   {
      return;
   }

   String sensorLabels[NUM_SENSORS];
   SerialTable::Column columns[NUM_SENSORS + 1];
   columns[0] = { "Temp", 8 };

   for (uint8_t i = 0; i < existingSensorCount; i++)
   {
      sensorLabels[i] = "Sensor" + String(existingSensors[i] + 1);
      columns[i + 1] = { sensorLabels[i].c_str(), 9 };
   }

   // Format with a fixed decimal precision and a leading +/- sign, so every value has
   // the same length and the decimal points line up regardless of sign.
   static const Format tableCorrectionFormat("+#.###", Format::Alignment::RIGHT);
   String correctionText[NUM_SENSORS];
   for (uint8_t i = 0; i < existingSensorCount; i++)
   {
      correctionText[i] = tableCorrectionFormat.toString(tempCorrections[existingSensors[i]]).c_str();
   }

   SerialTable table("Recorded Temperature Corrections", columns, existingSensorCount + 1);
   table.printHeader();
   float baseline = (sensorData[0] != nullptr) ? sensorData[0]->getLongAvgValue() : NAN;

   table.printRow(SerialTable::fixed(baseline, 0),
      correctionText[0], correctionText[1], correctionText[2], correctionText[3],
      correctionText[4], correctionText[5], correctionText[6], correctionText[7]);
}

///
/// <summary>
/// Displays previously saved calibration points (if any) on startup before running begins.
/// </summary>
///
void displaySavedInfo()
{
   displaySavedCalibrationPlot();
}

///
/// <summary>
/// If calibration points were previously saved (via saveCalibrationPoints()), displays them
/// as a scatter plot of correction factor vs. baseline temperature: each sensor's raw
/// calibration points are drawn as data points, and the better of a linear or quadratic
/// curve fit (chosen automatically per sensor, based on R-squared) is drawn as a line.
/// Also prints the calibration info to Serial and where to copy it. Waits for the encoder
/// A button to be pressed before returning. Does nothing if no calibration points have
/// been saved.
/// </summary>
///
void displaySavedCalibrationPlot()
{
   uint8_t count;
   TempCalibrator::CalibrationPoint* points = loadCalibrationPoints(count);
   if (points == nullptr)
   {
      return;
   }

   printCalibrationCodeToSerial();

   // Number of points used to draw each sensor's fitted curve
   constexpr uint8_t FIT_CURVE_RESOLUTION = 20;

   arduino.setTextSize(3);
   arduino.setCursor(0, 0);
   arduino.println("Temperature Calibrator", Color::HEADING);

   arduino.setTextSize(2);
   arduino.println("Saved Calibration Points. Press Button A to continue", Color::LABEL);

   ScatterPlot plot(&arduino, 0, arduino.getCursorY(), arduino.width(), arduino.height() - arduino.getCursorY());
   plot.setXAxisFormat(Format("##F"));
   ScatterPlotSeries* rawSeries[NUM_SENSORS] = { nullptr };
   ScatterPlotSeries* fitSeries[NUM_SENSORS] = { nullptr };

   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      Color color = SENSOR_PLOT_COLORS[i % (sizeof(SENSOR_PLOT_COLORS) / sizeof(SENSOR_PLOT_COLORS[0]))];

      rawSeries[i] = plot.createSeries(count);
      rawSeries[i]->showPoints = true;
      rawSeries[i]->showLines = false;
      rawSeries[i]->color = color;

      fitSeries[i] = plot.createSeries(FIT_CURVE_RESOLUTION);
      fitSeries[i]->showPoints = false;
      fitSeries[i]->showLines = true;
      fitSeries[i]->color = color;
   }

   for (uint8_t p = 0; p < count; p++)
   {
      for (uint8_t i = 0; i < NUM_SENSORS; i++)
      {
         if (!isnan(points[p].corrections[i]))
         {
            rawSeries[i]->add(points[p].baseline, points[p].corrections[i]);
         }
      }
   }

   // Points are recorded in ascending baseline order, so the first and last entries
   // give the range to draw each sensor's fitted curve over.
   if (count > 0)
   {
      float baselineMin = points[0].baseline;
      float baselineMax = points[count - 1].baseline;
      float baselineSpan = baselineMax - baselineMin;

      for (uint8_t i = 0; i < NUM_SENSORS; i++)
      {
         TempCalibrator::RegressionFit fit = TempCalibrator::getBestFit(points, count, i);
         if (!fit.valid)
         {
            continue;
         }

         for (uint8_t s = 0; s < FIT_CURVE_RESOLUTION; s++)
         {
            float baseline = (baselineSpan > 0.0f)
               ? (baselineMin + baselineSpan * s / (FIT_CURVE_RESOLUTION - 1))
               : baselineMin;
            fitSeries[i]->add(baseline, static_cast<float>(fit.evaluate(baseline)));
         }
      }
   }

   delete[] points;

   while (!arduino.encoderA.button.wasPressed())
   {
      plot.render();
      delay(1);
   }
}

///
/// <summary>
/// Displays the "Calibrating..." header banner at the top of the screen.
/// </summary>
///
void displayHeader()
{
   arduino.setTextSize(3);
   arduino.println("Temperature Calibrator", Color::HEADING);
}

///
/// <summary>
/// Displays the sensor table showing, for every sensor, its type, 10-sample average,
/// 2-minute timed average, and correction relative to sensor 0's timed average.
/// </summary>
///
void displaySensorList()
{
   arduino.setTextSize(2);

   static const Format numHeaderFormat(COL_NUM_WIDTH, Format::Alignment::LEFT);
   static const Format typeHeaderFormat(COL_TYPE_WIDTH, Format::Alignment::LEFT);
   static const Format numFormat("#", COL_NUM_WIDTH, Format::Alignment::LEFT);

   // Header formats match the width/alignment of the data formats below them so the
   // header text lines up with the columns it labels.
   static const Format shortAvgHeaderFormat(shortAvgFormat.length(), Format::Alignment::RIGHT);
   static const Format longAvgHeaderFormat(longAvgFormat.length(), Format::Alignment::RIGHT);
   static const Format correctionHeaderFormat(correctionFormat.length(), Format::Alignment::RIGHT);

   // Display table header, each column colored to match the color used for that
   // column's data below.
   arduino.print("", numHeaderFormat, COL_NUM_COLOR);
   arduino.print("Type", typeHeaderFormat, COL_TYPE_COLOR);
   arduino.print("10 Sample Avg", shortAvgHeaderFormat, COL_SHORT_AVG_COLOR);
   arduino.print(" ");
   arduino.print("5 Min Avg", longAvgHeaderFormat, COL_LONG_AVG_COLOR);
   arduino.print(" ");
   arduino.println("Correction", correctionHeaderFormat, COL_CORRECTION_COLOR);

   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      Multiplexer::select(i);
      TempSensor& sensor = sensors[i];
      bool sensorExists = sensor.exists();

      // Sensor number
      arduino.print((i + 1), numFormat, COL_NUM_COLOR);

      // Sensor type
      if (sensorExists)
      {
         arduino.print(sensor.type(), typeHeaderFormat, COL_TYPE_COLOR);
      }
      else
      {
         arduino.print("---", typeHeaderFormat, COL_TYPE_COLOR);
      }

      if (sensorExists)
      {
         float longAvg = sensorData[i]->getLongAvgValue();
         float baseline = sensorData[0]->getLongAvgValue();
         float correction = (!isnan(longAvg) && !isnan(baseline)) ? (baseline - longAvg) : NAN;
         float shortAvg = sensorData[i]->getShortAvgValue();

         // Display current temp
         if (!isnan(shortAvg))
         {
            arduino.print(shortAvg, shortAvgFormat, COL_SHORT_AVG_COLOR);
         }
         else
         {
            arduino.print("---", shortAvgFormat, COL_SHORT_AVG_COLOR);
         }
         arduino.print(" ");

         // Display averaged temp
         if (!isnan(longAvg))
         {
            arduino.print(longAvg, longAvgFormat, COL_LONG_AVG_COLOR);
         }
         else
         {
            arduino.print("---", longAvgFormat, COL_LONG_AVG_COLOR);
         }
         arduino.print(" ");

         // Display correction factor (only once the timed average is available, since
         // it can't yet be computed otherwise)
         if (!isnan(longAvg) && !isnan(correction))
         {
            arduino.println(correction, correctionFormat, COL_CORRECTION_COLOR);
         }
         else
         {
            arduino.println("---", correctionFormat, COL_CORRECTION_COLOR);
         }
      }
      else
      {
         arduino.print("---", shortAvgFormat, COL_SHORT_AVG_COLOR);
         arduino.print(" ");
         arduino.print("---", longAvgFormat, COL_LONG_AVG_COLOR);
         arduino.print(" ");
         arduino.println("---", correctionFormat, COL_CORRECTION_COLOR);
      }
   }
}

///
/// <summary>
/// Renders the three scatter plots showing the configured time window of history for every
/// detected sensor, one per table column: 10 Sample Avg, 5 Min Avg, and Correction (left to
/// right). All plots fill the remaining display space below the calibration table.
/// </summary>
///
void displayScatterPlots()
{
   if (shortAvgPlot) shortAvgPlot->render();
   if (longAvgPlot) longAvgPlot->render();
   if (correctionPlot) correctionPlot->render();
}

void setup()
{
   SerialX::begin();
   Wire.begin();

   arduino.begin();

   displaySavedInfo();

   arduino.setTextSize(3);

   arduino.preferences.begin(CALIBRATOR_PREFS_NAMESPACE, false);
   uint8_t detectedSensorCount = 0;
   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      Multiplexer::select(i);
      TempSensor& sensor = sensors[i];
      sensor.begin();

      bool sensorExists = sensor.exists();
      if (sensorExists)
      {
         detectedSensorCount++;
      }

      // clear out corrections
      sensor.setTempCorrectionF(0);

      shortAvgPoints[i] = new InfluxPoint("Air", { { "location", (String("Cal ") + (i + 1) + ": Short Average").c_str() } });
      shortAvgFields[i] = shortAvgPoints[i]->addValueField("temperature", 3);

      longAvgPoints[i] = new InfluxPoint("Air", { { "location", (String("Cal ") + (i + 1) + ": Long Average").c_str() } });
      longAvgFields[i] = longAvgPoints[i]->addValueField("temperature", 3);

      correctionPoints[i] = new InfluxPoint("Air", { { "location", (String("Cal ") + (i + 1) + ": Correction").c_str() } });
      correctionFields[i] = correctionPoints[i]->addValueField("temperature", 4);

      arduino.preferences.putString((String(ID_KEY_PREFIX) + i).c_str(), sensorExists ? sensor.id() : "");

      sensorData[i] = new SensorData(&sensor, i);
   }
   arduino.preferences.end();

   uint8_t batchSize = std::max(static_cast<uint8_t>(1), static_cast<uint8_t>(BATCH_SIZE_PER_SENSOR * detectedSensorCount));
   client.setWriteOptions(WriteOptions().batchSize(batchSize).bufferSize(2 * batchSize));

   Serial.print("Detected sensors: ");
   Serial.println(detectedSensorCount);

   arduino.echoToSerial = true;
   arduino.clearDisplay();
   arduino.setTextSize(3);
   arduino.println("Init", Color::HEADING);
   arduino.moveCursorY(10);

   arduino.setTextSize(2);
   if (!influx.begin(&arduino))
   {
      Util::reset(WIFI_RESET_DELAY_S);
   }

   delay(INFLUX_INIT_DELAY_MS);

   arduino.clearDisplay();
   arduino.echoToSerial = false;

   pinMode(BUILTIN_LED, OUTPUT);
   digitalWrite(BUILTIN_LED, LOW);

   // Initialize TimedScatterPlot to plot every detected sensor as a line
   // The plot starts below the calibration table and fills the remaining display space

   // Calculate topOffset: header (size 3) + table header (size 2) + 8 sensor rows (size 2)
   int16_t headerHeight = arduino.charH();  // "Calibrating..." header, still at size 3 here

   arduino.setTextSize(2);
   int16_t tableHeight = (1 + NUM_SENSORS) * arduino.charH();  // header row + 8 sensor rows at size 2
   arduino.setTextSize(3);  // restore, matching the size used elsewhere in setup()

   int16_t topOffset = headerHeight + tableHeight + 4;  // Add small padding

   uint16_t plotAreaWidth = arduino.width();
   uint16_t plotWidth = plotAreaWidth / 3;
   uint16_t plotHeight = static_cast<uint16_t>(arduino.height() - topOffset);

   Rect16 shortAvgPlotRect
   {
      0,
      static_cast<uint16_t>(topOffset),
      plotWidth,
      plotHeight
   };

   Rect16 longAvgPlotRect
   {
      plotWidth,
      static_cast<uint16_t>(topOffset),
      plotWidth,
      plotHeight
   };

   Rect16 correctionPlotRect
   {
      static_cast<uint16_t>(2 * plotWidth),
      static_cast<uint16_t>(topOffset),
      static_cast<uint16_t>(plotAreaWidth - 2 * plotWidth),
      plotHeight
   };

   shortAvgPlot = new (std::nothrow) TimedScatterPlot(&arduino, shortAvgPlotRect, TIME_WINDOW_PLOT_MS, 0.0f, "10 Sample Avg");
   longAvgPlot = new (std::nothrow) TimedScatterPlot(&arduino, longAvgPlotRect, TIME_WINDOW_PLOT_MS, 0.0f, "5 Min Avg");
   correctionPlot = new (std::nothrow) TimedScatterPlot(&arduino, correctionPlotRect, TIME_WINDOW_PLOT_MS, Format("+#.##", Format::Alignment::RIGHT), 0.0f, "Correction");

   if (!shortAvgPlot || !longAvgPlot || !correctionPlot)
   {
      Util::setHaltReason("OOM allocating scatter plots in Temp_Calibrator_Playground");
      Util::reset();
      return;
   }

   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      if (!sensors[i].exists())
      {
         continue;
      }

      Color color = SENSOR_PLOT_COLORS[i % (sizeof(SENSOR_PLOT_COLORS) / sizeof(SENSOR_PLOT_COLORS[0]))];

      TimedScatterPlotSeries* shortAvgSeriesForSensor = shortAvgPlot->createSeries();
      TimedScatterPlotSeries* longAvgSeriesForSensor = longAvgPlot->createSeries();
      TimedScatterPlotSeries* correctionSeriesForSensor = correctionPlot->createSeries();
      if (!shortAvgSeriesForSensor || !longAvgSeriesForSensor || !correctionSeriesForSensor)
      {
         Util::setHaltReason("OOM allocating scatter plot series in Temp_Calibrator_Playground");
         Util::reset();
         return;
      }

      shortAvgSeriesForSensor->showPoints = false;
      shortAvgSeriesForSensor->showLines = true;
      shortAvgSeriesForSensor->color = color;
      shortAvgSeries[i] = shortAvgSeriesForSensor;

      longAvgSeriesForSensor->showPoints = false;
      longAvgSeriesForSensor->showLines = true;
      longAvgSeriesForSensor->color = color;
      longAvgSeries[i] = longAvgSeriesForSensor;

      correctionSeriesForSensor->showPoints = false;
      correctionSeriesForSensor->showLines = true;
      correctionSeriesForSensor->color = color;
      correctionSeries[i] = correctionSeriesForSensor;
   }

   // Clear the saved calibration plot screen before switching to the calibrating display
   arduino.clearDisplay();
}

void loop()
{
   if (arduino.encoderA.button.wasPressed())
   {
      arduino.clearDisplay();
      calibrator.printCalibrationTable();
   }

   // ------------------------------------------- sample (each SensorData self-paces to 100ms)
   for (int i = 0; i < NUM_SENSORS; i++)
   {
      if (sensorData[i]->ready())
      {
         sensorData[i]->read();
         calibrator.set(i, sensorData[i]->getLongAvgValue());
      }
   }

   // Save the calibration point table to Preferences as soon as a new entry is added,
   // rather than waiting for the encoder A button to be pressed.
   uint8_t calibrationPointCount = calibrator.getCalibrationPointCount();
   if (calibrationPointCount != lastCalibrationPointCount)
   {
      saveCalibrationPoints();
      lastCalibrationPointCount = calibrationPointCount;
   }

   // ------------------------------------------- calibrate + display
   if (sensorReadTrigger.ready())
   {
      for (int i = 0; i < NUM_SENSORS; i++)
      {
         if (sensors[i].exists())
         {
            sampleCount++;
         }
      }

      // Baseline is the first sensor's 2-minute timed average
      float baseline = sensorData[0]->getLongAvgValue();

      for (int i = 0; i < NUM_SENSORS; i++)
      {
         if (!sensors[i].exists())
         {
            continue;
         }

         float longAvg = sensorData[i]->getLongAvgValue();
         float shortAvg = sensorData[i]->getShortAvgValue();

         // Add this sensor's short average value to the first plot
         if (shortAvgSeries[i] != nullptr && !isnan(shortAvg))
         {
            shortAvgSeries[i]->add(shortAvg);
         }

         // Add this sensor's long average value to the second plot
         if (longAvgSeries[i] != nullptr && !isnan(longAvg))
         {
            longAvgSeries[i]->add(longAvg);
         }

         // Add this sensor's current correction/delta to the third plot (only once the
         // long average and baseline are available)
         if (correctionSeries[i] != nullptr && !isnan(longAvg) && !isnan(baseline))
         {
            correctionSeries[i]->add(baseline - longAvg);
         }
      }
   }

   // Always display the data
   arduino.setCursor(0, 0);
   displayHeader();

   arduino.setCursor(0, arduino.charH());
   displaySensorList();

   // Display scatter plots for every detected sensor
   displayScatterPlots();

   // ------------------------------------------- save to flash
   if (prefsTrigger.ready())
   {
      float saveBaseline = sensorData[0]->getLongAvgValue();
      arduino.preferences.begin(CALIBRATOR_PREFS_NAMESPACE, false);
      for (uint8_t i = 0; i < NUM_SENSORS; i++)
      {
         float longAvg = sensorData[i]->getLongAvgValue();
         float correction = (!isnan(longAvg) && !isnan(saveBaseline)) ? (saveBaseline - longAvg) : 0.0f;
         arduino.preferences.putFloat((String(TEMP_KEY_PREFIX) + i).c_str(), correction);
      }
      arduino.preferences.end();
   }

   // ------------------------------------------- report to serial
   if (serialReportTrigger.ready())
   {
      printCalibrationCodeToSerial();
   }

   // ------------------------------------------- send to INFLUX
   if (influxTrigger.ready())
   {
      if (influx.ensureWiFiConnected())
      {
         digitalWrite(BUILTIN_LED, HIGH);

         Serial.print("Uploading to InfluxDB... ");
         Serial.print(sampleCount);
         Serial.println(" values collected");
         sampleCount = 0;

         bool writeFailed = false;
         bool baselineFull = sensorData[0]->isLongAvgFull();
         float baseline = sensorData[0]->getLongAvgValue();

         for (int i = 0; i < NUM_SENSORS; i++)
         {
            if (!sensors[i].exists())
            {
               continue;
            }

            // Short average uploads as soon as it is available; long average and
            // correction (which depends on it) only upload once their full timed
            // window has been collected.
            float tShortAvg = sensorData[i]->getShortAvgValue();
            bool longAvgFull = baselineFull && sensorData[i]->isLongAvgFull();
            float tLongAvg = sensorData[i]->getLongAvgValue();
            float tDelta = baseline - tLongAvg;

            shortAvgFields[i]->set(tShortAvg);
            longAvgFields[i]->set(tLongAvg);
            correctionFields[i]->set(tDelta);

            // InfluxPoint::post() clears/re-populates fields, skips NaN/Inf values,
            // and (with writeToSerial=true) prints the line protocol actually sent
            // and any error, so bad/unexpected uploads can be spotted directly. Points
            // aren't posted until their underlying value is expected to be ready (e.g.
            // long average/correction before the timed window fills), so a NaN value
            // once posting starts is a genuine error and is still reported.
            if ((!shortAvgPoints[i]->post(&client)) ||
                (longAvgFull && !longAvgPoints[i]->post(&client)) ||
                (longAvgFull && !correctionPoints[i]->post(&client)))
            {
               writeFailed = true;
               break;
            }
         }

         if (!writeFailed && !client.isBufferEmpty())
         {
            writeFailed = !client.flushBuffer();
         }

         if (writeFailed)
         {
            Serial.print("InfluxDB write failed. Status code: ");
            Serial.print(client.getLastStatusCode());
            Serial.print(", message: ");
            Serial.println(client.getLastErrorMessage());
         }

         digitalWrite(BUILTIN_LED, LOW);
      }
   }
}
