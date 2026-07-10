//
// Calibrates and displays temperature readings for 8 multiplexed sensors and uploads calibration telemetry.
//
// Uses the ESP32-S3 Playground board with rotary encoder and TFT display instead of buttons.
// Displays real-time temperature data and correction factors. On startup, saved calibration deltas
// are loaded from Preferences, displayed, and printed to Serial. The sketch probes all 8 multiplexer
// channels, initializes each TempSensor, and tracks detected sensor count for InfluxDB batching.
//
// Rotary encoder button cycles between Raw Temps (timed average), Corrected (timed average + correction),
// and Correction (current computed delta). Correction factors are periodically saved to Preferences
// and telemetry is written to InfluxDB with reconnect and buffering.
//
// Turn the rotary encoder to highlight different sensors in the display.
//
#include "WiFiSettings.h"
#include "ESP32_S3_Playground.h"
#include "Timer.h"
#include "TempSensor.h"
#include "Multiplexer.h"
#include "SerialX.h"
//#include "Influx.h"
#include "TempCalibrator.h"
#include "TimedScatterPlot.h"
#include "TimedValues.h"
#include <Wire.h>

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

// The averaging window used for computing correction factors
constexpr unsigned long SAMPLING_DURATION_S = 60;

// Sensor and telemetry timing
constexpr uint16_t SAMPLING_INTERVAL_MS = 10000;  // 10 seconds between measurements
constexpr uint16_t SAMPLE_RATE_MS = 100;  // 10 Hz sample rate
constexpr uint8_t NUM_SAMPLES = 10;  // Average 10 measurements
constexpr uint8_t DISCARD_SAMPLES = 0;  // Discard 0 measurements
constexpr auto INFLUX_INTERVAL_S = 10;
constexpr auto PREFS_INTERVAL_S = 60;
constexpr auto SAVED_INFO_WAIT_S = 60;
constexpr auto WIFI_RESET_DELAY_S = 10;

constexpr const char* CALIBRATOR_PREFS_NAMESPACE = "Calibrator";
constexpr const char* TEMP_KEY_PREFIX = "Temp ";
constexpr const char* ID_KEY_PREFIX = "ID ";

// Calibrator for temperature corrections and timed averages
TempCalibrator calibrator(NUM_SENSORS, SAMPLING_DURATION_S * 1000UL, TempCalibrator::BaselineMode::FIRST_SENSOR);

// Track current temperature readings for each sensor
float currentReadings[NUM_SENSORS] = {NAN, NAN, NAN, NAN, NAN, NAN, NAN, NAN};

Format tempFormat("###.##");
Format avgTempFormat("###.###");
Format correctionFormat("+#.###");

// Time window for sensor 0 plot (10 minutes of history)
constexpr unsigned long TIME_WINDOW_PLOT_MS = 600000;   // 10 minutes

// Timed value buffer for sensor 0 (optimized rolling window)
TimedValuesBase<float> sensorPlotData(TIME_WINDOW_PLOT_MS, 64);

// Scatter plot renderer (uses TimedScatterPlot for optimized rendering)
TimedScatterPlot* plotSensor0 = nullptr;

Timer sensorReadTrigger(SAMPLING_INTERVAL_MS);
//TimerSecs influxTrigger(INFLUX_INTERVAL_S);
TimerSecs prefsTrigger(PREFS_INTERVAL_S);

//InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);
//Influx influx(WIFI_SSID, WIFI_PASSWORD, &client);

//Point points[] =
//{
//   Point("Air"),
//   Point("Air"),
//   Point("Air"),
//   Point("Air"),
//   Point("Air"),
//   Point("Air"),
//   Point("Air"),
//   Point("Air"),
//};

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

   // print saved factors for easy paste into TempSensorCallibration.h
   Serial.println("Copy this data to libraries\\Woyak\\TempSensorCallibration.h");
   Serial.println(String("Based on averaging data points over the last ") + SAMPLING_DURATION_S + " seconds, here are the calibration factors:");
   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      Serial.print("\"");
      Serial.print(ids[i].c_str());
      Serial.print("\", ");
      Serial.print(tempCorrections[i], 3);
      Serial.print(", 0.000,");
      Serial.println();
   }
}



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
   while (!arduino.encoder.button.wasPressed() && !waitTimer.ready())
   {
      delay(1);
   }

   // print again in case the user didn't have serial open the first time
   printCalibrationCodeToSerial();
}

void displayHeader()
{
   arduino.setTextSize(3);
   arduino.fillRect(0, 0, arduino.width(), arduino.charH(), Color::ORANGE);
   arduino.printlnC("Calibrating...", Color::WHITE, Color::ORANGE);
}

void displaySensorList()
{
   arduino.setTextSize(2);

   // Display table header (no heading for first column)
   arduino.print("    ", Color::HEADING);
   arduino.print("Type     ", Color::HEADING);
   arduino.print("Now    ", Color::HEADING);
   arduino.print("Avg      ", Color::HEADING);
   arduino.println("Delta", Color::HEADING);

   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      Multiplexer::select(i);
      TempSensor& sensor = sensors[i];
      bool sensorExists = sensor.exists();

      // Sensor number
      arduino.print((i + 1), Color::LABEL);
      arduino.print("   ");

      // Sensor type
      if (sensorExists)
      {
         String type = sensor.type();
         arduino.print(type, Color::LABEL);
         // Pad to 9 characters
         for (int pad = type.length(); pad < 9; pad++)
            arduino.print(" ");
      }
      else
      {
         arduino.print("---      ");
      }

      if (sensorExists)
      {
         float correction = calibrator.getCorrection(i);
         float timedAverage = calibrator.getAverage(i);
         float current = currentReadings[i];

         // Display current temp (padded to 9 chars)
         if (!isnan(current))
         {
            arduino.print(current, tempFormat, Color::VALUE);
            arduino.print(" ");
         }
         else
         {
            arduino.print("         ");
         }

         // Display averaged temp (padded to 10 chars)
         if (!isnan(timedAverage))
         {
            arduino.print(timedAverage, avgTempFormat, Color::VALUE);
            arduino.print(" ");
         }
         else
         {
            arduino.print("          ");
         }

         // Display correction factor
         if (!isnan(correction))
         {
            arduino.println(correction, correctionFormat, Color::VALUE);
         }
         else
         {
            arduino.println("          ");
         }
      }
      else
      {
         arduino.print("----    ");
         arduino.print("----      ");
         arduino.println("----");
      }
   }
}

/// <summary>
/// Renders the scatter plot for sensor 0 showing 10 minutes of history.
/// The plot fills the remaining display space below the calibration table.
/// </summary>
void displayScatterPlots()
{
   if (!plotSensor0) return;

   // The plot automatically handles full-screen rendering with topOffset positioning
   plotSensor0->render();
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

      //points[i].addTag("location", (String("Calibration ") + (i + 1)).c_str());
      arduino.preferences.putString((String(ID_KEY_PREFIX) + i).c_str(), sensorExists ? sensor.id() : "");
   }
   arduino.preferences.end();

   //uint8_t batchSize = std::max((uint8_t)1, detectedSensorCount);
   //client.setWriteOptions(WriteOptions().batchSize(batchSize).bufferSize(2 * batchSize));
   Serial.print("Detected sensors: ");
   Serial.println(detectedSensorCount);

   //arduino.echoToSerial = true;
   //arduino.clearDisplay();
   //arduino.setTextSize(3);
   //arduino.println("Init", Color::HEADING);
   //arduino.moveCursorY(10);

   //arduino.setTextSize(2);
   //if (!influx.begin(&arduino))
   //{
   //   Util::reset(WIFI_RESET_DELAY_S);
   //}

   // Initialize TimedScatterPlot for sensor 0 data
   // The plot starts below the calibration table and fills the remaining display space
   plotSensor0 = new (std::nothrow) TimedScatterPlot(&arduino, sensorPlotData, TIME_WINDOW_PLOT_MS);

   if (!plotSensor0)
   {
      Serial.println("Failed to allocate scatter plot");
      return;
   }

   // Calculate topOffset: header (size 3) + table header (size 2) + 8 sensor rows (size 2)
   // Header: charH() at size 3
   // Table: (1 header row + NUM_SENSORS rows) * 2 * charH() at size 2
   int16_t headerHeight = arduino.charH();  // "Calibrating..." header at size 3 = charH()
   int16_t tableHeight = (1 + NUM_SENSORS) * 2 * arduino.charH();  // header + 8 rows at size 2
   int16_t topOffset = headerHeight + tableHeight + 4;  // Add small padding

   plotSensor0->setTopOffset(topOffset);
}

long count = 0;
void loop()
{
   if (arduino.encoder.button.wasPressed())
   {
      arduino.clearDisplay();
   }

   // ------------------------------------------- sample + display
   if (sensorReadTrigger.ready())
   {
      for (int i = 0; i < NUM_SENSORS; i++)
      {
         Multiplexer::select(i);
         TempSensor& sensor = sensors[i];
         if (sensor.exists())
         {
            float temp = sensor.sample(NUM_SAMPLES, DISCARD_SAMPLES, SAMPLE_RATE_MS);
            currentReadings[i] = temp;
            calibrator.set(i, temp);
            count++;

            // Add sensor 0 data to TimedValues for scatter plot
            if (i == 0 && !isnan(temp))
            {
               sensorPlotData.set(temp);
            }
         }
      }

      // Sample timing is managed by sensorReadTrigger timer
   }

   // Always display the data
   arduino.setCursor(0, 0);
   displayHeader();

   arduino.setCursor(0, arduino.charH());
   displaySensorList();

   // Display scatter plots for sensor 0
   displayScatterPlots();

   // ------------------------------------------- save to flash
   if (prefsTrigger.ready())
   {
      arduino.preferences.begin(CALIBRATOR_PREFS_NAMESPACE, false);
      for (uint8_t i = 0; i < NUM_SENSORS; i++)
      {
         arduino.preferences.putFloat((String(TEMP_KEY_PREFIX) + i).c_str(), calibrator.getCorrection(i));
      }
      arduino.preferences.end();

      printCalibrationCodeToSerial();
   }

   // ------------------------------------------- send to INFLUX
   //if (influxTrigger.ready())
   //{
   //   if (influx.ensureWiFiConnected())
   //   {
   //      digitalWrite(BUILTIN_LED, HIGH);

   //      Serial.print("Uploading to InfluxDB... ");
   //      Serial.print(count);
   //      Serial.println(" values collected");
   //      count = 0;

   //      bool writeFailed = false;
   //      float baseline = calibrator.getBaseline();

   //      for (int i = 0; i < NUM_SENSORS; i++)
   //      {
   //         float temperature = calibrator.getAverage(i);
   //         if (!sensors[i].exists() || std::isnan(temperature))
   //         {
   //            continue;
   //         }

   //         float tDelta = calibrator.getCorrection(i);
   //         float tCorrected = temperature + tDelta;
   //         float tCorrectedDelta = std::isnan(baseline) ? NAN : (tCorrected - baseline);

   //         points[i].clearFields();
   //         // the current readings
   //         points[i].addField("temperature", temperature, 3);

   //         // the deltas from the baseline
   //         points[i].addField("tDelta", tDelta, 4);

   //         // the current readings using the correction factors. Once these
   //         // stop changing, the correction factors have converged
   //         points[i].addField("tCorrected", tCorrected, 3);

   //         // corrected delta from baseline
   //         points[i].addField("tCorrectedDelta", tCorrectedDelta, 4);

   //         if (!client.writePoint(points[i]))
   //         {
   //            writeFailed = true;
   //            break;
   //         }
   //      }

   //      if (!writeFailed && !client.isBufferEmpty())
   //      {
   //         writeFailed = !client.flushBuffer();
   //      }

   //      if (writeFailed)
   //      {
   //         Serial.println("InfluxDB write failed: ");
   //         Serial.println(client.getLastErrorMessage());
   //      }
   //      else
   //      {
   //         printCalibrationCodeToSerial();
   //      }

   //      digitalWrite(BUILTIN_LED, LOW);
   //   }
   //}
}
