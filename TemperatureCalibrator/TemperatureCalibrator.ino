#include "Feather_ESP32_S3.h"
#include "Stopwatch.h"
#include "TempSensor.h"
#include "TimedAverager.h"
#include "RunningAverager.h"
#include "Multiplexer.h"
#include <Preferences.h>

#include <WiFiMulti.h>
WiFiMulti wifiMulti;
#define DEVICE "ESP32"

#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>

// for WIFI_SSID and WIFI_PASSWORD
#include "WiFiSettings.h"

// Time zone info
constexpr auto TZ_INFO = "UTC-5";

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

constexpr auto NUM_SECS = 5;
TimedAverager temp1(1000 * NUM_SECS);
TimedAverager temp2(1000 * NUM_SECS);
TimedAverager temp3(1000 * NUM_SECS);
TimedAverager temp4(1000 * NUM_SECS);
TimedAverager temp5(1000 * NUM_SECS);
TimedAverager temp6(1000 * NUM_SECS);
TimedAverager* temps[] = { &temp1, &temp2, &temp3, &temp4, &temp5, &temp6 };

TimedAverager hum1(1000 * NUM_SECS);
TimedAverager hum2(1000 * NUM_SECS);
TimedAverager hum3(1000 * NUM_SECS);
TimedAverager hum4(1000 * NUM_SECS);
TimedAverager hum5(1000 * NUM_SECS);
TimedAverager hum6(1000 * NUM_SECS);
TimedAverager* hums[] = { &hum1, &hum2, &hum3, &hum4, &hum5, &hum6 };

constexpr auto T10_AVERAGING_TIME = 10 * 60 * 1000;
TimedAverager t10avg1(T10_AVERAGING_TIME);
TimedAverager t10avg2(T10_AVERAGING_TIME);
TimedAverager t10avg3(T10_AVERAGING_TIME);
TimedAverager t10avg4(T10_AVERAGING_TIME);
TimedAverager t10avg5(T10_AVERAGING_TIME);
TimedAverager t10avg6(T10_AVERAGING_TIME);
TimedAverager* t10avgs[] = { &t10avg1, &t10avg2, &t10avg3, &t10avg4, &t10avg5, &t10avg6 };

Preferences prefs;
Stopwatch influxTrigger;
Stopwatch prefsTrigger;

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

uint8_t view = 0;
constexpr auto MAX_VIEWS = 3;

void printCorrectionValue(float correction, Color color)
{
   if (fabs(correction) < 0.01)
   {
      feather.print(" ");
      correction = 0; // avoid the -0.00 case
   }
   else if (correction > 0)
   {
      feather.print("+", color);
   }
   feather.println(correction, 5, color);
}


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
   feather.display.setTextWrap(false);

   prefs.begin("Calibrator", false);
   float corrections[6];
   for (int i = 0; i < NUM_SENSORS; i++)
   {
      corrections[i] = prefs.getFloat((String("Sensor ") + i).c_str());
   }
   prefs.end();
   feather.display.setTextSize(2);
   feather.println("Saved", Color::WHITE);
   feather.println("Results", Color::WHITE);
   feather.moveCursorY(10);

   for (int i = 0; i < NUM_SENSORS; i++)
   {
      feather.display.setTextSize(2);
      feather.print((i + 1), 2, Color::WHITE);

      feather.display.setTextSize(3);
      printCorrectionValue(corrections[i], Color::YELLOW);
      feather.moveCursorY(8);
   }

   while (feather.buttonA.wasPressed() == false)
   {
      delay(1);
   }
   feather.buttonA.reset();

   client.setWriteOptions(WriteOptions().batchSize(NUM_SENSORS).bufferSize(2 * NUM_SENSORS));
   for (int i = 0; i < NUM_SENSORS; i++)
   {
      Multiplexer::select(i);
      sensors[i]->begin();
      points[i]->addTag("location", (String("Calibration ") + (i + 1)).c_str());
   }

   feather.echoToSerial = true;
   feather.clear();
   feather.display.setTextSize(3);
   feather.println("Init", Color::CYAN);
   feather.moveCursorY(10);

   feather.display.setTextSize(2);

   // Setup wifi
   WiFi.mode(WIFI_STA);
   wifiMulti.addAP(WIFI_SSID, WIFI_PASSWORD);

   feather.print("WiFi", MSG_COLOR);
   while (wifiMulti.run() != WL_CONNECTED)
   {
      Serial.print(".");
      delay(100);
   }
   feather.setCursorX(-2 * 2 * 8);
   feather.println("ok", OK_COLOR);

   // Accurate time is necessary for certificate validation and writing in batches
   // We use the NTP servers in your area as provided by: https://www.pool.ntp.org/zone/
   // Syncing progress and the time will be printed to Serial.
   feather.echoToSerial = false;
   feather.print("Time", MSG_COLOR);
   timeSync(TZ_INFO, "pool.ntp.org", "time.nis.gov");
   feather.setCursorX(-2 * 2 * 8);
   feather.println("ok", OK_COLOR);
   feather.echoToSerial = true;

   // Check server connection
   feather.print("Influx", MSG_COLOR);

   if (client.validateConnection())
   {
      feather.setCursorX(-2 * 2 * 8);
      feather.println("ok", OK_COLOR);
      Serial.println(client.getServerUrl());
   }
   else
   {
      feather.setCursorX(-6 * 2 * 8);
      feather.println("FAILED", FAILED_COLOR);
      feather.display.setTextSize(1);
      feather.println(client.getLastErrorMessage(), FAILED_COLOR);
      while (1);
   }

   delay(1000);

   feather.clear();
   feather.echoToSerial = false;

   prefsTrigger.reset();
   influxTrigger.reset();

   prefs.begin("calibrator", false);

   pinMode(BUILTIN_LED, OUTPUT);
   digitalWrite(BUILTIN_LED, LOW);
}

long count;
void loop()
{
   feather.setCursor(0, 0);
   feather.display.setTextSize(2);

   if (feather.buttonA.wasPressed())
   {
      feather.clear();
      feather.buttonA.reset();
      view++;
      if (view > MAX_VIEWS)
      {
         view = 0;
      }
   }

   count++;

   // ------------------------------------------- display

   // print headers
   feather.display.setTextSize(2);
   switch (view)
   {
   case 0:
      feather.println("Temperature", Color::ORANGE);
      feather.moveCursorY(10);
      break;

   case 1:
      feather.println("Humidity", Color::ORANGE);
      feather.moveCursorY(10);
      break;

   case 2:
      feather.println("Corrections", Color::ORANGE);
      feather.moveCursorY(10);
      break;

   case 3:
      feather.println("Sensors", Color::ORANGE);
      feather.moveCursorY(10);
      break;
   }

   for (int i = 0; i < NUM_SENSORS; i++)
   {
      Multiplexer::select(i);
      TempSensor* sensor = sensors[i];
      float temp, hum;
      sensors[i]->readBoth(temp, hum);
      temps[i]->set(temp);
      hums[i]->set(temp);
      t10avgs[i]->set(temp);

      // values
      feather.display.setTextSize(2);
      feather.print((i + 1), 2, Color::WHITE);
      if (sensor->exists())
      {
         switch (view)
         {
         case 0: // temperature
            feather.display.setTextSize(3);
            feather.println(temp, 6, Color::YELLOW);
            feather.moveCursorY(8);
            break;

         case 1: // humidity
            feather.display.setTextSize(3);
            feather.println(hum, "%", 6, 1, Color::YELLOW);
            feather.moveCursorY(8);
            break;

         case 2: // temp corrections
         {
            float correction = t10avgs[i]->get() - t10avgs[0]->get();

            feather.display.setTextSize(2);
            printCorrectionValue(correction, Color::YELLOW);
            feather.moveCursorY(2);

            feather.moveCursorX(3 * 2 * 6);
            feather.println(temp, " F", 8, 2, Color::CYAN);
            break;
         }

         case 3: // sensor info
            feather.display.setTextSize(2);
            feather.println(sensor->type(), Color::YELLOW);

            feather.display.setTextSize(2);
            feather.moveCursorY(2);
            feather.print("  0x");
            feather.println(String(sensor->address(), HEX), 2, Color::CYAN);
            break;
         }
      }
      else
      {
         feather.println("-----", Color::ORANGE);
      }

      feather.moveCursorY(6);
   }

   // ------------------------------------------- save to flash
   if (prefsTrigger.elapsedSecs() > 10 * 60)
   {
      prefsTrigger.reset();

      prefs.begin("Calibrator", false);
      for (int i = 0; i < NUM_SENSORS; i++)
      {
         float correction = t10avgs[i]->get() - t10avgs[0]->get();
         prefs.putFloat((String("Sensor ") + i).c_str(), correction);
      }
      prefs.end();
   }

   // ------------------------------------------- send to INFLUX
   if (influxTrigger.elapsedSecs() > NUM_SECS)
   {
      digitalWrite(BUILTIN_LED, HIGH);

      Serial.print(count);
      Serial.println(" values collected");
      count = 0;

      influxTrigger.reset();

      for (int i = 0; i < NUM_SENSORS; i++)
      {
         points[i]->clearFields();
         points[i]->addField("temperature", temps[i]->get(), 4);
         points[i]->addField("hum", hums[i]->get(), 2);

         if (sensors[i]->exists())
         {
            if (!client.writePoint(*points[i]))
            {
               Serial.println("InfluxDB write failed: ");
               Serial.println(client.getLastErrorMessage());
            }
            //Serial.println(points[i]->toLineProtocol());
         }
      }
      client.flushBuffer();

      digitalWrite(BUILTIN_LED, LOW);
   }
}


