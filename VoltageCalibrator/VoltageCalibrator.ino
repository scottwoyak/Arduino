
// our passwords not under version control
#include "WiFiSettings.h"

#include <FeedTimer.h>
#include <NTPClient.h>
#include <Stopwatch.h>
#include <WiFi101.h>
#include <WiFiUdp.h>
#include <Util.h>
#include <RunningAverager.h>
#include <I2C.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// use the Adafruit C1500 WiFi board (via Feather M0 WiFi)
#define USE_WINC1500
#include <AdafruitIO.h>
#include <AdafruitIO_WiFi.h>

#define BUTTON_A 9
#define BUTTON_B 6
#define BUTTON_C 5

// A7 is already rigged for Adafruit Feathers to split the battery
// volts with 100k resistors
#define BATTERY_VOLTS_PIN PIN_A7

// internet clock
WiFiUDP ntpUDP;
long timeZoneCorrection = -4 * 60 * 60;
NTPClient clock(ntpUDP, timeZoneCorrection);

// default pins for WiFi: 2, 4, 7, 8
// ids defined in WiFiSettings.h
AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, WIFI_SSID, WIFI_PASS);

// feed timers
const int FEED_INTERVAL = 10;
FeedTimer   feedTimer(&clock, FEED_INTERVAL * 60);

// Adafruit IO feeds
AdafruitIO_Feed* voltageFeed = io.feed("voltage");
AdafruitIO_Feed* timeFeed = io.feed("time");

// 2 line OLED display
Adafruit_SSD1306 display(128, 32, &Wire);

Stopwatch time(StopwatchPrecision::Millis);
RunningAverager averageVolts(50);

void setup() {

   // start serial port
   Serial.begin(115200);

   // wait a few seconds for the serial monitor to open
   while (millis() < 2000 && !Serial)
   {
   };
   delay(500);

   // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
   display.begin(SSD1306_SWITCHCAPVCC, 0x3C); // Address 0x3C for 128x32
   display.setTextSize(1);
   display.setTextColor(SH110X_WHITE);
   display.clearDisplay();
   display.setCursor(0, 0);
   display.display(); // actually display all of the above   

   Serial.println("Starting Voltage Calibration Sketch");
   display.println("Starting...");
   display.display();


   // connect to the wireless
   display.println("Connecting to WiFI...");
   display.display();
   Util::connectToWifi(WIFI_SSID, WIFI_PASS);

   // connect to io.adafruit.com
   display.println("Connecting to IO...");
   display.display();
   delay(10);
   Util::connectToAdafruitIO(&io);

   // we are connected
   Serial.println(io.statusText());
   display.println(io.statusText());
   display.display();

   Serial.print("clock.begin()");
   clock.begin();
   clock.setUpdateInterval(24 * 60 * 60 * 1000);
   clock.update();
   Serial.println(" - ok");

   Serial.print("feedTimer.begin()");
   feedTimer.begin();
   Serial.println(" - ok");

   analogReadResolution(12);

   //WiFi.maxLowPowerMode();

   //digitalWrite(LED_BUILTIN, LOW);
}

void loop() {
   clock.update();

   // io.run(); is required for all sketches.
   // it should always be present at the top of your loop
   // function. it keeps the client connected to
   // io.adafruit.com, and processes any incoming data.
   if (io.run() != AIO_CONNECTED) {
      Serial.println("Error: Could not reconnect to Adafruit IO");
      return;
   }

   display.clearDisplay();

   double t = time.elapsedSecs();
   double v = Util::readVolts(BATTERY_VOLTS_PIN);
   averageVolts.set(v);

   display.setCursor(0, 0);
   display.setTextSize(2);
   display.print(averageVolts.get(), 4);
   display.println(" v");
   display.print(t, 0);
   display.println(" s");
   display.display();

   if (feedTimer.ready()) {
      voltageFeed->save(averageVolts.get());
      timeFeed->save(String(t, 0) + "s " + String(averageVolts.get(), 3) + "v");
   }
}
