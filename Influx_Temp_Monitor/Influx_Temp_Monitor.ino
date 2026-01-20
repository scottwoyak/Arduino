#include "Feather_ESP32_S3.h"
#include "Potentiometer.h"
#include "TempSensor.h"
#include "RunningAverager.h"

#include <WiFiMulti.h>
WiFiMulti wifiMulti;
#define DEVICE "ESP32"

#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>

#include <string>

// for WIFI_SSID and WIFI_PASSWORD
#include "WiFiSettings.h"

// Time zone info
#define TZ_INFO "UTC-5"

const char* version = "v0.9";

std::string rooms[] =
{
   "Barn 1st Floor",
   "Barn 2nd Floor",
   "Bathroom",
   "Bedroom",
   "Chicken Coup",
   "Clay Studio",
   "Den",
   "Kitchen",
   "Living Room",
   "Outside",
   "Studio",
   "Sunroom",
};
std::string postfixes[] =
{
   "",
   " 1",
   " 2",
   " 3",
   " 4",
   " 5",
   " 6",
   " 7",
   " 8",
   " 9",
   " 10",
};

Feather_ESP32_S3 feather;
IntPotentiometer roomP(A0, 0, (sizeof(rooms) / sizeof(rooms[0]) - 1));
IntPotentiometer postfixP(A1, 0, (sizeof(postfixes) / sizeof(postfixes[0]) - 1));
FloatPotentiometer calibrationP(A2, -1.0, 1.0, 201);
std::string location;
Stopwatch trigger;
RunningAverager temperature(50);
RunningAverager humidity(50);

ITempSensor* sensor;
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);

// Data point
Point point("Temperature");

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

   const Color565 MSG_COLOR = Color565::WHITE;
   const Color565 OK_COLOR = Color565::YELLOW;
   const Color565 FAILED_COLOR = Color565::ORANGE;
   const uint8_t CHAR_WIDTH = 12;

   feather.display.setTextWrap(false);

   // ask for information
   bool ready = false;
   while (ready == false)
   {
      if (digitalRead(0) == LOW)
      {
         ready = true;
      }

      feather.display.setTextSize(2);
      feather.display.setCursor(0, 0);
      feather.println("Sensor Information...\n");

      feather.println("Location:", Color565::WHITE);
      feather.setCursorX(20);
      std::string str = rooms[roomP.read()] + postfixes[postfixP.read()];
      feather.println(str, 20, Color565::YELLOW);

      feather.println();

      feather.println("Calibration:", Color565::WHITE);
      feather.setCursorX(20);
      feather.println(calibrationP.read(), 5, 2, Color565::YELLOW);
   }
   feather.clear();
   location = rooms[roomP.read()] + postfixes[postfixP.read()];

   // while starting, echo the display to serial
   feather.echoToSerial = true;

   feather.println("Initializing", Color565::CYAN);

   // Setup wifi
   WiFi.mode(WIFI_STA);
   wifiMulti.addAP(WIFI_SSID, WIFI_PASSWORD);

   feather.print("WiFi... ", MSG_COLOR);
   while (wifiMulti.run() != WL_CONNECTED)
   {
      Serial.print(".");
      delay(100);
   }
   feather.setCursorX(feather.display.width() - 2 * CHAR_WIDTH);
   feather.println("ok", OK_COLOR);

   // Accurate time is necessary for certificate validation and writing in batches
   // We use the NTP servers in your area as provided by: https://www.pool.ntp.org/zone/
   // Syncing progress and the time will be printed to Serial.
   feather.print("Syncing Time... ", MSG_COLOR);
   timeSync(TZ_INFO, "pool.ntp.org", "time.nis.gov");
   feather.setCursorX(feather.display.width() - 2 * CHAR_WIDTH);
   feather.println("ok", OK_COLOR);

   // Check server connection
   feather.print("InfluxDB... ", MSG_COLOR);

   if (client.validateConnection())
   {
      feather.setCursorX(feather.display.width() - 2 * CHAR_WIDTH);
      feather.println("ok", OK_COLOR);
      Serial.println(client.getServerUrl());
   }
   else
   {
      feather.setCursorX(feather.display.width() - 6 * CHAR_WIDTH);
      feather.println("FAILED", FAILED_COLOR);
      feather.display.setTextSize(1);
      feather.println(client.getLastErrorMessage(), FAILED_COLOR);
      while (1);
   }

   feather.print("Sensor... ", MSG_COLOR);
   sensor = TempSensorFactory::create(false);
   if (sensor->exists())
   {
      feather.setCursorX(feather.display.width() - 2 * CHAR_WIDTH);
      feather.println("ok", OK_COLOR);

      Serial.println(sensor->info());
   }
   else
   {
      feather.setCursorX(feather.display.width() - 6 * CHAR_WIDTH);
      feather.print("FAILED", FAILED_COLOR);
      while (1);
   }

   feather.print("Sensor.begin() ", MSG_COLOR);
   if (sensor->begin())
   {
      feather.setCursorX(feather.display.width() - 2 * CHAR_WIDTH);
      feather.println("ok", OK_COLOR);
   }
   else
   {
      feather.setCursorX(feather.display.width() - 6 * CHAR_WIDTH);
      feather.println("FAILED", FAILED_COLOR);
      while (1);
   }

   delay(5000);


   point.addTag("location", location.c_str());
   feather.clear();
   feather.echoToSerial = false;
}

#define MAX_CHARS 8
#define CHAR_H 8
#define CHAR_W 6

// Add the main program code into the continuous loop() function
void loop()
{
   feather.display.setTextSize(2);

   // Clear fields for reusing the point. Tags will remain the same as set above.
   point.clearFields();

   // Store measured value into point
   temperature.set(sensor->readTemperatureF());
   humidity.set(sensor->readHumidity());

   // Check WiFi connection and reconnect if needed
   if (wifiMulti.run() != WL_CONNECTED)
   {
      feather.display.println("Wifi connection lost");
   }

   uint8_t x = feather.display.width() - (MAX_CHARS * 8 * CHAR_W) / 2;
   feather.display.setCursor(x, (feather.display.height() - 2*4*CHAR_H-CHAR_H)/2);

   feather.display.setTextSize(4);
   feather.println(temperature.get(), " F", 8);

   feather.setCursor(x, feather.display.getCursorY()+CHAR_H);
   feather.println(humidity.get(), "%", 7);

   // Write point
   if (trigger.elapsedSecs() > 5)
   {
      trigger.reset();

      point.addField("temperature", temperature.get());
      point.addField("humidity", humidity.get());
      // Print what are we exactly writing
      //Serial.println(point.toLineProtocol());

      digitalWrite(BUILTIN_LED, HIGH);
      if (!client.writePoint(point))
      {
         Serial.println("InfluxDB write failed: ");
         Serial.println(client.getLastErrorMessage());
      }
      digitalWrite(BUILTIN_LED, LOW);
   }

   feather.display.setTextSize(2);
   feather.setCursor(0, feather.display.height() - 16);
   feather.print(location.c_str(), Color565::GRAY);

   feather.setCursorX(feather.display.width() - 12 * strlen(version));
   feather.print(version, Color565::GRAY);
}


