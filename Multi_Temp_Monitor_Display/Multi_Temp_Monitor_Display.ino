/// <summary>
/// Displays temperatures from up to 8 I2C-multiplexed sensors and uploads readings to InfluxDB.
/// Hold Button A to show sensor type instead of temperature for each sensor index.
/// </summary>
/// <remarks>
/// Initializes up to 8 TempSensor instances behind an I2C multiplexer and tracks each sensor's
/// location metadata for telemetry tagging. Reads temperature/humidity on SENSOR_READ_INTERVAL_MS
/// cadence and feeds time-averaged fields, using a 60-second averaging window for displayed/uploaded
/// values while keeping upload cadence at INFLUX_INTERVAL_S. Renders either averaged temperature
/// values or sensor type labels (Button A held) on display.
/// 
/// Telemetry flow: each sensor maps to an InfluxPoint with averaged temperature/humidity fields. On
/// each upload interval, all active sensor points are posted, then the client buffer is flushed.
/// Wi-Fi connectivity is monitored continuously and triggers reset on loss.
/// 
/// Typical usage: start the sketch and verify detected sensors in Serial startup output, let averages
/// settle (up to 60 seconds) before using displayed values for decisions, then monitor the Influx
/// stream for per-location averaged temperature/humidity values.
/// </remarks>

#include "ArduinoBoard.h"

#ifndef ARDUINO_BUTTON_SUPPORTED
#error "This sketch requires a board with button support (e.g. Feather ESP32-S3 or Feather M0)."
#endif
#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif
#ifndef ARDUINO_LED_SUPPORTED
#error "This sketch requires a board with onboard NeoPixel LED support (e.g. Feather ESP32-S3 or Waveshare ESP32-S3-Zero)."
#endif

#include "TempSensor.h"
#include "SerialX.h"
#include "Influx.h"
#include "Status.h"
#include "I2CMultiplexor.h"
#include "Timer.h"

#include "WiFiSettings.h"

Format tempFormat("###.## F");

constexpr uint8_t NUM_SENSORS = 8;

constexpr uint8_t INFLUX_TEMP_DECIMAL_PLACES = 3;
constexpr uint8_t INFLUX_HUMIDITY_DECIMAL_PLACES = 2;
constexpr uint8_t SENSOR_CORRECTION_DECIMAL_PLACES = 3;
constexpr uint8_t WIFI_RESET_DELAY_S = 10;
constexpr uint16_t SENSOR_READ_INTERVAL_MS = 500;
constexpr auto INFLUX_MEASUREMENT = "Air";
constexpr auto INFLUX_INTERVAL_S = 15;
constexpr auto AVERAGE_WINDOW_S = 60;
constexpr auto INFLUX_TEMPERATURE_FIELD_NAME = "temperature";
constexpr auto INFLUX_HUMIDITY_FIELD_NAME = "humidity";

Arduino arduino;
NeoPixelStatus status(&arduino.neoPixel);
I2CMultiplexor multi;
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);
Influx influx(WIFI_SSID, WIFI_PASSWORD, &client, &status);
Timer sensorTimer(SENSOR_READ_INTERVAL_MS);
Timer influxTimer(INFLUX_INTERVAL_S * 1000);

TempSensor* sensors[NUM_SENSORS];
InfluxPoint* points[NUM_SENSORS];
InfluxField* tempFields[NUM_SENSORS];
InfluxField* humFields[NUM_SENSORS];

const char* locations[NUM_SENSORS] = {
   "Test 1",
   "Test 2",
   "Test 3",
   "Test 4",
   "Test 5",
   "Test 6",
   "Test 7",
   "Test 8",
};

void setup()
{
   SerialX::begin();
   Wire.begin();

   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      sensors[i] = new TempSensor();
      points[i] = new InfluxPoint(INFLUX_MEASUREMENT);
      tempFields[i] = points[i]->addTimeAveragedField(AVERAGE_WINDOW_S, INFLUX_TEMPERATURE_FIELD_NAME, INFLUX_TEMP_DECIMAL_PLACES);
      humFields[i] = points[i]->addTimeAveragedField(AVERAGE_WINDOW_S, INFLUX_HUMIDITY_FIELD_NAME, INFLUX_HUMIDITY_DECIMAL_PLACES);
      points[i]->addTag("location", locations[i]);
   }

   arduino.begin();
   arduino.setRotation(DisplayRotation::PORTRAIT);
   arduino.setTextSize(2);
   arduino.display.setTextWrap(false);
   pinMode(BUILTIN_LED, OUTPUT);

   status.begin();
   status.setStatus(Status::STARTED);

   arduino.echoToSerial = true;
   arduino.clearDisplay();
   arduino.println("Initializing", Color::HEADING);
   arduino.moveCursorY(arduino.charH() / 2);

   arduino.print("Sensors... ", Color::LABEL);
   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      Serial.println();
      Serial.print("Sensor ");
      Serial.print(i);
      Serial.print(": ");
      Serial.println(locations[i]);

      multi.select(i);
      if (sensors[i]->begin(true))
      {
         if (!sensors[i]->exists())
         {
            Serial.println("Sensor not detected");
         }
         else
         {
            Serial.print("         Type: ");
            Serial.println(sensors[i]->type());
            Serial.print("      Address: ");
            Serial.println(sensors[i]->address());
            Serial.print("           ID: ");
            Serial.println(sensors[i]->id());
            Serial.print("   Correction: ");
            Serial.println(sensors[i]->tempCorrectionF(), SENSOR_CORRECTION_DECIMAL_PLACES);
         }
      }
      else
      {
         status.setStatus(Color::RED);
         Serial.println("FAILED");
      }
   }
   arduino.printlnR("ok", Color::VALUE);

   client.setWriteOptions(WriteOptions().batchSize(NUM_SENSORS).bufferSize(NUM_SENSORS).flushInterval(INFLUX_INTERVAL_S + 1));
   if (!influx.begin(&arduino))
   {
      Util::reset(WIFI_RESET_DELAY_S);
   }

   arduino.clearDisplay();
   arduino.echoToSerial = false;
}

void loop()
{
   const bool showType = arduino.buttonA.isPressed();
   static bool lastShowType = showType;

   if (showType != lastShowType)
   {
      arduino.clearDisplay();
      lastShowType = showType;
   }

   if (sensorTimer.ready())
   {
      for (uint8_t i = 0; i < NUM_SENSORS; i++)
      {
         if (!sensors[i]->exists())
         {
            continue;
         }

         multi.select(i);
         tempFields[i]->set(sensors[i]->readTemperatureF());
         humFields[i]->set(sensors[i]->readHumidity());
      }
   }

   if (!influx.ensureWiFiConnected())
   {
      arduino.println("WiFi connection lost");
      Serial.println("WiFi connection lost");
      Util::reset(WIFI_RESET_DELAY_S);
   }

   arduino.setCursor(0, 0);
   arduino.setTextSize(2);
   arduino.print("Multi Monitor", Color::HEADING);
   arduino.println();
   arduino.moveCursorY(arduino.charH() / 3);

   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      arduino.setTextSize(2);
      arduino.print(i, Color::GRAY);
      arduino.print(" ");
      arduino.setTextSize(3);

      if (!sensors[i]->exists())
      {
         arduino.println("----", Color::GRAY);
         continue;
      }

      if (showType)
      {
         arduino.println(sensors[i]->type(), Color::VALUE);
      }
      else
      {
         float temp = tempFields[i]->get();
         arduino.println(temp, tempFormat, Color::VALUE);
      }
   }

   if (influxTimer.ready())
   {
      digitalWrite(BUILTIN_LED, HIGH);

      bool writeFailed = false;
      for (uint8_t i = 0; i < NUM_SENSORS; i++)
      {
         if (!sensors[i]->exists())
         {
            continue;
         }

         if (!points[i]->post(&client))
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


