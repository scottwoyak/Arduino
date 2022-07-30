
// our passwords not under version control
#include "WiFiSettings.h"

#include <FeedTimer.h>
#include <NTPClient.h>
#include <WiFi101.h>
#include <WiFiUdp.h>
#include <Util.h>
#include <RunningAverager.h>
#include <I2C.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

#include <Fonts/FreeMono9pt7b.h>
const GFXfont* font = &FreeMono9pt7b;

// use the Adafruit C1500 WiFi board (via Feather M0 WiFi)
#define USE_WINC1500
#include <AdafruitIO.h>
#include <AdafruitIO_WiFi.h>

#define BUTTON_A 9
#define BUTTON_B 6
#define BUTTON_C 5

// A7 is already rigged for Adafruit Feathers to split the battery
// volts with 100k resistors
#define BATTERY1_VOLTS_PIN PIN_A7
#define BATTERY2_VOLTS_PIN PIN_A0

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
AdafruitIO_Feed* voltage1Feed = io.feed("boat.voltage1");
AdafruitIO_Feed* voltage2Feed = io.feed("boat.voltage2");
AdafruitIO_Feed* voltage3Feed = io.feed("boat.voltage3");

// 2 line OLED display
Adafruit_SH1107 display(64, 128, &Wire);

RunningAverager averageVolts1(50);
RunningAverager averageVolts2(50);

void setup() {

   // start serial port
   Serial.begin(115200);

   // wait a few seconds for the serial monitor to open
   while (millis() < 2000 && !Serial)
   {
   };
   delay(500);

   pinMode(BATTERY1_VOLTS_PIN, INPUT);
   pinMode(BATTERY2_VOLTS_PIN, INPUT);

   display.begin(0x3C, true); // Address 0x3C default
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
   if (Util::connectToWifi(WIFI_SSID, WIFI_PASS, 10 * 1000)) {

      // connect to io.adafruit.com
      display.println("Connecting to IO...");
      display.display();
      delay(10);
      Util::connectToAdafruitIO(&io);

      // we are connected
      Serial.println(io.statusText());
      display.println(io.statusText());
      display.display();
   }

   Serial.print("clock.begin()");
   clock.begin();
   clock.setUpdateInterval(24 * 60 * 60 * 1000);
   clock.update();
   Serial.println(" - ok");

   Serial.print("feedTimer.begin()");
   feedTimer.begin();
   Serial.println(" - ok");

   analogReadResolution(12);

   WiFi.maxLowPowerMode();

   digitalWrite(LED_BUILTIN, LOW);
}

void loop() {
   display.clearDisplay();

   float r1 = 9.97;
   float r2 = 99.7;
   averageVolts1.set(Util::readVolts(BATTERY1_VOLTS_PIN, 4096, 2.0));
   averageVolts2.set(Util::readVolts(BATTERY2_VOLTS_PIN, 4096, (r1 + r2) / r1));

   //display.setTextSize(2);
   display.setFont(font);
   display.setRotation(4);
   display.setCursor(0, font->yAdvance);
   display.println(averageVolts1.get(), 3);
   display.println(averageVolts2.get(), 3);
   display.setFont();
   display.setCursor(0, 120);
   switch (WiFi.status()) {
   case WL_CONNECTED:
      display.print("CONNECTED");
      break;
   default:
      //display.print("not connected");
      break;
   }
   display.display();

   if (WiFi.status() == WL_CONNECTED) {
      clock.update();

      // io.run(); is required for all sketches.
      // it should always be present at the top of your loop
      // function. it keeps the client connected to
      // io.adafruit.com, and processes any incoming data.
      if (io.run() != AIO_CONNECTED) {
         Serial.println("Error: Could not reconnect to Adafruit IO");
      }
      else {
         if (feedTimer.ready()) {
            voltage1Feed->save(averageVolts1.get());
            voltage2Feed->save(averageVolts2.get());
         }
      }
   }
   else {
      Serial.println("Not Connected");
   }
}
