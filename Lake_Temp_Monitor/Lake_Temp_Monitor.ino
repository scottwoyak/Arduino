#include "Feather.h"
#include "TempSensor.h"
#include "TimedAverager.h"
#include "Stopwatch.h"
#include "Adafruit_SleepyDog.h"
#include "SerialX.h"
#include "Influx.h"
#include <string>
#include "I2CMultiplexor.h"

#include "WiFiSettings.h"

Format humFormat("##.#%");
Format tempFormat("###.## F");

constexpr auto version = "v0.90";

Feather feather;

I2CMultiplexor multi;

constexpr auto INFLUX_MEASUREMENT = "Air";
constexpr auto INFLUX_INTERVAL_S = 15; // log data to InfluxDB every this many seconds
constexpr auto WATCHDOG_INTERVAL_S = 60; // reboot if we haven't logged data for this many seconds
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);

/*
TimedPoint pointSurface(INFLUX_INTERVAL_S, INFLUX_MEASUREMENT);
TimedPoint pointBottom(INFLUX_INTERVAL_S, INFLUX_MEASUREMENT);
TimedPoint point3(INFLUX_INTERVAL_S, INFLUX_MEASUREMENT);
TimedPoint point4(INFLUX_INTERVAL_S, INFLUX_MEASUREMENT);

Field* tempSurfaceField = pointSurface.addTimeAveragedField("temperature", 3);
Field* tempBottomField = pointBottom.addTimeAveragedField("temperature", 3);
Field* temp3Field = point3.addTimeAveragedField("temperature", 3);
Field* temp4Field = point4.addTimeAveragedField("temperature", 3);

Field* humSurfaceField = pointSurface.addTimeAveragedField("humidity", 2);
Field* humBottomField = pointBottom.addTimeAveragedField("humidity", 2);
Field* hum3Field = point3.addTimeAveragedField("humidity", 2);
Field* hum4Field = point4.addTimeAveragedField("humidity", 2);
*/

constexpr uint8_t NUM_SENSORS = 4;
TempSensor* sensors[NUM_SENSORS];
TimedPoint* points[NUM_SENSORS];
Field* tempFields[NUM_SENSORS];
Field* humFields[NUM_SENSORS];
uint8_t sensorPorts[] = { 2, 3, 4, 5 };

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
   feather.display.setTextWrap(false);
   pinMode(BUILTIN_LED, OUTPUT);

   feather.echoToSerial = true;
   feather.clearDisplay();
   feather.println("Initializing", Color::HEADING);
   feather.moveCursorY(feather.charH() / 2);

   multi.begin();

   feather.print("Sensors... ", Color::LABEL);
   for (int i = 0; i < NUM_SENSORS; i++)
   {
      SerialX::println();
      SerialX::println("Sensor ", i);
      multi.select(sensorPorts[i]);
      if (sensors[i]->begin(true))
      {
         SerialX::println("         Type: ", sensors[i]->type());
         SerialX::println("      Address: ", sensors[i]->address());
         SerialX::println("           ID: ", sensors[i]->id());
         SerialX::println("   Correction: ", sensors[i]->tempCorrectionF);
      }
      else
      {
         // TODO null out this sensor
         SerialX::println("FAILED");
      }
   }
   feather.printlnR("ok", Color::VALUE);


   Influx::begin(&feather, WIFI_SSID, WIFI_PASSWORD, &client);

   points[0]->addTag("location", "Water 1"); // Surface
   points[1]->addTag("location", "Water 2"); // Bottom
   points[2]->addTag("location", "3 Feet"); // Bottom
   points[3]->addTag("location", "Deep"); // Bottom

   feather.clearDisplay();
   feather.echoToSerial = false;

   Watchdog.enable(WATCHDOG_INTERVAL_S * 1000);
}

void loop()
{
   Watchdog.reset();

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
      feather.println("Wifi connection lost");
   }

   feather.setCursor(0, 0);
   feather.display.setTextSize(2);
   feather.print("Influx", Color::HEADING);
   feather.println();

   feather.setTextSize(3);
   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      if (sensors[i]->exists())
      {
         float temp = tempFields[i]->get();
         float hum = humFields[i]->get();

         feather.print(temp, tempFormat, Color::VALUE);
         feather.printR(hum, humFormat, Color::VALUE);
         feather.println();
      }
      else
      {
         feather.print("----", Color::VALUE);
         feather.printR("----", Color::VALUE);
         feather.println();
      }
   }

   feather.setTextSize(2);
   feather.printR(version, Color::SUB_LABEL);

   // Write point
   if (points[0]->ready())
   {
      digitalWrite(BUILTIN_LED, HIGH);

      bool failed = false;
      for (uint8_t i = 0; i < NUM_SENSORS; i++)
      {
         if (sensors[i]->exists())
         {
            if (points[i]->post(&client) == false)
            {
               failed = true;
               Serial.println("InfluxDB write failed: ");
               Serial.println(client.getLastErrorMessage());
            }
         }
      }

      if (failed)
      {
         // sleep for 10 seconds (reboot upon wake up)
         feather.deepSleep(10);
      }

      digitalWrite(BUILTIN_LED, LOW);
   }
}


