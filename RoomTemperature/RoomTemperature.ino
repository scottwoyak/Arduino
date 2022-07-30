
// our passwords not under version control
#include "WiFiSettings.h"

#include <FeedTimer.h>
#include <NTPClient.h>
#include <Stopwatch.h>
#include <MinMaxValue.h>
#include <WiFi101.h>
#include <WiFiUdp.h>
#include <Logger.h>
#include <Util.h>
#include <AccumulatingAverager.h>
#include <RunningAverager.h>
#include <Adafruit_LPS35HW.h>
#include <I2C.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <FlashStorage.h>
#include "TempSensor.h"
#include <Adafruit_SleepyDog.h>

#define VERSION "1.2"

// Create a structure that is big enough to contain a name
// and a surname. The "valid" variable is set to "true" once
// the structure is filled with actual data for the first time.
typedef struct {
   boolean valid;
   char location[100];
   char room[100];
} ID;

// Reserve a portion of flash memory to store a "id" and call it "flash".
FlashStorage(flash, ID);

// use the Adafruit C1500 WiFi board (via Feather M0 WiFi)
#define USE_WINC1500
#include <AdafruitIO.h>
#include <AdafruitIO_WiFi.h>

#define BUTTON_A 9
#define BUTTON_B 6
#define BUTTON_C 5

// internet clock
WiFiUDP ntpUDP;
long timeZoneCorrection = -4 * 60 * 60;
NTPClient clock(ntpUDP, timeZoneCorrection);

// default pins for WiFi: 2, 4, 7, 8
// ids defined in WiFiSettings.h
AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, WIFI_SSID, WIFI_PASS);

ITempSensor* sensor;

// feed intervals in mins
const int WEATHER_INTERVAL = 1;

// feed timers
FeedTimer   weatherTimer(&clock, WEATHER_INTERVAL * 60);

// Adafruit IO feeds
AdafruitIO_Feed* temperatureFeed;
AdafruitIO_Feed* humidityFeed;

// logging mechanism
Logger Error(
   new SerialLogHandler()
);

Adafruit_SH1107 display(64, 128, &Wire);

AccumulatingAverager temperature(-10, 110);
AccumulatingAverager humidity(0, 100);

String location;
String room;

const char* choose(const char* choice1, const char* choice2, const char* choice3, int timeout = -1) {

   display.clearDisplay();
   display.setCursor(0, 0);
   display.println(choice1);
   display.println(choice2);
   display.println(choice3);
   display.display();

   Stopwatch sw;
   const char* choice;
   while (1) {
      if (timeout > 0 && sw.elapsedSecs() > timeout) {
         choice = choice1;
         break;
      }

      if (digitalRead(BUTTON_A) == LOW) {
         choice = choice1;
         break;
      }

      if (digitalRead(BUTTON_B) == LOW) {
         choice = choice2;
         break;
      }

      if (digitalRead(BUTTON_C) == LOW) {
         choice = choice3;
         break;
      }
   }

   while (digitalRead(BUTTON_A) == LOW || digitalRead(BUTTON_B) == LOW || digitalRead(BUTTON_C) == LOW) {
   }

   display.clearDisplay();
   display.setCursor(0, 0);
   display.display();

   return choice;
}


void setup() {

   // start serial port
   Serial.begin(115200);

   // wait a few seconds for the serial monitor to open
   while (millis() < 2000 && !Serial)
   {
   };
   delay(500);

   display.begin(0x3C, true); // Address 0x3C default
   display.setTextSize(1);
   display.setTextColor(SH110X_WHITE);
   display.clearDisplay();
   display.setRotation(1);
   display.setCursor(0, 0);
   display.display(); // actually display all of the above   

   Serial.println("Starting Room Temperature Sketch");
   display.println("Starting...");
   display.display();


   pinMode(BUTTON_A, INPUT_PULLUP);
   pinMode(BUTTON_B, INPUT_PULLUP);
   pinMode(BUTTON_C, INPUT_PULLUP);

   // Read the id from flash
   ID id = flash.read();

   // If this is the first run the "valid" value should be "false"...
   if (id.valid) {

      String key = String(id.location) + "-" + id.room;
      const char* choice1 = key.c_str();
      const char* choice2 = "New Location";
      const char* choice3 = nullptr;
      display.setTextSize(1);
      if (choose(choice1, choice2, choice3, 5) == choice1)
      {
         location = id.location;
         room = id.room;
      }
   }

   if (location.length() == 0) {
      display.setTextSize(2);
      location = choose("Bragg", "Wayne", "Lake");
      room = choose("Bedroom", "LivingRoom", "Basement");

      // store the information in flash
      location.toCharArray(id.location, 100);
      room.toCharArray(id.room, 100);
      id.valid = true;
      flash.write(id);
   }

   Watchdog.enable();

   // connect to the wireless
   display.setTextSize(1);
   display.println("Connecting to WiFI...");
   Util::connectToWifi(WIFI_SSID, WIFI_PASS);
   Watchdog.reset();

   // create the IO feeds
   String key = String("temperature.") + location + String("-") + room;
   key.toLowerCase();
   temperatureFeed = io.feed(key.c_str());
   key = String("humidity.") + location + String("-") + room;
   key.toLowerCase();
   humidityFeed = io.feed(key.c_str());

   // connect to io.adafruit.com
   display.setTextSize(1);
   display.println("Connecting to IO...");
   display.display();
   delay(10);
   Util::connectToAdafruitIO(&io);
   Watchdog.reset();

   // we are connected
   Serial.println(io.statusText());
   display.println(io.statusText());
   display.display();

   sensor = TempSensorFactory::create();
   Serial.print("sensor.begin()");
   if (sensor->begin() == false) {
      Error.println("Temperature/Humidity sensor initialization failed");
   }
   else {
      Serial.println(" - ok");
   }

   Serial.print("clock.begin()");
   clock.begin();
   clock.setUpdateInterval(24 * 60 * 60 * 1000);
   clock.update();
   Serial.println(" - ok");

   Serial.print("timer.begin()");
   weatherTimer.begin();
   Serial.println(" - ok");

   WiFi.maxLowPowerMode();

   display.clearDisplay();
   display.display();

   digitalWrite(LED_BUILTIN, LOW);
}

void loop() {
   clock.update();
   Watchdog.reset();

   // io.run(); is required for all sketches.
   // it should always be present at the top of your loop
   // function. it keeps the client connected to
   // io.adafruit.com, and processes any incoming data.
   if (io.run() != AIO_CONNECTED) {
      Error.println("Error: Could not reconnect to Adafruit IO");
      return;
   }

   display.clearDisplay();

   // char size for text size 1
   const int XS = 6;
   const int YS = 8;

   if (digitalRead(BUTTON_A) == LOW) {
      display.setCursor(0, 0);
      display.setTextSize(3);
      display.print(Util::C2F(sensor->readTemperature()), 1);
      display.println(" F");
      display.print(sensor->readHumidity(), 1);
      display.println("%");
      display.setTextSize(1);
      display.println();
      display.print(location);
      display.print("-");
      display.print(room);
   }
   else {
      display.setTextSize(1);
      display.setCursor(0, 64 - YS);
      display.print(Util::C2F(sensor->readTemperature()), 1);
      display.print(" F ");
      display.print(sensor->readHumidity(), 1);
      display.print("%");
   }

   display.setTextSize(1);
   display.setCursor(128 - 3 * XS, 64 - YS);
   display.print(VERSION);
   display.display();

   temperature.set(Util::C2F(sensor->readTemperature()));
   humidity.set(sensor->readHumidity());

   if (weatherTimer.ready()) {

      float t = temperature.get();
      float h = humidity.get();

      // weather feeds
      temperatureFeed->save(t);
      humidityFeed->save(h);

      Serial.print("Temp: ");
      Serial.print(t);
      Serial.print("F");
      Serial.println();

      Serial.print("Humidity: ");
      Serial.print(h);
      Serial.print("%");
      Serial.println();

      temperature.reset();
      humidity.reset();
   }
}
