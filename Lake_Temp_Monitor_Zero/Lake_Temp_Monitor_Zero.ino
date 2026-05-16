#include "TempSensor.h"
#include "TimedAverager.h"
#include "Adafruit_SleepyDog.h"
#include "SerialX.h"
#include "Influx.h"
#include <string>
#include <I2CMultiplexor.h>
#include <CountdownTimer.h>
#include <ESP32TempSensor.h>
#include <Status.h>

#include "WiFiSettings.h"

Format humFormat("##.#%");
Format tempFormat("###.## F");
Format countdownFormat("##");

constexpr auto version = "v1.0";

I2CMultiplexor multi;

constexpr auto INFLUX_MEASUREMENT = "Air";
constexpr auto INFLUX_INTERVAL_S = 15; // log data to InfluxDB every this many seconds
constexpr auto WATCHDOG_INTERVAL_S = 60; // reboot if we haven't logged data for this many seconds
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);

constexpr uint8_t NUM_SENSORS = 5;
TempSensor* sensors[NUM_SENSORS];
TimedPoint* points[NUM_SENSORS];
Field* tempFields[NUM_SENSORS];
Field* humFields[NUM_SENSORS];
uint8_t sensorPorts[] = { 1, 2, 3, 0, 4 };

constexpr auto I2C_SCL = 7;
constexpr auto I2C_SDA = 8;

constexpr auto BLUE_LED_PIN = 3;
constexpr auto GREEN_LED_PIN = 4;
constexpr auto RED_LED_PIN = 6;

LedStatus status(BLUE_LED_PIN, GREEN_LED_PIN);
Led redLed(RED_LED_PIN);

const char* locations[] = {
   "Baseline Surface",
   "Baseline 3 Feet",
   "Baseline Bottom",
   "Baseline Enclosure",
   "Baseline CPU",
};

void setup()
{
   // if we can't startup after 5 minutes, reboot and try again
   Watchdog.enable(5 * 60 * 1000);

   // create all the objects
   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      sensors[i] = new TempSensor();
      points[i] = new TimedPoint(INFLUX_INTERVAL_S, INFLUX_MEASUREMENT);
      tempFields[i] = points[i]->addTimeAveragedField("temperature", 3);
      humFields[i] = points[i]->addTimeAveragedField("humidity", 2);
   }

   SerialX::begin();

   SerialX::println("Initializing");

   // Initialize I2C with custom pins
   Wire.begin(I2C_SDA, I2C_SCL);

   status.begin();
   redLed.begin();

   SerialX::print("Sensors... ");
   for (int i = 0; i < NUM_SENSORS; i++)
   {
      SerialX::println();
      SerialX::print("Sensor ", i);
      SerialX::print(": ");
      SerialX::println(locations[i]);

      bool status;
      if (i == NUM_SENSORS - 1)
      {
         status = sensors[i]->begin(new ESP32TempSensor(), true);
      }
      else
      {
         multi.select(sensorPorts[i]);
         status = sensors[i]->begin(true);
      }

      if (status)
      {
         SerialX::println("         Type: ", sensors[i]->type());
         SerialX::println("      Address: ", sensors[i]->address());
         SerialX::println("           ID: ", sensors[i]->id());
         SerialX::println("   Correction: ", sensors[i]->tempCorrectionF(), 3);
      }
      else
      {
         // TODO null out this sensor
         SerialX::println("FAILED");
      }
   }
   SerialX::println("ok");

   Influx::begin(&status, WIFI_SSID, WIFI_PASSWORD, &client);

   for (int i = 0; i < NUM_SENSORS; i++)
   {
      points[i]->addTag("location", locations[i]);
   }

   Watchdog.enable(WATCHDOG_INTERVAL_S * 1000);
}

void loop()
{
   // reboot once a day
   if (millis() > 24 * 60 * 60 * 1000)
   {
      Serial.println("Rebooting after 24 hours of uptime");
      Util::reset();
   }

   redLed.turnOff(); // turned on when uploading data

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
   if (wifiMulti.run() != WL_CONNECTED)
   {
      Serial.println("WiFi connection lost");
      Util::reset(10);
   }

   // Write point
   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      if (sensors[i]->exists() == false)
      {
         continue;
      }

      if (points[i]->ready())
      {
         redLed.turnOn();
         if (points[i]->post(&client, true) == false)
         {
            Serial.println("InfluxDB write failed: ");
            Serial.println(client.getLastErrorMessage());
         }
         else
         {
            // only reset the Watchdog if we've had a successful write
            Watchdog.reset();
         }
      }
   }
}


