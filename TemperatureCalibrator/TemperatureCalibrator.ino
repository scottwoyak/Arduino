#include "Feather_ESP32_S3.h"
#include "Stopwatch.h"
#include "TempSensor.h"
#include "TimedAverager.h"
#include "Multiplexer.h"

#include <WiFiMulti.h>
WiFiMulti wifiMulti;
#define DEVICE "ESP32"

#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>

// for WIFI_SSID and WIFI_PASSWORD
#include "WiFiSettings.h"

// Time zone info
#define TZ_INFO "UTC-5"

// 
// This sketch displays the current temperature on an Arduino ESP32 Feather
//
Feather_ESP32_S3 feather;
TempSensor sensor1;
TempSensor sensor2;
TempSensor sensor3;
TempSensor sensor4;
TempSensor sensor5;
TempSensor sensor6;
TempSensor* sensors[] = { &sensor1, &sensor2, &sensor3, &sensor4, &sensor5, &sensor6 };
const int NUM_SENSORS = sizeof(sensors) / sizeof(sensors[0]);

#define NUM_SECS 5 
TimedAverager temp1(1000 * NUM_SECS);
TimedAverager temp2(1000 * NUM_SECS);
TimedAverager temp3(1000 * NUM_SECS);
TimedAverager temp4(1000 * NUM_SECS);
TimedAverager temp5(1000 * NUM_SECS);
TimedAverager temp6(1000 * NUM_SECS);
TimedAverager* temps[] = { &temp1, &temp2, &temp3, &temp4, &temp5, &temp6 };

Stopwatch trigger;

InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);

Point point1("Air");
Point point2("Air");
Point point3("Air");
Point point4("Air");
Point point5("Air");
Point point6("Air");
Point* points[] = { &point1,&point2,&point3,&point4,&point5,&point6 };

const Color MSG_COLOR = Color::WHITE;
const Color OK_COLOR = Color::YELLOW;
const Color FAILED_COLOR = Color::ORANGE;

// The setup() function runs once each time the micro-controller starts
void setup()
{
   Wire.begin();

   // start serial port
   Serial.begin(115200);

   // wait a few seconds for the serial monitor to open
   while (millis() < 2000 && !Serial)
   {
   };
   delay(500);

   feather.begin();
   feather.display.setRotation(0);

   client.setWriteOptions(WriteOptions().batchSize(NUM_SENSORS).bufferSize(2 * NUM_SENSORS));
   for (int i = 0; i < 6; i++)
   {
      Multiplexer::select(i);
      sensors[i]->begin();
      points[i]->addTag("location", (String("Calibration ") + (i + 1)).c_str());
   }
   trigger.reset();

   feather.echoToSerial = true;
   feather.clear();
   feather.println("Initializing", Color::CYAN);

   // Setup wifi
   WiFi.mode(WIFI_STA);
   wifiMulti.addAP(WIFI_SSID, WIFI_PASSWORD);

   feather.print("WiFi... ", MSG_COLOR);
   while (wifiMulti.run() != WL_CONNECTED)
   {
      Serial.print(".");
      delay(100);
   }
   feather.setCursorX(feather.display.width() - 2 * 8);
   feather.println("ok", OK_COLOR);

   // Accurate time is necessary for certificate validation and writing in batches
   // We use the NTP servers in your area as provided by: https://www.pool.ntp.org/zone/
   // Syncing progress and the time will be printed to Serial.
   feather.echoToSerial = false;
   feather.print("Syncing Time... ", MSG_COLOR);
   timeSync(TZ_INFO, "pool.ntp.org", "time.nis.gov");
   feather.setCursorX(feather.display.width() - 2 * 8);
   feather.println("ok", OK_COLOR);
   feather.echoToSerial = true;

   // Check server connection
   feather.print("InfluxDB... ", MSG_COLOR);

   if (client.validateConnection())
   {
      feather.setCursorX(feather.display.width() - 2 * 8);
      feather.println("ok", OK_COLOR);
      Serial.println(client.getServerUrl());
   }
   else
   {
      feather.setCursorX(feather.display.width() - 6 * 8);
      feather.println("FAILED", FAILED_COLOR);
      feather.display.setTextSize(1);
      feather.println(client.getLastErrorMessage(), FAILED_COLOR);
      while (1);
   }

   delay(5000);

   feather.clear();
   feather.echoToSerial = false;
   trigger.reset();

   pinMode(BUILTIN_LED, HIGH);
}

void loop()
{
   feather.setCursor(0, 0);
   feather.display.setTextSize(2);

   for (int i = 0; i < 6; i++)
   {
      Multiplexer::select(i);
      TempSensor* sensor = sensors[i];
      float temp = sensors[i]->readTemperatureF();
      temps[i]->set(temp);

      feather.print((i + 1), 2, Color::WHITE);

      if (sensor->exists())
      {
         feather.println(temp, " F", 8, Color::YELLOW);
         feather.moveCursorY(2);
         feather.print("  ");
         feather.println(sensor->type(), Color::GRAY);
      }
      else
      {
         feather.println("-----", Color::GRAY);
      }

      feather.moveCursorY(6);
   }

   if (trigger.elapsedSecs() > NUM_SECS)
   {
      trigger.reset();

      for (int i = 0; i < NUM_SENSORS; i++)
      {
         points[i]->clearFields();
         points[i]->addField("temperature", temps[i]->get());

         if (sensors[i]->exists())
         {
            if (!client.writePoint(*points[i]))
            {
               Serial.println("InfluxDB write failed: ");
               Serial.println(client.getLastErrorMessage());
            }
            Serial.println(points[i]->toLineProtocol());
         }
      }
      client.flushBuffer();
   }
}


