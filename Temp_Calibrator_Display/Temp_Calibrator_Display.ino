// -------------------------------------------------------------------------------------------------
//
// Calibrates and displays temperature readings for 8 multiplexed sensors, then uploads
// raw/corrected values and calibration deltas to InfluxDB.
//
// Detailed behavior:
// - On startup, previously saved calibration deltas are loaded from Preferences, displayed,
//   and printed to Serial in copy/paste format for TempSensorCallibration.h updates.
// - The sketch probes all 8 multiplexer channels, initializes each TempSensor, and tracks
//   detected sensor count to optimize InfluxDB batch settings.
// - Button A starts live calibration display cycling between 3 views:
//   1) Raw Temps (timed average)
//   2) Corrected (timed average + correction)
//   3) Correction (current computed delta)
//
// Calibration and persistence flow:
// - TempCalibrator computes per-sensor timed averages and correction values relative to baseline.
// - The same timed-average source is used for display, correction flow, and Influx writes.
// - Correction factors are periodically saved to Preferences and echoed to Serial.
//
// Telemetry flow:
// - At fixed intervals, the sketch publishes per-sensor raw/corrected values and deltas
//   to InfluxDB (with Wi-Fi reconnect handling and buffered flush).
//
// Typical usage:
// - Power on and review saved deltas.
// - Press Button A to begin live calibration.
// - Leave the system running until corrections stabilize, then copy printed values for code use.
//
// -------------------------------------------------------------------------------------------------
#include "WiFiSettings.h"
#include "Timer.h"
#include "TempSensor.h"
#include "Multiplexer.h"
#include "Feather.h"
#include "SerialX.h"
#include "Influx.h"
#include "TempCalibrator.h"
#include <Wire.h>

Feather feather;
NeoPixelStatus status(&feather.neoPixel);
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
constexpr auto CORRECTION_AVG_S = 60;
constexpr unsigned long CORRECTION_AVG_MS = 1000UL * CORRECTION_AVG_S;

// How often we send data to Influx
constexpr auto INFLUX_INTERVAL_S = 10;
constexpr auto PREFS_INTERVAL_S = 60;
constexpr auto SAVED_INFO_WAIT_S = 60;
constexpr auto WIFI_RESET_DELAY_S = 10;

// Calibrator for temperature corrections and timed averages
TempCalibrator calibrator(NUM_SENSORS, CORRECTION_AVG_MS, TempCalibrator::BaselineMode::FIRST_SENSOR);

Format tempFormat("###.## F");
Format correctionFormat("+#.###");

TimerSecs influxTrigger(INFLUX_INTERVAL_S);
TimerSecs prefsTrigger(PREFS_INTERVAL_S);

InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);
Influx influx(WIFI_SSID, WIFI_PASSWORD, &client, &status);

Point points[] =
{
   Point("Air"),
   Point("Air"),
   Point("Air"),
   Point("Air"),
   Point("Air"),
   Point("Air"),
   Point("Air"),
   Point("Air"),
};

uint8_t view = 0;
constexpr uint8_t NUM_VIEWS = 3;

void printCalibrationCodeToSerial()
{
   // load saved correction factors
   feather.preferences.begin("Calibrator", false);
   float tcorrections[NUM_SENSORS];
   std::string ids[NUM_SENSORS];

   for (int i = 0; i < NUM_SENSORS; i++)
   {
      tcorrections[i] = feather.preferences.getFloat((String("Temp ") + i).c_str());
      ids[i] = feather.preferences.getString((String("ID ") + i).c_str()).c_str();
   }
   feather.preferences.end();

   // print saved factors for easy paste into TempSensorCallibration.h
   Serial.println("Copy this data to libraries\\Woyak\\TempSensorCallibration.h");
   Serial.println(String("Based on averaging data points over the last ") + CORRECTION_AVG_S + " seconds, here are the calibration factors:");
   for (int i = 0; i < NUM_SENSORS; i++)
   {
      Serial.print("\"");
      Serial.print(ids[i].c_str());
      Serial.print("\", ");
      Serial.print(-tcorrections[i], 3);
      Serial.print(", 0.000,");
      Serial.println();
   }
}

void displaySavedInfo()
{
   // load saved correction factors
   feather.preferences.begin("Calibrator", false);
   float tcorrections[NUM_SENSORS];

   for (int i = 0; i < NUM_SENSORS; i++)
   {
      tcorrections[i] = feather.preferences.getFloat((String("Temp ") + i).c_str());
   }
   feather.preferences.end();

   printCalibrationCodeToSerial();

   // display saved temperature factors
   feather.setCursor(0, 0);
   feather.setTextSize(2);
   feather.fillRect(0, 0, feather.width(), 2.5 * feather.charH(), Color::ORANGE);
   feather.moveCursorY((0.5) * feather.charH() / 2);
   feather.printlnC("Temp", Color::WHITE, Color::ORANGE);
   feather.printlnC("Deltas", Color::WHITE, Color::ORANGE);
   feather.moveCursorY(feather.charH() / 2);

   feather.setTextSize(3);
   for (int i = 0; i < NUM_SENSORS; i++)
   {
      feather.print((i + 1), Color::LABEL);
      feather.moveCursorX(10);

      if (isnan(tcorrections[i]))
      {
         feather.println("-----", Color::GRAY);
      }
      else
      {
         feather.println(-tcorrections[i], correctionFormat, Color::VALUE);
      }
   }

   TimerSecs waitTimer(SAVED_INFO_WAIT_S);
   while (feather.buttonA.wasPressed() == false && !waitTimer.ready())
   {
      delay(1);
   }

   // print again in case the user didn't have serial open the first time
   printCalibrationCodeToSerial();
}

void waitForButtonPress()
{
   feather.clearDisplay();

   while (feather.buttonA.wasPressed() == false)
   {
      feather.setCursor(0, 0);
      feather.setTextSize(3);

      for (int i = 0; i < NUM_SENSORS; i++)
      {
         Multiplexer::select(i);
         TempSensor& sensor = sensors[i];
         bool sensorExists = sensor.exists();
         float temp = NAN;
         if (sensorExists)
         {
            temp = sensor.readTemperatureF();
         }
         feather.print((i + 1), Color::LABEL);
         feather.moveCursorX(feather.charW() / 2);
         if (sensorExists)
         {
            feather.println(temp, tempFormat, Color::VALUE);
         }
         else
         {
            feather.println("-----", Color::GRAY);
         }
      }

      feather.setTextSize(2);
      feather.setCursorY(feather.height() - 2 * feather.charH());
      feather.println("Press button", Color::GRAY);
      feather.print("to begin", Color::GRAY);
   }
}

void setup()
{
   SerialX::begin();
   Wire.begin();

   feather.begin();
   feather.setRotation(DisplayRotation::PORTRAIT);

   status.begin();

   displaySavedInfo();

   feather.setTextSize(3);

   feather.preferences.begin("Calibrator", false);
   uint8_t detectedSensorCount = 0;
   for (int i = 0; i < NUM_SENSORS; i++)
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

      points[i].addTag("location", (String("Calibration ") + (i + 1)).c_str());
      feather.preferences.putString((String("ID ") + i).c_str(), sensorExists ? sensor.id() : "");
   }
   feather.preferences.end();

   uint8_t batchSize = std::max((uint8_t)1, detectedSensorCount);
   client.setWriteOptions(WriteOptions().batchSize(batchSize).bufferSize(2 * batchSize));
   Serial.print("Detected sensors: ");
   Serial.println(detectedSensorCount);

   waitForButtonPress();

   feather.echoToSerial = true;
   feather.clearDisplay();
   feather.setTextSize(3);
   feather.println("Init", Color::HEADING);
   feather.moveCursorY(10);

   feather.setTextSize(2);
   if (!influx.begin(&feather))
   {
      Util::reset(WIFI_RESET_DELAY_S);
   }

   delay(1000);

   feather.clearDisplay();
   feather.echoToSerial = false;

   pinMode(BUILTIN_LED, OUTPUT);
   digitalWrite(BUILTIN_LED, LOW);
}

long count = 0;
void loop()
{
   feather.setCursor(0, 0);

   if (feather.buttonA.wasPressed())
   {
      feather.clearDisplay();
      view = (view + 1) % NUM_VIEWS;
   }

   count++;

   // ------------------------------------------- display

   feather.setTextSize(2);
   switch (view)
   {
   case 0:
      feather.println("Raw Temps", Color::HEADING);
      break;

   case 1:
      feather.println("Corrected", Color::HEADING);
      break;

   case 2:
      feather.println("Correction", Color::HEADING);
      break;
   }
   feather.moveCursorY(10);

   feather.setTextSize(3);
   for (int i = 0; i < NUM_SENSORS; i++)
   {
      Multiplexer::select(i);
      TempSensor& sensor = sensors[i];
      bool sensorExists = sensor.exists();
      if (sensorExists)
      {
         float temp = sensor.readTemperatureF();
         calibrator.set(i, temp);
      }

      // values
      feather.print((i + 1), Color::LABEL);
      feather.moveCursorX(feather.charW() / 2);
      if (sensorExists)
      {
         float correction = calibrator.getCorrection(i);
         float displayCorrection = -correction;
         float timedAverage = calibrator.getAverage(i);
         switch (view)
         {
         case 0:
            feather.println(timedAverage, tempFormat, Color::VALUE);
            break;

         case 1:
            feather.println(timedAverage + correction, tempFormat, Color::VALUE);
            break;

         case 2:
            feather.println(displayCorrection, correctionFormat, Color::VALUE);
            break;
         }
      }
      else
      {
         feather.println("-----", Color::GRAY);
      }
   }

   // ------------------------------------------- save to flash
   if (prefsTrigger.ready())
   {
      feather.preferences.begin("Calibrator", false);
      for (int i = 0; i < NUM_SENSORS; i++)
      {
         feather.preferences.putFloat((String("Temp ") + i).c_str(), calibrator.getCorrection(i));
      }
      feather.preferences.end();

      printCalibrationCodeToSerial();
   }

   // ------------------------------------------- send to INFLUX
   if (influxTrigger.ready())
   {
      if (influx.ensureWiFiConnected())
      {
         digitalWrite(BUILTIN_LED, HIGH);

         Serial.print("Uploading to InfluxDB... ");
         Serial.print(count);
         Serial.println(" values collected");
         count = 0;

         bool writeFailed = false;
         float baseline = calibrator.getBaseline();

         for (int i = 0; i < NUM_SENSORS; i++)
         {
            float temperature = calibrator.getAverage(i);
            if (!sensors[i].exists() || std::isnan(temperature))
            {
               continue;
            }

            float tDelta = -calibrator.getCorrection(i);
            float tCorrected = temperature - tDelta;
            float tCorrectedDelta = std::isnan(baseline) ? NAN : (tCorrected - baseline);

            points[i].clearFields();
            // the current readings
            points[i].addField("temperature", temperature, 3);

            // the deltas from the baseline
            points[i].addField("tDelta", tDelta, 4);

            // the current readings using the correction factors. Once these
            // stop changing, the correction factors have converged
            points[i].addField("tCorrected", tCorrected, 3);

            // corrected delta from baseline
            points[i].addField("tCorrectedDelta", tCorrectedDelta, 4);

            if (!client.writePoint(points[i]))
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
            Serial.println("InfluxDB write failed: ");
            Serial.println(client.getLastErrorMessage());
         }

         digitalWrite(BUILTIN_LED, LOW);
      }
   }
}


