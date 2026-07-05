//
// Lake temperature monitor with display and InfluxDB logging.
//
// Reads temperature and humidity from four sensors at different water depths (surface,
// 3 feet, bottom, deep) via I2C multiplexor and displays them on a Feather display.
// Logs readings to InfluxDB every 15 seconds with automatic WiFi reconnection and
// watchdog reboot safety. Reboots daily and after 5 minutes of startup initialization
// failure.
//

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include "TempSensor.h"
#include <Adafruit_SleepyDog.h>
#include "SerialX.h"
#include "Influx.h"
#include "I2CMultiplexor.h"
#include "Timer.h"
#include "Util.h"

#include "WiFiSettings.h"

Format humFormat("##.#%");
Format tempFormat("###.## F");

constexpr auto version = "v1.0";

Arduino arduino;
I2CMultiplexor multi;

constexpr auto INFLUX_MEASUREMENT = "Air";
constexpr auto INFLUX_INTERVAL_S = 15;
constexpr auto WATCHDOG_INTERVAL_S = 60;
constexpr auto WIFI_RESET_DELAY_S = 10;

InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);
Influx influx(WIFI_SSID, WIFI_PASSWORD, &client);
Timer influxTimer(INFLUX_INTERVAL_S * 1000);

constexpr uint8_t NUM_SENSORS = 4;

TempSensor* sensors[NUM_SENSORS];
InfluxPoint* points[NUM_SENSORS];
InfluxField* tempFields[NUM_SENSORS];
InfluxField* humFields[NUM_SENSORS];

uint8_t sensorPorts[] = { 0, 1, 2, 3 };  // Surface, 3 feet, bottom, deep

const char* locations[] = {
   "Surface",
   "3 Feet",
   "Bottom",
   "Deep",
};

void setup()
{
   // If we can't startup after 5 minutes, reboot and try again
   Watchdog.enable(5 * 60 * 1000);

   // Create all the objects
   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      sensors[i] = new TempSensor();
      points[i] = new InfluxPoint(INFLUX_MEASUREMENT);
      tempFields[i] = points[i]->addTimeAveragedField(INFLUX_INTERVAL_S, "temperature", 3);
      humFields[i] = points[i]->addTimeAveragedField(INFLUX_INTERVAL_S, "humidity", 2);
   }

   SerialX::begin();
   Wire.begin();

   arduino.begin();
   arduino.setRotation(DisplayRotation::PORTRAIT);
   arduino.setTextSize(2);
   arduino.display.setTextWrap(false);
   pinMode(BUILTIN_LED, OUTPUT);

   arduino.echoToSerial = true;
   arduino.display.setRotation(2);
   arduino.clearDisplay();
   arduino.println("Initializing", Color::HEADING);
   arduino.moveCursorY(arduino.charH() / 2);

   arduino.print("Sensors... ", Color::LABEL);
   for (int i = 0; i < NUM_SENSORS; i++)
   {
      Serial.println();
      Serial.print("Sensor ");
      Serial.print(i);
      Serial.print(": ");
      Serial.println(locations[i]);
      multi.select(sensorPorts[i]);
      if (sensors[i]->begin(true))
      {
         Serial.print("         Type: ");
         Serial.println(sensors[i]->type());
         Serial.print("      Address: ");
         Serial.println(sensors[i]->address());
         Serial.print("           ID: ");
         Serial.println(sensors[i]->id());
         Serial.print("   Correction: ");
         Serial.println(sensors[i]->tempCorrectionF(), 3);
      }
      else
      {
         delete sensors[i];
         sensors[i] = nullptr;
         Serial.println("FAILED");
      }
   }
   arduino.printlnR("ok", Color::VALUE);

   if (!influx.begin(&arduino))
   {
      Util::reset(WIFI_RESET_DELAY_S);
   }

   for (int i = 0; i < NUM_SENSORS; i++)
   {
      points[i]->addTag("location", locations[i]);
   }

   arduino.clearDisplay();
   arduino.echoToSerial = false;

   Watchdog.enable(WATCHDOG_INTERVAL_S * 1000);
}

void loop()
{
   // Reboot once a day
   if (millis() > 24 * 60 * 60 * 1000)
   {
      Serial.println("Rebooting after 24 hours of uptime");
      Util::reset();
   }

   // Store measured value into point
   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      if (sensors[i]->exists())
      {
         multi.select(sensorPorts[i]);
         tempFields[i]->set(sensors[i]->readTemperatureF());
         humFields[i]->set(sensors[i]->readHumidity());
      }
   }

   // Check WiFi connection and reconnect if needed
   if (!influx.ensureWiFiConnected())
   {
      arduino.println("WiFi connection lost");
      Serial.println("WiFi connection lost");
      Util::reset(WIFI_RESET_DELAY_S);
   }

   arduino.setCursor(0, 0);
   arduino.setTextSize(2);
   arduino.print("Lake Temp", Color::HEADING);
   arduino.println();

   arduino.setTextSize(3);
   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      if (sensors[i]->exists())
      {
         float temp = tempFields[i]->get();
         float hum = humFields[i]->get();

         arduino.setTextSize(4);
         arduino.println(temp, tempFormat, Color::VALUE);
         arduino.setTextSize(2);
         arduino.print(hum, humFormat, Color::WHITE);
         arduino.printR(locations[i], Color::SUB_LABEL);
         arduino.println();
         arduino.moveCursorY(arduino.charH() / 2);
      }
      else
      {
         arduino.setTextSize(3);
         arduino.println("----", Color::GRAY);
         arduino.setTextSize(2);
         arduino.println("----", Color::GRAY);
         arduino.moveCursorY(arduino.charH() / 2);
      }
   }

   arduino.setTextSize(2);
   arduino.setCursorY(0);
   arduino.printR(version, Color::SUB_LABEL);

   // Write point
   if (influxTimer.ready())
   {
      if (!influx.ensureWiFiConnected())
      {
         Serial.println("WiFi connection lost");
         Util::reset(WIFI_RESET_DELAY_S);
      }

      digitalWrite(BUILTIN_LED, HIGH);

      for (uint8_t i = 0; i < NUM_SENSORS; i++)
      {
         if (sensors[i]->exists())
         {
            if (points[i]->post(&client) == false)
            {
               Serial.println("InfluxDB write failed: ");
               Serial.println(client.getLastErrorMessage());
            }
            else
            {
               // Only reset the Watchdog if we've had a successful write
               Watchdog.reset();
            }
         }
      }

      digitalWrite(BUILTIN_LED, LOW);
   }
}
