//
// Calibrates and displays temperature readings for 8 multiplexed sensors and uploads calibration telemetry.
//
// Uses the ESP32-S3 Playground board with rotary encoder and TFT display instead of buttons.
// Each sensor is sampled on its own 100ms cadence via a Sampler, which maintains a short
// (10 sample) rolling average and a long (2 minute) timed average. The display shows a table
// of per-sensor short average, long average, and correction (delta from sensor 0's long
// average), plus three scatter plots - one per table column - showing recent history for
// every detected sensor. On startup, saved calibration deltas are loaded from Preferences,
// displayed, and printed to Serial. Correction factors are periodically saved to Preferences,
// reported to Serial (as a timestamped table) once per minute, and telemetry is written to
// InfluxDB with reconnect and buffering.
//
// Each Sampler also feeds its long (timed) average into a shared TempCalibrator, which automatically
// records every sensor's correction factor each time the baseline (sensor 0) reaches a new
// whole-number temperature. Press the encoder A button to print the recorded calibration-vs-temperature
// table to Serial and save it to Preferences (as a binary blob) for later analysis; use
// loadCalibrationPoints() to read it back.
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
constexpr uint16_t SAMPLE_INTERVAL_MS = 100;  // how often each Sampler takes a raw reading
constexpr uint16_t SENSOR_UPDATE_INTERVAL_MS = SAMPLE_INTERVAL_MS;  // how often readings/plots/correction update
constexpr unsigned long TIMED_AVERAGE_DURATION_MS = 2 * 60 * 1000UL;  // 2 minute averaging window
constexpr auto INFLUX_INTERVAL_S = 10;
constexpr auto PREFS_INTERVAL_S = 60;
constexpr auto SERIAL_REPORT_INTERVAL_S = 60;  // 1 minute
constexpr auto SAVED_INFO_WAIT_S = 60;
constexpr auto WIFI_RESET_DELAY_S = 10;
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
/// value. Call update() as often as possible from loop(); it internally gates actual
/// sampling (including the Multiplexer::select() call) to once every SAMPLE_INTERVAL_MS.
/// </summary>
///
class Sampler
{
private:
   static constexpr uint8_t BUFFER_SIZE = 10;

   TempSensor* _sensor;
   uint8_t _muxIndex;
   Timer _sampleTimer;

   RollingAverage _shortAverage;
   TimedAverage _longAverage;

   TempCalibrator* _calibrator;
   uint8_t _calibratorIndex;

public:
   ///
   /// <summary>
   /// Constructs a Sampler for the given sensor at the given multiplexer channel.
   /// </summary>
   /// <param name="sensor">Sensor to sample.</param>
   /// <param name="muxIndex">Multiplexer channel this sensor is wired to.</param>
   /// <param name="calibrator">Shared TempCalibrator that records per-degree correction factors.</param>
   /// <param name="calibratorIndex">This sensor's index within the calibrator.</param>
   ///
   Sampler(TempSensor* sensor, uint8_t muxIndex, TempCalibrator* calibrator, uint8_t calibratorIndex)
      : _sensor(sensor), _muxIndex(muxIndex), _sampleTimer(SAMPLE_INTERVAL_MS),
        _shortAverage(BUFFER_SIZE), _longAverage(TIMED_AVERAGE_DURATION_MS),
        _calibrator(calibrator), _calibratorIndex(calibratorIndex)
   {
   }

   ///
   /// <summary>
   /// Takes a new reading if SAMPLE_INTERVAL_MS has elapsed since the last one, storing it
   /// in the rolling buffer, the timed average, and the shared TempCalibrator. Does nothing
   /// if the sensor does not exist.
   /// </summary>
   ///
   void update()
   {
      if (!_sensor->exists() || !_sampleTimer.ready())
      {
         return;
      }

      Multiplexer::select(_muxIndex);
      float temp = _sensor->readTemperatureF();

      _shortAverage.set(temp);
      _longAverage.set(temp);
      _calibrator->set(_calibratorIndex, _longAverage.average());
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
   /// Gets the timed average over the configured TIMED_AVERAGE_DURATION_MS window (2 minutes).
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
   /// TIMED_AVERAGE_DURATION_MS window of data.
   /// </summary>
   /// <returns>True once the long average window is fully populated.</returns>
   ///
   bool isLongAvgFull()
   {
      return _longAverage.isFull();
   }
};

// Samplers, one per sensor, each maintaining a rolling buffer of recent readings
Sampler* samplers[NUM_SENSORS] = { nullptr };

// Records each sensor's correction factor every time sensor 0's temperature reaches a new
// whole-number value, building a table of correction-vs-temperature for later interpolation.
TempCalibrator calibrator(NUM_SENSORS, TempCalibrator::BaselineMode::FIRST_SENSOR);

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
// table column: 10 Sample Avg, 2 Min Avg, and Correction (left to right).
TimedScatterPlot* shortAvgPlot = nullptr;
TimedScatterPlot* longAvgPlot = nullptr;
TimedScatterPlot* correctionPlot = nullptr;
TimedScatterPlotSeries* shortAvgSeries[NUM_SENSORS] = { nullptr };
TimedScatterPlotSeries* longAvgSeries[NUM_SENSORS] = { nullptr };
TimedScatterPlotSeries* correctionSeries[NUM_SENSORS] = { nullptr };

Timer sensorReadTrigger(SENSOR_UPDATE_INTERVAL_MS);
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
   // load saved correction factors
   arduino.preferences.begin(CALIBRATOR_PREFS_NAMESPACE, false);
   float tempCorrections[NUM_SENSORS];
   std::string ids[NUM_SENSORS];

   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      tempCorrections[i] = arduino.preferences.getFloat((String(TEMP_KEY_PREFIX) + i).c_str());
      ids[i] = arduino.preferences.getString((String(ID_KEY_PREFIX) + i).c_str()).c_str();
   }
   arduino.preferences.end();

   // Separate this report from any prior Serial output with a blank line and a timestamped title
   time_t now = time(nullptr);
   struct tm timeinfo;
   localtime_r(&now, &timeinfo);
   String timestamp = String(timeinfo.tm_year + 1900) + "-" + _twoDigits(timeinfo.tm_mon + 1) + "-" + _twoDigits(timeinfo.tm_mday) +
      " " + _twoDigits(timeinfo.tm_hour) + ":" + _twoDigits(timeinfo.tm_min) + ":" + _twoDigits(timeinfo.tm_sec);

   Serial.println();
   Serial.print("Calibration Report - ");
   Serial.println(timestamp);

   // print saved factors for easy paste into TempSensorCallibration.h
   Serial.println("Copy this data to libraries\\Woyak\\TempSensorCallibration.h");
   Serial.println(String("// Based on averaging data points over the last ") + (TIMED_AVERAGE_DURATION_MS / 1000) + " seconds at a sample rate of " + (1000 / SENSOR_UPDATE_INTERVAL_MS) + "/s");
   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      Serial.print("\"");
      Serial.print(ids[i].c_str());
      Serial.print("\", ");
      Serial.print(tempCorrections[i], 3);
      Serial.print(", 0.000,");
      Serial.println();
   }

   // Also display the recorded correction factors in a table for easy reading, one
   // column per sensor with the current baseline temperature in the first column.
   // Skip the table entirely if no saved correction run exists yet (no sensor IDs
   // have ever been recorded to Preferences).
   bool anySavedData = false;
   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      if (!ids[i].empty())
      {
         anySavedData = true;
         break;
      }
   }

   if (!anySavedData)
   {
      return;
   }

   String sensorLabels[NUM_SENSORS];
   SerialTable::Column columns[NUM_SENSORS + 1];
   columns[0] = { "Temp", 8 };

   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      sensorLabels[i] = "Sensor" + String(i + 1);
      columns[i + 1] = { sensorLabels[i].c_str(), 9 };
   }

   String correctionText[NUM_SENSORS];
   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      correctionText[i] = !ids[i].empty() ? String(tempCorrections[i], 3) : "-----";
   }

   SerialTable table("Recorded Temperature Corrections", columns, NUM_SENSORS + 1);
   table.printHeader();
   float baseline = (samplers[0] != nullptr) ? samplers[0]->getLongAvgValue() : NAN;
   table.printRow(SerialTable::fixed(baseline, 0),
      correctionText[0],
      correctionText[1],
      correctionText[2],
      correctionText[3],
      correctionText[4],
      correctionText[5],
      correctionText[6],
      correctionText[7]);
}

///
/// <summary>
/// Displays previously saved correction factors on screen and prints them to Serial,
/// waiting for the rotary encoder button or a timeout before returning.
/// </summary>
///
void displaySavedInfo()
{
   // load saved correction factors
   arduino.preferences.begin(CALIBRATOR_PREFS_NAMESPACE, false);
   float tempCorrections[NUM_SENSORS];

   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      tempCorrections[i] = arduino.preferences.getFloat((String(TEMP_KEY_PREFIX) + i).c_str());
   }
   arduino.preferences.end();

   printCalibrationCodeToSerial();

   // display saved temperature factors
   arduino.setCursor(0, 0);
   arduino.setTextSize(3);
   arduino.fillRect(0, 0, arduino.width(), arduino.charH(), Color::ORANGE);
   arduino.printlnC("Saved Correction Factors", Color::WHITE, Color::ORANGE);
   arduino.moveCursorY(arduino.charH() / 2);

   arduino.setTextSize(3);
   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      arduino.print((i + 1), Color::LABEL);
      arduino.moveCursorX(10);

      if (isnan(tempCorrections[i]))
      {
         arduino.println("-----", Color::GRAY);
      }
      else
      {
         arduino.println(tempCorrections[i], correctionFormat, Color::VALUE);
      }
   }

   TimerSecs waitTimer(SAVED_INFO_WAIT_S);
   while (!arduino.encoderA.button.wasPressed() && !waitTimer.ready())
   {
      delay(1);
   }

   // print again in case the user didn't have serial open the first time
   printCalibrationCodeToSerial();
}

///
/// <summary>
/// Displays the "Calibrating..." header banner at the top of the screen.
/// </summary>
///
void displayHeader()
{
   arduino.setTextSize(3);
   arduino.fillRect(0, 0, arduino.width(), arduino.charH(), Color::ORANGE);
   arduino.printlnC("Calibrating...", Color::WHITE, Color::ORANGE);
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
   arduino.print("2 Min Avg", longAvgHeaderFormat, COL_LONG_AVG_COLOR);
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
         float longAvg = samplers[i]->getLongAvgValue();
         float baseline = samplers[0]->getLongAvgValue();
         float correction = (!isnan(longAvg) && !isnan(baseline)) ? (baseline - longAvg) : NAN;
         float shortAvg = samplers[i]->getShortAvgValue();

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
/// detected sensor, one per table column: 10 Sample Avg, 2 Min Avg, and Correction (left to
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

      samplers[i] = new Sampler(&sensor, i, &calibrator, i);
   }
   arduino.preferences.end();

   uint8_t batchSize = std::max((uint8_t)1, (uint8_t)(BATCH_SIZE_PER_SENSOR * detectedSensorCount));
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
   longAvgPlot = new (std::nothrow) TimedScatterPlot(&arduino, longAvgPlotRect, TIME_WINDOW_PLOT_MS, 0.0f, "2 Min Avg");
   correctionPlot = new (std::nothrow) TimedScatterPlot(&arduino, correctionPlotRect, TIME_WINDOW_PLOT_MS, Format("+#.##", Format::Alignment::RIGHT), Format("##.#s", Format::Alignment::CENTER), 0.0f, "Correction");

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

   // Clear the "Saved Correction Factors" screen before switching to the calibrating display
   arduino.clearDisplay();
}

void loop()
{
   if (arduino.encoderA.button.wasPressed())
   {
      arduino.clearDisplay();
      calibrator.printCalibrationTable();
      saveCalibrationPoints();
   }

   // ------------------------------------------- sample (each Sampler self-paces to 100ms)
   for (int i = 0; i < NUM_SENSORS; i++)
   {
      samplers[i]->update();
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
      float baseline = samplers[0]->getLongAvgValue();

      for (int i = 0; i < NUM_SENSORS; i++)
      {
         if (!sensors[i].exists())
         {
            continue;
         }

         float longAvg = samplers[i]->getLongAvgValue();
         float shortAvg = samplers[i]->getShortAvgValue();

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
      float saveBaseline = samplers[0]->getLongAvgValue();
      arduino.preferences.begin(CALIBRATOR_PREFS_NAMESPACE, false);
      for (uint8_t i = 0; i < NUM_SENSORS; i++)
      {
         float longAvg = samplers[i]->getLongAvgValue();
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
         bool baselineFull = samplers[0]->isLongAvgFull();
         float baseline = samplers[0]->getLongAvgValue();

         for (int i = 0; i < NUM_SENSORS; i++)
         {
            if (!sensors[i].exists())
            {
               continue;
            }

            // Short average uploads as soon as it is available; long average and
            // correction (which depends on it) only upload once their full timed
            // window has been collected.
            float tShortAvg = samplers[i]->getShortAvgValue();
            bool longAvgFull = baselineFull && samplers[i]->isLongAvgFull();
            float tLongAvg = samplers[i]->getLongAvgValue();
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
            if ((!shortAvgPoints[i]->post(&client, true)) ||
                (longAvgFull && !longAvgPoints[i]->post(&client, true)) ||
                (longAvgFull && !correctionPoints[i]->post(&client, true)))
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
