//
// Calibrates and displays temperature readings for 8 multiplexed sensors and uploads calibration telemetry.
//
// On startup, saved calibration deltas are loaded from Preferences, displayed, and printed to Serial
// in copy/paste format for TempSensorCallibration.h updates. The sketch probes all 8 multiplexer
// channels, initializes each TempSensor, and tracks detected sensor count for InfluxDB batching.
//
// Button A starts live calibration display cycling between Raw Temps (timed average), Corrected
// (timed average + correction), and Correction (current computed delta). Correction factors are
// periodically saved to Preferences and telemetry is written to InfluxDB with reconnect and buffering.
//
// Typical usage: power on and review saved deltas, press Button A to begin live calibration,
// then wait for corrections to stabilize and copy printed values for code use.
//
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
constexpr unsigned long SAMPLING_DURATION_S = 60;

// Sensor and telemetry timing
constexpr uint16_t SAMPLING_INTERVAL_MS = 100;
constexpr auto INFLUX_INTERVAL_S = 10;
constexpr auto PREFS_INTERVAL_S = 60;
constexpr auto SAVED_INFO_WAIT_S = 60;
constexpr auto WIFI_RESET_DELAY_S = 10;

constexpr const char* CALIBRATOR_PREFS_NAMESPACE = "Calibrator";
constexpr const char* TEMP_KEY_PREFIX = "Temp ";
constexpr const char* ID_KEY_PREFIX = "ID ";

// Calibrator for temperature corrections and timed averages
TempCalibrator calibrator(NUM_SENSORS, SAMPLING_DURATION_S * 1000UL, TempCalibrator::BaselineMode::FIRST_SENSOR);

Format tempFormat("###.## F");
Format correctionFormat("+#.###");

Timer sensorReadTrigger(SAMPLING_INTERVAL_MS);
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

enum class View : uint8_t
{
   RawTemps = 0,
   Corrected,
   Correction,
};

inline View& operator++(View& view)
{
   switch (view)
   {
   case View::RawTemps:
      view = View::Corrected;
      break;
   case View::Corrected:
      view = View::Correction;
      break;
   default:
      view = View::RawTemps;
      break;
   }
   return view;
}

inline View operator++(View& view, int)
{
   View previous = view;
   ++view;
   return previous;
}

View view = View::RawTemps;

void printCalibrationCodeToSerial()
{
   // load saved correction factors
   feather.preferences.begin(CALIBRATOR_PREFS_NAMESPACE, false);
   float tempCorrections[NUM_SENSORS];
   std::string ids[NUM_SENSORS];

   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      tempCorrections[i] = feather.preferences.getFloat((String(TEMP_KEY_PREFIX) + i).c_str());
      ids[i] = feather.preferences.getString((String(ID_KEY_PREFIX) + i).c_str()).c_str();
   }
   feather.preferences.end();

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
   feather.preferences.begin(CALIBRATOR_PREFS_NAMESPACE, false);
   float tempCorrections[NUM_SENSORS];

   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      tempCorrections[i] = feather.preferences.getFloat((String(TEMP_KEY_PREFIX) + i).c_str());
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
   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      feather.print((i + 1), Color::LABEL);
      feather.moveCursorX(10);

      if (isnan(tempCorrections[i]))
      {
         feather.println("-----", Color::GRAY);
      }
      else
      {
         feather.println(tempCorrections[i], correctionFormat, Color::VALUE);
      }
   }

   TimerSecs waitTimer(SAVED_INFO_WAIT_S);
   while (!feather.buttonA.wasPressed() && !waitTimer.ready())
   {
      delay(1);
   }

   // print again in case the user didn't have serial open the first time
   printCalibrationCodeToSerial();
}

void waitForButtonPress()
{
   feather.clearDisplay();

   while (!feather.buttonA.wasPressed())
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

   feather.preferences.begin(CALIBRATOR_PREFS_NAMESPACE, false);
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

      points[i].addTag("location", (String("Calibration ") + (i + 1)).c_str());
      feather.preferences.putString((String(ID_KEY_PREFIX) + i).c_str(), sensorExists ? sensor.id() : "");
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
   if (feather.buttonA.wasPressed())
   {
      feather.clearDisplay();
      view++;
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
            float temp = sensor.readTemperatureF();
            calibrator.set(i, temp);
            count++;
         }
      }

      feather.setCursor(0, 0);
      feather.setTextSize(2);
      switch (view)
      {
      case View::RawTemps:
         feather.println("Raw Temps", Color::HEADING);
         break;

      case View::Corrected:
         feather.println("Corrected", Color::HEADING);
         break;

      case View::Correction:
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

         // values
         feather.print((i + 1), Color::LABEL);
         feather.moveCursorX(feather.charW() / 2);
         if (sensorExists)
         {
            float correction = calibrator.getCorrection(i);
            float timedAverage = calibrator.getAverage(i);
            switch (view)
            {
            case View::RawTemps:
               feather.println(timedAverage, tempFormat, Color::VALUE);
               break;

            case View::Corrected:
               feather.println(timedAverage + correction, tempFormat, Color::VALUE);
               break;

            case View::Correction:
               feather.println(correction, correctionFormat, Color::VALUE);
               break;
            }
         }
         else
         {
            feather.println("-----", Color::GRAY);
         }
      }
   }

   // ------------------------------------------- save to flash
   if (prefsTrigger.ready())
   {
      feather.preferences.begin(CALIBRATOR_PREFS_NAMESPACE, false);
      for (uint8_t i = 0; i < NUM_SENSORS; i++)
      {
         feather.preferences.putFloat((String(TEMP_KEY_PREFIX) + i).c_str(), calibrator.getCorrection(i));
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

            float tDelta = calibrator.getCorrection(i);
            float tCorrected = temperature + tDelta;
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
         else
         {
            printCalibrationCodeToSerial();
         }

         digitalWrite(BUILTIN_LED, LOW);
      }
   }
}


