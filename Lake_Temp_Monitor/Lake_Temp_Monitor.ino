#include "Feather.h"
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

Feather feather;

I2CMultiplexor multi;

constexpr auto INFLUX_MEASUREMENT = "Air";
constexpr auto INFLUX_INTERVAL_S = 15; // log data to InfluxDB every this many seconds
constexpr auto WATCHDOG_INTERVAL_S = 60; // reboot if we haven't logged data for this many seconds
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);

constexpr uint8_t NUM_SENSORS = 4;
TempSensor* sensors[NUM_SENSORS];
TimedPoint* points[NUM_SENSORS];
Field* tempFields[NUM_SENSORS];
Field* humFields[NUM_SENSORS];
uint8_t sensorPorts[] = { 0, 1, 2, 3 };

const char* locations[] = {
   "Surface",
   "3 Feet",
   "Bottom",
   "Deep",
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

   feather.begin();
   feather.setRotation(DisplayRotation::PORTRAIT);
   feather.setTextSize(2);
   feather.display.setTextWrap(false);
   pinMode(BUILTIN_LED, OUTPUT);

   feather.echoToSerial = true;
   feather.display.setRotation(2);
   feather.clearDisplay();
   feather.println("Initializing", Color::HEADING);
   feather.moveCursorY(feather.charH() / 2);

   multi.begin();

   feather.print("Sensors... ", Color::LABEL);
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
   feather.printlnR("ok", Color::VALUE);


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

   feather.setCursor(0, 0);
   feather.setTextSize(2);
   feather.print("Lake Temp", Color::HEADING);
   feather.println();

   feather.setTextSize(3);
   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      if (sensors[i]->exists())
      {
         float temp = tempFields[i]->get();
         float hum = humFields[i]->get();

         feather.setTextSize(4);
         feather.println(temp, tempFormat, Color::VALUE);
         feather.setTextSize(2);
         feather.print(hum, humFormat, Color::WHITE);
         feather.printR(locations[i], Color::SUB_LABEL);
         feather.println();
         feather.moveCursorY(feather.charH() / 2);
      }
      else
      {
         feather.setTextSize(3);
         feather.println("----", Color::GRAY);
         feather.setTextSize(2);
         feather.println("----", Color::GRAY);
         feather.moveCursorY(feather.charH() / 2);
      }
   }

   feather.setTextSize(2);
   feather.setCursorY(0);
   feather.printR(version, Color::SUB_LABEL);

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

      digitalWrite(BUILTIN_LED, LOW);
   }
}


