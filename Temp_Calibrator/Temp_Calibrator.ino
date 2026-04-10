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

// The amount of time we use to get an average "current" reading. This
// is used to report the current temperature to Influx
constexpr auto CURRENT_AVG_S = 20;

// The amount of time we use to get a reading used for computing correction factors
constexpr auto CORRECTION_AVG_S = 10 * 60;

// How often we send data to Influx
constexpr auto INFLUX_INTERVAL_S = 10;

TimedAverager* temps[] =
{
   new TimedAverager(1000 * CURRENT_AVG_S),
   new TimedAverager(1000 * CURRENT_AVG_S),
   new TimedAverager(1000 * CURRENT_AVG_S),
   new TimedAverager(1000 * CURRENT_AVG_S),
   new TimedAverager(1000 * CURRENT_AVG_S),
   new TimedAverager(1000 * CURRENT_AVG_S),
};

TimedAverager* hums[] =
{
   new TimedAverager(1000 * CURRENT_AVG_S),
   new TimedAverager(1000 * CURRENT_AVG_S),
   new TimedAverager(1000 * CURRENT_AVG_S),
   new TimedAverager(1000 * CURRENT_AVG_S),
   new TimedAverager(1000 * CURRENT_AVG_S),
   new TimedAverager(1000 * CURRENT_AVG_S),
};

TimedAverager* tavgs[] = {
   new TimedAverager(1000*CORRECTION_AVG_S),
   new TimedAverager(1000 * CORRECTION_AVG_S),
   new TimedAverager(1000 * CORRECTION_AVG_S),
   new TimedAverager(1000 * CORRECTION_AVG_S),
   new TimedAverager(1000 * CORRECTION_AVG_S),
   new TimedAverager(1000 * CORRECTION_AVG_S),
};

TimedAverager* havgs[] = {
   new TimedAverager(1000 * CORRECTION_AVG_S),
   new TimedAverager(1000 * CORRECTION_AVG_S),
   new TimedAverager(1000 * CORRECTION_AVG_S),
   new TimedAverager(1000 * CORRECTION_AVG_S),
   new TimedAverager(1000 * CORRECTION_AVG_S),
   new TimedAverager(1000 * CORRECTION_AVG_S),
};

Format tempFormat("###.## F");
Format temp2Format("###.##");
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

uint8_t view = 0;
constexpr auto NUM_VIEWS = 4;

void printCalibrationCodeToSerial()
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
   Serial.println("Copy this data to libraries\\Woyak\\TempSensorCallibration.h");
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
}

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

   printCalibrationCodeToSerial();

   // display saved temperature factors
   feather.display.setCursor(0, 0);
   feather.setTextSize(3);
   feather.display.fillRect(0, 0, feather.display.width(), 2.5 * feather.charH(), (uint16_t)Color::ORANGE);
   feather.moveCursorY((0.5) * feather.charH() / 2);
   feather.printlnC("Temp", Color::WHITE, Color::ORANGE);
   feather.printlnC("Deltas", Color::WHITE, Color::ORANGE);
   feather.moveCursorY(feather.charH() / 2);

   for (int i = 0; i < NUM_SENSORS; i++)
   {
      feather.print((i + 1), Color::LABEL);
      feather.moveCursorX(10);

      if (isnan(tcorrections[i]))
      {
         feather.println("-----", Color::GRAY);
      }
      else
      {
         feather.println(tcorrections[i], correctionFormat, Color::VALUE);
      }
      feather.moveCursorY(feather.charH() / 4);
   }

   Stopwatch sw;
   while (feather.buttonA.wasPressed() == false && sw.elapsedSecs() < 60)
   {
      delay(1);
   }

   // print again in case the user didn't have serial open the first time
   printCalibrationCodeToSerial();

   // display saved humidity factors
   feather.display.setCursor(0, 0);
   feather.display.fillRect(0, 0, feather.display.width(), 2.5 * feather.charH(), (uint16_t)Color::ORANGE);
   feather.moveCursorY((0.5) * feather.charH() / 2);
   feather.printlnC("Humidity", Color::WHITE, Color::ORANGE);
   feather.printlnC("Deltas", Color::WHITE, Color::ORANGE);
   feather.moveCursorY(feather.charH() / 2);

   feather.setTextSize(3);
   for (int i = 0; i < NUM_SENSORS; i++)
   {
      feather.print((i + 1), Color::LABEL);
      feather.moveCursorX(10);

      if (isnan(hcorrections[i]))
      {
         feather.println("-----", Color::GRAY);
      }
      else
      {
         feather.println(hcorrections[i], correctionFormat, Color::VALUE);
      }
      feather.moveCursorY(feather.charH() / 4);
   }

   while (feather.buttonA.wasPressed() == false && sw.elapsedSecs() < 60)
   {
      delay(1);
   }
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
   feather.clearDisplay();
   feather.setTextSize(3);
   feather.println("Init", Color::HEADING);
   feather.moveCursorY(10);

   feather.setTextSize(3);

   feather.preferences.begin("Calibrator", false);
   for (int i = 0; i < NUM_SENSORS; i++)
   {
      Multiplexer::select(i);
      sensors[i]->begin();

      // clear out corrections
      sensors[i]->setTempCorrectionF(0);
      sensors[i]->setHumidityCorrection(0);

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
      feather.setTextSize(1);
      feather.println(client.getLastErrorMessage(), Color::RED);
      while (1);
   }

   delay(1000);

   feather.clearDisplay();
   feather.echoToSerial = false;

   prefsTrigger.reset();
   influxTrigger.reset();

   feather.preferences.begin("calibrator", false);

   pinMode(BUILTIN_LED, OUTPUT);
   digitalWrite(BUILTIN_LED, LOW);
}

// This is the baseline value used to calibrate the other sensors. It can
// either be one of the sensors, or the average of all the sensors.
float getBaseline(TimedAverager* values[])
{
   float sum = 0;
   int count = 0;
   for (int i = 0; i < NUM_SENSORS; i++)
   {
      if (isnan(values[i]->get()) == false)
      {
         sum += values[i]->get();
         count++;
      }
   }
   if (count > 0)
   {
      return sum / count;
   }
   else
   {
      return NAN;
   }
}

long count;
void loop()
{
   feather.setCursor(0, 0);

   if (feather.buttonA.wasPressed())
   {
      feather.clearDisplay();
      view++;
      view = view % NUM_VIEWS;
   }

   count++;

   // ------------------------------------------- display

   // print headers
   feather.setTextSize(3);
   switch (view)
   {
   case 0:
   case 1:
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
      feather.setTextSize(3);

      switch (view)
      {
      case 0: // temperature
      {
         feather.print((i + 1), Color::LABEL);
         feather.moveCursorX(feather.charW() / 2);
         int x = feather.display.getCursorX();
         if (sensor->exists())
         {
            feather.print(temp, tempFormat, Color::VALUE);
            float correction = tavgs[i]->get() - getBaseline(tavgs);
            feather.setCursor(x, feather.getCursor().y + feather.charH() - 3);
            feather.setTextSize(2);
            feather.print(temp - correction, temp2Format, Color::GRAY);
            feather.printR(correction, correctionFormat, Color::GRAY);
         }
         else
         {
            feather.println("-----", Color::GRAY);
            feather.setTextSize(2);
         }
         feather.println();
         feather.moveCursorY(2);
      }
      break;

      case 1: // humidity
      {
         feather.print((i + 1), Color::LABEL);
         int x = feather.display.getCursorX();
         feather.moveCursorX(feather.charW() / 2);
         if (sensor->exists())
         {
            feather.print(hum, humFormat, Color::VALUE);
            float correction = havgs[i]->get() - getBaseline(havgs);
            feather.setCursor(x, feather.getCursor().y + feather.charH() - 3);
            feather.setTextSize(2);
            feather.print(hum - correction, humFormat, Color::GRAY);
            feather.printR(correction, correctionFormat, Color::GRAY);
         }
         else
         {
            feather.println("-----", Color::GRAY);
            feather.setTextSize(2);
         }
         feather.println();
         feather.moveCursorY(2);
      }
      break;

      case 2: // sensor info
      case 3:

         if (i < 3 && view != 2)
         {
            continue;
         }

         if (i >= 3 && view != 3)
         {
            continue;
         }

         feather.print((i + 1), Color::LABEL);
         feather.moveCursorX(feather.charW() / 2);
         feather.setTextSize(2);
         int16_t x = feather.display.getCursorX();

         feather.println(sensor->type(), Color::VALUE);
         feather.moveCursor(x, 3);
         feather.print("0x", Color::VALUE2);
         feather.println(sensor->address(), HEX, Color::VALUE2);
         feather.moveCursor(x, 3);
         feather.println(sensor->id(), Color::GREEN);
         feather.println();
         break;
      }
   }


   // ------------------------------------------- save to flash
   if (prefsTrigger.elapsedSecs() > 60)
   {
      prefsTrigger.reset();

      feather.preferences.begin("Calibrator", false);
      float baselineT = getBaseline(tavgs);
      float baselineH = getBaseline(havgs);
      for (int i = 0; i < NUM_SENSORS; i++)
      {
         float tcorrection = tavgs[i]->get() - baselineT;
         float hcorrection = havgs[i]->get() - baselineH;
         feather.preferences.putFloat((String("Temp ") + i).c_str(), tcorrection);
         feather.preferences.putFloat((String("Hum ") + i).c_str(), hcorrection);
      }
      feather.preferences.end();

      printCalibrationCodeToSerial();
   }

   // ------------------------------------------- send to INFLUX
   if (influxTrigger.elapsedSecs() > INFLUX_INTERVAL_S)
   {
      digitalWrite(BUILTIN_LED, HIGH);

      Serial.print(count);
      Serial.println(" values collected");
      count = 0;

      influxTrigger.reset();

      for (int i = 0; i < NUM_SENSORS; i++)
      {
         float tDelta = tavgs[i]->get() - getBaseline(tavgs);
         float hDelta = havgs[i]->get() - getBaseline(havgs);
         float tCorrected = temps[i]->get() - tDelta;
         float hCorrected = hums[i]->get() - hDelta;
         float tCorrectedDelta = tCorrected - getBaseline(temps);
         float hCorrectedDelta = hCorrected - getBaseline(hums);
         points[i]->clearFields();
         // the current readings
         points[i]->addField("temperature", temps[i]->get(), 3);
         points[i]->addField("humidity", hums[i]->get(), 2);

         // the deltas from the baseline
         points[i]->addField("tDelta", tDelta, 4);
         points[i]->addField("hDelta", hDelta, 3);

         // the current readings using the correction factors. Once these
         // stop changing, the correction factors have converged
         points[i]->addField("tCorrected", tCorrected, 3);
         points[i]->addField("hCorrected", hCorrected, 2);

         // the deltas of the corrected readings. The closer these are to
         // zero, the less variation that exists between sensors
         points[i]->addField("tCorrectedDelta", tCorrectedDelta, 4);
         points[i]->addField("hCorrectedDelta", hCorrectedDelta, 3);

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


