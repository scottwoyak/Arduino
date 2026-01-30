#include "Feather_ESP32_S3.h"
#include "Stopwatch.h"
#include "TempSensor.h"
#include "TimedAverager.h"
#include "RunningAverager.h"
#include "Multiplexer.h"
#include "Feather.h"
#include "SerialX.h"

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

Format tempFormat("###.##F");
Format temp2Format("###.#");
Format humFormat("###.#%");
Format hum2Format("###.#");
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

uint8_t view = 0;
constexpr auto NUM_VIEWS = 6;

void displaySavedInfo()
{
   // load saved correction factors
   feather.preferences.begin("Calibrator", false);
   float tcorrections[6];
   float hcorrections[6];
   std::string ids[6];

   for (int i = 0; i < NUM_SENSORS; i++)
   {
      tcorrections[i] = feather.preferences.getFloat((String("Temp ") + i).c_str());
      hcorrections[i] = feather.preferences.getFloat((String("Hum ") + i).c_str());
      ids[i] = feather.preferences.getString((String("ID ") + i).c_str()).c_str();
   }
   feather.preferences.end();

   // print saved factors for easy paste into TempSensor.cpp
   Serial.println("Copy this data to TempSensor.cpp");
   for (int i = 0; i < NUM_SENSORS; i++)
   {
      Serial.print("\"");
      Serial.print(ids[i].c_str());
      Serial.print("\", ");
      Serial.print(-tcorrections[i], 3);
      Serial.print(", ");
      Serial.print(-hcorrections[i], 3);
      Serial.print(", ");
      Serial.println();
   }

   // display saved temperature factors
   feather.display.setCursor(0, 0);
   feather.display.setTextSize(2);
   feather.display.fillRect(0, 0, feather.display.width(), 3 * feather.charH(), (uint16_t)Color::ORANGE);
   feather.moveCursorY(feather.charH() / 2);
   feather.printlnC("Temp", Color::WHITE, Color::ORANGE);
   feather.printlnC("Deltas", Color::WHITE, Color::ORANGE);
   feather.moveCursorY(14);

   for (int i = 0; i < NUM_SENSORS; i++)
   {
      feather.display.setTextSize(2);
      feather.print((i + 1), Color::LABEL);
      feather.moveCursorX(10);

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
   feather.moveCursorY(feather.charH() / 2);
   feather.printlnC("Humidity", Color::WHITE, Color::ORANGE);
   feather.printlnC("Deltas", Color::WHITE, Color::ORANGE);
   feather.moveCursorY(14);

   for (int i = 0; i < NUM_SENSORS; i++)
   {
      feather.display.setTextSize(2);
      feather.print((i + 1), Color::LABEL);
      feather.moveCursorX(10);

      feather.display.setTextSize(3);
      feather.println(hcorrections[i], correctionFormat, Color::VALUE);
      feather.moveCursorY(2);
   }

   while (feather.buttonA.wasPressed() == false)
   {
      delay(1);
   }
   feather.buttonA.reset();
}

void setup()
{
   Wire.begin();
   SerialX::begin();

   feather.begin();
   feather.display.setRotation(0);

   displaySavedInfo();

   client.setWriteOptions(WriteOptions().batchSize(NUM_SENSORS).bufferSize(2 * NUM_SENSORS));
   feather.echoToSerial = true;
   feather.clear();
   feather.display.setTextSize(3);
   feather.println("Init", Color::HEADING);
   feather.moveCursorY(10);

   feather.display.setTextSize(2);

   feather.preferences.begin("Calibrator", false);
   for (int i = 0; i < NUM_SENSORS; i++)
   {
      Multiplexer::select(i);
      sensors[i]->begin();

      // clear out corrections
      sensors[i]->tempCorrectionF = 0;
      sensors[i]->humCorrection = 0;

      points[i]->addTag("location", (String("Calibration ") + (i + 1)).c_str());
      feather.preferences.putString((String("ID ") + i).c_str(), sensors[i]->id());
   }
   feather.preferences.end();

   // Setup wifi
   WiFi.mode(WIFI_STA);
   wifiMulti.addAP(WIFI_SSID, WIFI_PASSWORD);

   feather.print("WiFi", Color::LABEL);
   while (wifiMulti.run() != WL_CONNECTED)
   {
      Serial.print(".");
      delay(100);
   }
   feather.setCursorX(-2 * 2 * 8);
   feather.println("ok", Color::VALUE);

   // Accurate time is necessary for certificate validation and writing in batches
   // We use the NTP servers in your area as provided by: https://www.pool.ntp.org/zone/
   // Syncing progress and the time will be printed to Serial.
   feather.echoToSerial = false;
   feather.print("Time", Color::LABEL);
   timeSync(TZ_INFO, "pool.ntp.org", "time.nis.gov");
   feather.setCursorX(-2 * 2 * 8);
   feather.println("ok", Color::VALUE);
   feather.echoToSerial = true;

   // Check server connection
   feather.print("Influx", Color::LABEL);

   if (client.validateConnection())
   {
      feather.setCursorX(-2 * 2 * 8);
      feather.println("ok", Color::VALUE);
      Serial.println(client.getServerUrl());
   }
   else
   {
      feather.setCursorX(-6 * 2 * 8);
      feather.println("FAILED", Color::RED);
      feather.display.setTextSize(1);
      feather.println(client.getLastErrorMessage(), Color::RED);
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
      view = view % NUM_VIEWS;
   }

   count++;

   // ------------------------------------------- display

   // print headers
   feather.display.setTextSize(2);
   switch (view)
   {
   case 0:
      feather.println("Temperature", Color::HEADING);
      break;

   case 1:
      feather.println("Humidity", Color::HEADING);
      break;

   case 2:
      feather.println("Temp Deltas", Color::HEADING);
      break;

   case 3:
      feather.println("Hum Deltas", Color::HEADING);
      break;

   case 4:
      feather.println("Sensors 1/2", Color::HEADING);
      break;

   case 5:
      feather.println("Sensors 2/2", Color::HEADING);
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

      if (sensor->exists())
      {
         switch (view)
         {
         case 0: // temperature
            feather.print((i + 1), Color::LABEL);
            feather.moveCursorX(feather.charW() / 2);
            feather.display.setTextSize(3);
            feather.println(temp, tempFormat, Color::VALUE);
            feather.moveCursorY(8);
            break;

         case 1: // humidity
            feather.print((i + 1), Color::LABEL);
            feather.moveCursorX(feather.charW() / 2);
            feather.display.setTextSize(3);
            if (isnan(hum))
            {
               feather.println("----", Color::VALUE);
            }
            else
            {
               feather.println(hum, humFormat, Color::VALUE);
            }
            feather.moveCursorY(8);
            break;

         case 2: // temp deltas
         {
            feather.print((i + 1), Color::LABEL);

            float correction = tavgs[i]->get() - tavgs[0]->get();
            feather.moveCursorX(feather.charW() / 2);
            feather.display.setTextSize(2);
            feather.print(correction, correctionFormat, Color::VALUE);
            feather.moveCursorX(feather.charW() / 2);
            feather.println(temp, temp2Format, Color::VALUE2);
            feather.moveCursorY(2);
            break;
         }

         case 3: // hum deltas
         {
            feather.print((i + 1), Color::LABEL);

            float correction = havgs[i]->get() - havgs[0]->get();
            feather.display.setTextSize(2);
            feather.moveCursorX(feather.charW() / 2);
            feather.print(correction, correctionFormat, Color::VALUE);
            feather.moveCursorX(feather.charW() / 2);
            feather.println(hum, hum2Format, Color::VALUE2);
            feather.moveCursorY(2);
            break;
         }

         case 4: // sensor info
         case 5:

            if (i < 3 && view != 4)
            {
               continue;
            }

            if (i >= 3 && view != 5)
            {
               continue;
            }

            feather.print((i + 1), Color::LABEL);
            feather.moveCursorX(feather.charW() / 2);
            feather.display.setTextSize(2);
            int16_t x = feather.display.getCursorX();

            feather.println(sensor->type(), Color::VALUE);
            feather.moveCursor(x, 3);
            feather.print("0x", Color::VALUE2);
            feather.println(sensor->address(), HEX, Color::VALUE2);
            feather.moveCursor(x, 3);
            feather.println(sensor->id(), Color::GREEN);
            break;
         }
      }
      else
      {
         feather.println("-----", Color::GRAY);
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
         float tDelta = tavgs[i]->get() - tavgs[0]->get();
         float hDelta = havgs[i]->get() - havgs[0]->get();
         points[i]->clearFields();
         points[i]->addField("temperature", temps[i]->get(), 3);
         points[i]->addField("humidity", hums[i]->get(), 2);
         points[i]->addField("tDelta", tDelta, 3);
         points[i]->addField("hDelta", hDelta, 2);
         points[i]->addField("tCorrected", temps[i]->get() - tDelta, 3);
         points[i]->addField("hCorrected", hums[i]->get() - hDelta, 2);

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


