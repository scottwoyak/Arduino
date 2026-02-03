#include "Feather_ESP32_S3.h"
#include "IndexedEncoder.h"
#include "TempSensor.h"
#include "TimedAverager.h"
#include "Stopwatch.h"
#include "Adafruit_SleepyDog.h"
#include "SerialX.h"
#include "Influx.h"
#include <string>

// for WIFI_SSID and WIFI_PASSWORD
#include "WiFiSettings.h"

Format humFormat("##.#%");
Format tempFormat("###.## F");

constexpr auto version = "v0.96";

String rooms[] =
{
   "Barn 1st Floor",
   "Barn 2nd Floor",
   "Bathroom",
   "Bedroom",
   "Chicken Coop",
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

Feather_ESP32_S3 feather;
IndexedEncoder encoder(9, 6, 5, NUM_ROOMS);
String location;

TempSensor sensor;

constexpr auto INFLUX_MEASUREMENT = "Air";
constexpr auto INFLUX_INTERVAL_S = 15;
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);
TimedPoint point(INFLUX_INTERVAL_S, INFLUX_MEASUREMENT); // Influx data point
Field* tempField = point.addTimeAveragedField("temperature", 3);
Field* humField = point.addTimeAveragedField("humidity", 2);

const char* LOCATION_KEY = "location"; // for preferences

enum class EncoderItem
{
   Room,
   Postfix,
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
      }

      feather.display.setTextSize(2);
      feather.display.setCursor(0, 0);
      feather.println("Sensor Information\n", Color::HEADING);

      feather.println("Location:", Color::LABEL);
      feather.setCursorX(20);

      getColors(EncoderItem::Room, &textColor, &bgColor);
      feather.print(rooms[roomIndex], textColor, bgColor);
      getColors(EncoderItem::Postfix, &textColor, &bgColor);
      feather.print(postfixes[postfixIndex], textColor, bgColor);
      feather.print("          "); // to the end of the line
      feather.println();

      if (encoder.button.wasPressed())
      {
         // advance the active item
         switch (activeItem)
         {
         case EncoderItem::Room:
            activeItem = EncoderItem::Postfix;
            break;

         case EncoderItem::Postfix:
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

      constexpr auto WAIT_TIME_S = 5;
      while (feather.buttonA.wasPressed() == false && encoder.button.wasPressed() == false && sw.elapsedSecs() < WAIT_TIME_S)
      {
         feather.setCursor(0, 0);
         feather.println("Use Saved Settings?", Color::HEADING);
         feather.println();
         feather.print("Location: ", Color::LABEL);
         feather.println(location, Color::VALUE);

         feather.setCursor(0, feather.display.height() - 32);
         feather.println("Press button to edit... ", Color::GRAY);
         feather.print("Continuing in ", Color::GRAY);
         feather.print(String((int)(WAIT_TIME_S - sw.elapsedSecs())), Color::GRAY);
      }
      useSavedSettings = sw.elapsedSecs() > WAIT_TIME_S;
   }
   feather.buttonA.reset();
   encoder.button.reset();
   feather.clearDisplay();

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
   SerialX::begin();

   feather.begin();
   feather.display.setTextWrap(false);
   encoder.begin();
   pinMode(BUILTIN_LED, OUTPUT);

   determineLocation();

   feather.echoToSerial = true;
   feather.clearDisplay();
   feather.println("Initializing", Color::HEADING);
   feather.moveCursorY(feather.charH()/2);

   feather.print("Sensor... ", Color::LABEL);
   if (sensor.begin(true))
   {
      feather.printlnR("ok", Color::VALUE);

      SerialX::println("   Type: ", sensor.type());
      SerialX::println("   Address: ", sensor.address());
      SerialX::println("   ID: ", sensor.id());
   }
   else
   {
      feather.printR("FAILED", Color::RED);
      while (1);
   }

   Influx::begin(&feather, WIFI_SSID, WIFI_PASSWORD, &client);

   delay(5000);

   point.addTag("location", location);

   feather.clearDisplay();
   feather.echoToSerial = false;

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

   // Store measured value into point
   tempField->set(sensor.readTemperatureF());
   humField->set(sensor.readHumidity());

   // Check WiFi connection and reconnect if needed
   if (wifiMulti.run() != WL_CONNECTED)
   {
      feather.display.println("Wifi connection lost");
   }

   if (feather.isDisplayOn())
   {
      feather.setCursor(0, 0);
      feather.display.setTextSize(2);
      feather.print("Influx", Color::HEADING);
      feather.printR(sensor.type(), Color::GRAY);
      feather.println();

      float temp = tempField->get();
      float hum = humField->get();

      feather.display.setTextSize(4);
      constexpr auto MAX_CHARS = 8;
      uint8_t x = (feather.display.width() - MAX_CHARS * feather.charW()) / 2;
      constexpr auto SPACING = 8;
      feather.display.setCursor(x, (feather.display.height() - 2 * feather.charH()) / 2 - SPACING / 2);
      feather.println(temp, tempFormat, Color::VALUE);

      feather.setCursor(x, feather.display.getCursorY() + SPACING);
      feather.println(hum, humFormat, Color::VALUE);

      feather.display.setTextSize(2);
      feather.setCursor(0, -feather.charH());
      feather.print(location.c_str(), Color::CYAN);

      feather.printR(version, Color::SUB_LABEL);
   }

   // Write point
   if (point.ready())
   {
      digitalWrite(BUILTIN_LED, HIGH);
      if (point.post(&client)==false)
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


