#include "TempSensor.h"
#include "TimedAverager.h"
#include "Adafruit_SleepyDog.h"
#include "SerialX.h"
#include "Influx.h"
#include <string>
#include <I2CMultiplexor.h>
#include <CountdownTimer.h>

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
uint8_t sensorPorts[] = { 0, 1, 2, 3, 4 };

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

   Wire.begin();
   SerialX::begin();

   SerialX::println("Initializing");

   multi.begin();

   SerialX::print("Sensors... ");
   for (int i = 0; i < NUM_SENSORS; i++)
   {
      SerialX::println();
      SerialX::print("Sensor ", i);
      SerialX::print(": ");
      SerialX::println(locations[i]);
      multi.select(sensorPorts[i]);
      if (sensors[i]->begin(true))
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


   Influx::begin(&feather, WIFI_SSID, WIFI_PASSWORD, &client);

   for (int i = 0; i < NUM_SENSORS; i++)
   {
      points[i]->addTag("location", locations[i]);
   }

   feather.clearDisplay();
   feather.echoToSerial = false;

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
      feather.println("WiFi connection lost");
      Serial.println("WiFi connection lost");
      Util::reset(10);
   }

   // Write point
   if (points[0]->ready())
   {
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
               // only reset the Watchdog if we've had a successful write
               Watchdog.reset();
            }
         }
      }
   }
}


