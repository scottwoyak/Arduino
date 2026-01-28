#include "Feather_ESP32_S3.h"
#include "Potentiometer.h"
#include "IndexedEncoder.h"
#include "TempSensor.h"
#include "TimedAverager.h"
#include "Stopwatch.h"
#include "Adafruit_SleepyDog.h"

#include <WiFiMulti.h>
WiFiMulti wifiMulti;
#define DEVICE "ESP32"

#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>

#include <string>

// for WIFI_SSID and WIFI_PASSWORD
#include "WiFiSettings.h"

// Time zone info
constexpr auto TZ_INFO = "UTC-5";

constexpr auto version = "v0.94";
constexpr auto NUM_SECS = 15;
constexpr auto INFLUX_MEASUREMENT = "Air";

String rooms[] =
{
   "Barn 1st Floor",
   "Barn 2nd Floor",
   "Bathroom",
   "Bedroom",
   "Chicken Coup",
   "Clay Studio",
   "Closet",
   "Den",
   "Dinning Room",
   "Dog Shower",
   "Entry",
   "Family Room",
   "Garage",
   "Kitchen",
   "Living Room",
   "Mudroom",
   "Nook",
   "Outside",
   "Porch",
   "Studio",
   "Sunroom",
   "Wayne",
   "Workshop",
};
String postfixes[] =
{
   "  ",
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

constexpr auto NUM_ROOMS = sizeof(rooms) / sizeof(rooms[0]);
constexpr auto NUM_POSTFIXES = sizeof(postfixes) / sizeof(postfixes[0]);

constexpr Color MSG_COLOR = Color::WHITE;
constexpr Color OK_COLOR = Color::YELLOW;
constexpr Color FAILED_COLOR = Color::ORANGE;

Feather_ESP32_S3 feather;
IndexedEncoder encoder(9, 6, 5, NUM_ROOMS);
String location;
Stopwatch trigger;
TimedAverager temperature(1000 * NUM_SECS);
TimedAverager humidity(1000 * NUM_SECS);

TempSensor sensor;
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);
Point point(INFLUX_MEASUREMENT); // Influx data point

const char* LOCATION_KEY = "location"; // for preferences

enum class EncoderItem
{
   Room,
   Postfix,
   Correction10s,
   Correction100s,
};
EncoderItem activeItem;

void getColors(EncoderItem item, Color* textColor, Color* bgColor)
{
   if (item == activeItem)
   {
      *textColor = Color::YELLOW;
      *bgColor = Color::DARKGRAY;
   }
   else
   {
      *textColor = Color::ORANGE;
      *bgColor = Color::BLACK;
   }
}

void askLocation()
{
   int roomIndex = 0;
   int postfixIndex = 0;
   int correction10s = 0;
   int correction100s = 0;

   // ask for information
   while (feather.buttonA.wasPressed() == false)
   {
      Color textColor;
      Color bgColor;

      switch (activeItem)
      {
      case EncoderItem::Room:
         roomIndex = encoder.getIndex();
         break;
      case EncoderItem::Postfix:
         postfixIndex = encoder.getIndex();
         break;

      case EncoderItem::Correction10s:
         correction10s = encoder.getPosition();
         break;

      case EncoderItem::Correction100s:
         correction100s = encoder.getIndex();
         break;
      }

      feather.display.setTextSize(2);
      feather.display.setCursor(0, 0);
      feather.println("Sensor Information\n", Color::CYAN);

      feather.println("Location:", Color::WHITE);
      feather.setCursorX(20);

      getColors(EncoderItem::Room, &textColor, &bgColor);
      feather.print(rooms[roomIndex], textColor, bgColor);
      getColors(EncoderItem::Postfix, &textColor, &bgColor);
      feather.print(postfixes[postfixIndex], textColor, bgColor);
      feather.print("          "); // to the end of the line
      feather.println();

      feather.println("Calibration:", Color::WHITE);
      feather.setCursorX(20);
      getColors(EncoderItem::Correction10s, &textColor, &bgColor);
      feather.print(String(correction10s / 10.0, 1), textColor, bgColor);
      getColors(EncoderItem::Correction100s, &textColor, &bgColor);
      feather.print(correction100s, textColor, bgColor);
      feather.print(" ");

      if (encoder.button.wasPressed())
      {
         encoder.button.reset();

         // advance the active item
         switch (activeItem)
         {
         case EncoderItem::Room:
            activeItem = EncoderItem::Postfix;
            break;

         case EncoderItem::Postfix:
            activeItem = EncoderItem::Correction10s;
            break;

         case EncoderItem::Correction10s:
            activeItem = EncoderItem::Correction100s;
            break;

         case EncoderItem::Correction100s:
            activeItem = EncoderItem::Room;
            break;
         }

         // configure the encoder
         switch (activeItem)
         {
         case EncoderItem::Room:
            encoder.setIndex(roomIndex, NUM_ROOMS);
            break;
         case EncoderItem::Postfix:
            encoder.setIndex(postfixIndex, NUM_POSTFIXES);
            break;

         case EncoderItem::Correction10s:
            encoder.setPosition(correction10s);
            break;

         case EncoderItem::Correction100s:
            encoder.setIndex(correction100s, 10);
            break;
         }
      }
   }

   location = rooms[roomIndex] + postfixes[postfixIndex];
   location.trim();
}

void determineLocation()
{
   feather.echoToSerial = false;
   bool useSavedSettings = false;
   feather.preferences.begin("monitor", false);

   // if a prior location was saved, ask the user if they want to use it
   if (feather.preferences.isKey(LOCATION_KEY))
   {
      useSavedSettings = true;
      location = feather.preferences.getString(LOCATION_KEY);
      Stopwatch sw;

      while (feather.buttonA.wasPressed() == false && encoder.button.wasPressed() == false && sw.elapsedSecs() < 10)
      {
         feather.setCursor(0, 0);
         feather.println("Use Saved Settings?", Color::CYAN);
         feather.println();
         feather.print("Location: ", Color::WHITE);
         feather.println(location, Color::YELLOW);

         feather.setCursor(0, feather.display.height() - 32);
         feather.println("Press button to edit... ", Color::GRAY);
         feather.print("Continuing in ", Color::GRAY);
         feather.print(String((int)(10 - sw.elapsedSecs())), Color::GRAY);
      }
      useSavedSettings = sw.elapsedSecs() > 10;
   }
   feather.buttonA.reset();
   encoder.button.reset();
   feather.clear();

   if (useSavedSettings == false)
   {
      askLocation();
   }

   feather.preferences.putString(LOCATION_KEY, location);
   feather.preferences.end();
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
   feather.display.setTextWrap(false);
   encoder.begin();
   pinMode(BUILTIN_LED, OUTPUT);

   determineLocation();

   feather.echoToSerial = true;
   feather.clear();
   feather.println("Initializing", Color::CYAN);
   feather.moveCursorY(feather.charH()/2);

   // Setup wifi
   WiFi.mode(WIFI_STA);
   wifiMulti.addAP(WIFI_SSID, WIFI_PASSWORD);

   feather.print("WiFi... ", MSG_COLOR);
   while (wifiMulti.run() != WL_CONNECTED)
   {
      Serial.print(".");
      delay(100);
   }
   feather.printlnR("ok", OK_COLOR);

   // Accurate time is necessary for certificate validation and writing in batches
   // We use the NTP servers in your area as provided by: https://www.pool.ntp.org/zone/
   // Syncing progress and the time will be printed to Serial.
   feather.echoToSerial = false;
   feather.print("Syncing Time... ", MSG_COLOR);
   timeSync(TZ_INFO, "pool.ntp.org", "time.nis.gov");
   feather.printlnR("ok", OK_COLOR);
   feather.echoToSerial = true;

   // Check server connection
   feather.print("InfluxDB... ", MSG_COLOR);

   if (client.validateConnection())
   {
      feather.printlnR("ok", OK_COLOR);
      Serial.println(client.getServerUrl());
   }
   else
   {
      feather.printlnR("FAILED", FAILED_COLOR);
      feather.display.setTextSize(1);
      feather.println(client.getLastErrorMessage(), FAILED_COLOR);
      while (1);
   }

   feather.print("Sensor... ", MSG_COLOR);
   if (sensor.begin(false))
   {
      feather.printlnR("ok", OK_COLOR);

      Serial.println(sensor.info());
   }
   else
   {
      feather.printR("FAILED", FAILED_COLOR);
      while (1);
   }

   delay(5000);

   point.addTag("location", location.c_str());
   feather.clear();
   feather.echoToSerial = false;
   trigger.reset();

   encoder.setLimits(0, 20); // num steps to full brightness
   encoder.setPosition(encoder.getMax());

   Watchdog.enable(60 * 1000);
}

uint count = 0;

// Add the main program code into the continuous loop() function
void loop()
{
   Watchdog.reset();

   feather.displayLevel((float)encoder.getPosition() / encoder.getMax());

   count++;

   // Clear fields for reusing the point. Tags will remain the same as set above.
   point.clearFields();

   // Store measured value into point
   temperature.set(sensor.readTemperatureF());
   humidity.set(sensor.readHumidity());

   // Check WiFi connection and reconnect if needed
   if (wifiMulti.run() != WL_CONNECTED)
   {
      feather.display.println("Wifi connection lost");
   }

   if (feather.isDisplayOn())
   {
      feather.setCursor(0, 0);
      feather.display.setTextSize(2);
      feather.println("Influx Monitor", Color::YELLOW);

      float temp = temperature.get();
      float hum = humidity.get();

      feather.display.setTextSize(4);
      constexpr auto MAX_CHARS = 8;
      uint8_t x = (feather.display.width() - MAX_CHARS * feather.charW()) / 2;
      constexpr auto SPACING = 8;
      feather.display.setCursor(x, (feather.display.height() - 2 * feather.charH()) / 2 - SPACING / 2);
      feather.println(temp, " F", 8);

      feather.setCursor(x, feather.display.getCursorY() + SPACING);
      feather.println(hum, "%", 7);

      feather.display.setTextSize(2);
      feather.setCursor(0, -feather.charH());
      feather.print(location.c_str(), Color::CYAN);

      feather.printR(version, Color::GRAY);
   }

   // Write point
   if (trigger.elapsedSecs() > NUM_SECS)
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

      Serial.print(count);
      Serial.println(" data points averaged and uploaded");
      count = 0;
   }
}


