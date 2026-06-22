#include "Feather.h"
#include "TempSensor.h"
#include "Adafruit_SleepyDog.h"
#include "SerialX.h"
#include "Influx.h"
#include <string>
#include <I2CMultiplexor.h>
#include <Timer.h>

#include <WiFiSettings.h>

Format humFormat("##.#%");
Format tempFormat("###.## F");

constexpr auto version = "v1.0";

Feather feather;

I2CMultiplexor multi;

constexpr auto INFLUX_MEASUREMENT = "Air";
constexpr auto INFLUX_INTERVAL_S = 15; // log data to InfluxDB every this many seconds
constexpr auto WATCHDOG_INTERVAL_S = 60; // reboot if we haven't logged data for this many seconds
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);
Timer influxTimer(INFLUX_INTERVAL_S * 1000);

constexpr uint8_t NUM_SENSORS = 5;
TempSensor* sensors[NUM_SENSORS];
SimplePoint* points[NUM_SENSORS];
Field* tempFields[NUM_SENSORS];
Field* humFields[NUM_SENSORS];
uint8_t sensorPorts[] = { 0, 1, 2, 3, 4 };

const char* locations[] = {
   "Test 1",
   "Test 2",
   "Test 3",
   "Test 4",
   "Test 5",
};

void setup()
{
   Wire.begin();
   SerialX::begin();

   // create all the objects
   for (uint8_t i = 0; i < NUM_SENSORS; i++)
   {
      sensors[i] = new TempSensor();
      points[i] = new SimplePoint(INFLUX_MEASUREMENT);
      tempFields[i] = points[i]->addTimeAveragedField(INFLUX_INTERVAL_S, "temperature", 3);
      humFields[i] = points[i]->addTimeAveragedField(INFLUX_INTERVAL_S, "humidity", 2);
   }

   feather.begin();
   feather.setRotation(DisplayRotation::PORTRAIT);
   feather.setTextSize(2);
   feather.display.setTextWrap(false);
   pinMode(BUILTIN_LED, OUTPUT);

   feather.echoToSerial = true;
   feather.clearDisplay();
   feather.println("Initializing", Color::HEADING);
   feather.moveCursorY(feather.charH() / 2);

   feather.print("Sensors... ", Color::LABEL);
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
         // TODO null out this sensor
         Serial.println("FAILED");
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
}

void loop()
{
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
   feather.print("Temp", Color::HEADING);
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
         feather.moveCursorY(-2);
         feather.setTextSize(2);
         feather.print(hum, humFormat, Color::WHITE);
         feather.printR(locations[i], Color::SUB_LABEL);
         feather.println();
      }
      else
      {
         feather.setTextSize(3);
         feather.println("----", Color::GRAY);
         feather.moveCursorY(-2);
         feather.setTextSize(2);
         feather.println("----", Color::GRAY);
      }
   }

   feather.setTextSize(2);
   feather.setCursorY(0);
   feather.printR(version, Color::SUB_LABEL);

   // Write point
   if (influxTimer.ready())
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
         }
      }

      digitalWrite(BUILTIN_LED, LOW);
   }
}


