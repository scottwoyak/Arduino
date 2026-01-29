#include "Feather_ESP32_S3.h"
#include "Stopwatch.h"
#include "TempSensor.h"
#include "TimedAverager.h"
#include "RunningAverager.h"
#include "Multiplexer.h"
#include "Feather.h"

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
Feather feather;
TempSensor* sensors[] =
{
   new TempSensor(),
   new TempSensor(),
   new TempSensor(),
   new TempSensor(),
   new TempSensor(),
   new TempSensor(),
};
const int NUM_SENSORS = sizeof(sensors) / sizeof(sensors[0]);

constexpr auto NUM_SECS = 10;

TimedAverager* temps[] =
{
   new TimedAverager(1000 * NUM_SECS),
   new TimedAverager(1000 * NUM_SECS),
   new TimedAverager(1000 * NUM_SECS),
   new TimedAverager(1000 * NUM_SECS),
   new TimedAverager(1000 * NUM_SECS),
   new TimedAverager(1000 * NUM_SECS),
};

TimedAverager* hums[] =
{
   new TimedAverager(1000 * NUM_SECS),
   new TimedAverager(1000 * NUM_SECS),
   new TimedAverager(1000 * NUM_SECS),
   new TimedAverager(1000 * NUM_SECS),
   new TimedAverager(1000 * NUM_SECS),
   new TimedAverager(1000 * NUM_SECS),
};

constexpr auto AVERAGING_TIME = 10 * 60 * 1000;
TimedAverager* tavgs[] = {
   new TimedAverager(AVERAGING_TIME),
   new TimedAverager(AVERAGING_TIME),
   new TimedAverager(AVERAGING_TIME),
   new TimedAverager(AVERAGING_TIME),
   new TimedAverager(AVERAGING_TIME),
   new TimedAverager(AVERAGING_TIME),
};

TimedAverager* havgs[] = {
   new TimedAverager(AVERAGING_TIME),
   new TimedAverager(AVERAGING_TIME),
   new TimedAverager(AVERAGING_TIME),
   new TimedAverager(AVERAGING_TIME),
   new TimedAverager(AVERAGING_TIME),
   new TimedAverager(AVERAGING_TIME),
};

Format tempFormat("###.## F");
Format humFormat("###.#%");
Format correctionFormat("+#.##");


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
constexpr auto NUM_VIEWS = 4;

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

   // load saved correction factors
   feather.preferences.begin("Calibrator", false);
   float tcorrections[6];
   float hcorrections[6];
   for (int i = 0; i < NUM_SENSORS; i++)
   {
      tcorrections[i] = feather.preferences.getFloat((String("Temp ") + i).c_str());
      hcorrections[i] = feather.preferences.getFloat((String("Hum ") + i).c_str());
   }
   feather.preferences.end();

   // display saved temperature factors
   feather.display.setCursor(0, 0);
   feather.display.setTextSize(2);
   feather.display.fillRect(0, 0, feather.display.width(), 3 * feather.charH(), (uint16_t)Color::ORANGE);
   feather.moveCursor((feather.display.width() - 5 * feather.charW()) / 2, feather.charH() / 2);
   feather.println("Saved", Color::WHITE, Color::ORANGE);
   feather.moveCursorX((feather.display.width() - 9 * feather.charW()) / 2);
   feather.println("T Factors", Color::WHITE, Color::ORANGE);
   feather.moveCursorY(14);

   for (int i = 0; i < NUM_SENSORS; i++)
   {
      feather.display.setTextSize(2);
      feather.print((i + 1), Color::LABEL);

      feather.display.setTextSize(3);
      feather.println(tcorrections[i], correctionFormat, Color::VALUE);
      feather.moveCursorY(2);
   }

   while (feather.buttonA.wasPressed() == false)
   {
      delay(1);
   }
   feather.buttonA.reset();

   // display saved humidity factors
   feather.display.setCursor(0, 0);
   feather.display.setTextSize(2);
   feather.display.fillRect(0, 0, feather.display.width(), 3 * feather.charH(), (uint16_t)Color::ORANGE);
   feather.moveCursor((feather.display.width() - 5 * feather.charW()) / 2, feather.charH() / 2);
   feather.println("Saved", Color::WHITE, Color::ORANGE);
   feather.moveCursorX((feather.display.width() - 9 * feather.charW()) / 2);
   feather.println("H Factors", Color::WHITE, Color::ORANGE);
   feather.moveCursorY(14);

   for (int i = 0; i < NUM_SENSORS; i++)
   {
      feather.display.setTextSize(2);
      feather.print((i + 1), Color::WHITE);

      feather.display.setTextSize(3);
      feather.println(hcorrections[i], correctionFormat, Color::YELLOW);
      feather.moveCursorY(2);
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

   feather.preferences.begin("calibrator", false);

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
      if (view > NUM_VIEWS)
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
      break;

   case 1:
      feather.println("Humidity", Color::ORANGE);
      break;

   case 2:
      feather.println("T Factors", Color::ORANGE);
      break;

   case 3:
      feather.println("H Factors", Color::ORANGE);
      break;

   case 4:
      feather.println("Sensors", Color::ORANGE);
      break;
   }
   feather.moveCursorY(10);

   for (int i = 0; i < NUM_SENSORS; i++)
   {
      Multiplexer::select(i);
      TempSensor* sensor = sensors[i];
      float temp, hum;
      sensors[i]->readBoth(temp, hum);
      temps[i]->set(temp);
      hums[i]->set(hum);
      tavgs[i]->set(temp);
      havgs[i]->set(hum);

      // values
      feather.display.setTextSize(2);
      feather.print((i + 1), Color::WHITE);
      if (sensor->exists())
      {
         switch (view)
         {
         case 0: // temperature
            feather.moveCursorX(feather.charW() / 2);
            feather.display.setTextSize(3);
            feather.println(temp, 6, Color::YELLOW);
            feather.moveCursorY(8);
            break;

         case 1: // humidity
            feather.moveCursorX(feather.charW() / 2);
            feather.display.setTextSize(3);
            if (isnan(hum))
            {
               feather.println("----", 6, Color::YELLOW);
            }
            else
            {
               feather.println(hum, humFormat, Color::YELLOW);
            }
            feather.moveCursorY(8);
            break;

         case 2: // temp corrections
         {
            float correction = tavgs[i]->get() - tavgs[0]->get();

            feather.display.setTextSize(2);
            feather.print(correction, correctionFormat, Color::YELLOW);
            feather.println(temp, tempFormat, Color::CYAN);
            feather.moveCursorY(2);
            break;
         }

         case 3: // hum corrections
         {
            float correction = havgs[i]->get() - havgs[0]->get();

            feather.display.setTextSize(2);
            feather.print(correction, correctionFormat, Color::YELLOW);
            feather.println(temp, tempFormat, Color::CYAN);
            feather.moveCursorY(2);
            break;
         }

         case 4: // sensor info
            feather.moveCursorX(feather.charW() / 2);
            feather.display.setTextSize(2);
            feather.println(sensor->type(), Color::YELLOW);

            feather.moveCursorY(2);
            feather.print("  0x", Color::CYAN);
            feather.print(String(sensor->address(), HEX), Color::CYAN);
            feather.print(" ");
            feather.println(sensor->id(), Color::WHITE);
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
   if (prefsTrigger.elapsedSecs() > 60)
   {
      prefsTrigger.reset();

      feather.preferences.begin("Calibrator", false);
      for (int i = 0; i < NUM_SENSORS; i++)
      {
         float tcorrection = tavgs[i]->get() - tavgs[0]->get();
         float hcorrection = havgs[i]->get() - havgs[0]->get();
         feather.preferences.putFloat((String("Temp ") + i).c_str(), tcorrection);
         feather.preferences.putFloat((String("Hum ") + i).c_str(), hcorrection);
      }
      feather.preferences.end();
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
         points[i]->addField("humidity", hums[i]->get(), 2);

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


