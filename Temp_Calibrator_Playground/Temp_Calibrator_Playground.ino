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
#include "Influx.h"
#include "TempCalibrator.h"
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
Influx influx(WIFI_SSID, WIFI_PASSWORD, &client);

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
uint8_t selectedSensor = 0;  // controlled by rotary encoder

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
   arduino.setTextSize(2);
   arduino.fillRect(0, 0, arduino.width(), 2.5 * arduino.charH(), Color::ORANGE);
   arduino.moveCursorY((0.5) * arduino.charH() / 2);
   arduino.printlnC("Temp", Color::WHITE, Color::ORANGE);
   arduino.printlnC("Deltas", Color::WHITE, Color::ORANGE);
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
   arduino.setTextSize(2);
   arduino.fillRect(0, 0, arduino.width(), 2 * arduino.charH(), Color::ORANGE);

   switch (view)
   {
   case View::RawTemps:
      arduino.printlnC("Raw Temps", Color::WHITE, Color::ORANGE);
      break;
   case View::Corrected:
      arduino.printlnC("Corrected", Color::WHITE, Color::ORANGE);
      break;
   case View::Correction:
      arduino.printlnC("Correction", Color::WHITE, Color::ORANGE);
      break;
   }
}

void displaySensorList()
{
   arduino.setTextSize(3);
   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      Multiplexer::select(i);
      TempSensor& sensor = sensors[i];
      bool sensorExists = sensor.exists();

      // Highlight selected sensor
      if (i == selectedSensor)
      {
         arduino.fillRect(0, arduino.getCursorY(), arduino.width(), arduino.charH(), Color::GRAY);
      }

      // Sensor number
      arduino.print((i + 1), (i == selectedSensor) ? Color::BLUE : Color::LABEL);
      arduino.moveCursorX(10);

      if (sensorExists)
      {
         float correction = calibrator.getCorrection(i);
         float timedAverage = calibrator.getAverage(i);

         switch (view)
         {
         case View::RawTemps:
            arduino.println(timedAverage, tempFormat, Color::VALUE);
            break;

         case View::Corrected:
            arduino.println(timedAverage + correction, tempFormat, Color::VALUE);
            break;

         case View::Correction:
            arduino.println(correction, correctionFormat, Color::VALUE);
            break;
         }
      }
      else
      {
         arduino.println("-----", Color::GRAY);
      }
   }
}

void setup()
{
   SerialX::begin();
   Wire.begin();

   arduino.begin();
   arduino.setRotation(DisplayRotation::PORTRAIT);

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

      points[i].addTag("location", (String("Calibration ") + (i + 1)).c_str());
      arduino.preferences.putString((String(ID_KEY_PREFIX) + i).c_str(), sensorExists ? sensor.id() : "");
   }
   arduino.preferences.end();

   uint8_t batchSize = std::max((uint8_t)1, detectedSensorCount);
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

   delay(1000);

   arduino.clearDisplay();
   arduino.echoToSerial = false;

   pinMode(BUILTIN_LED, OUTPUT);
   digitalWrite(BUILTIN_LED, LOW);
}

long count = 0;
void loop()
{
   if (arduino.encoder.button.wasPressed())
   {
      arduino.clearDisplay();
      view++;
   }

   // Update selected sensor from encoder position
   int encoderPos = arduino.encoder.getPosition();
   selectedSensor = constrain(encoderPos % NUM_SENSORS, 0, NUM_SENSORS - 1);

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

      arduino.setCursor(0, 0);
      displayHeader();

      arduino.setCursor(0, 2 * arduino.charH());
      displaySensorList();
   }

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
