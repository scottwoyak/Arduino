
// our passwords not under version control
#include "WiFiSettings.h"

#include <FeedTimer.h>
#include <NTPClient.h>
#include <WiFi101.h>
#include <WiFiUdp.h>
#include <Util.h>
#include <RunningAverager.h>
#include <TimedAverager.h>
#include <I2C.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <Adafruit_SSD1351.h>
#include <Adafruit_SSD1306.h>

// use the Adafruit C1500 WiFi board (via Feather M0 WiFi)
#define USE_WINC1500
#include <AdafruitIO.h>
#include <AdafruitIO_WiFi.h>

// Color definitions
/*
#define BLACK           0x0000
#define BLUE            0x001F
#define RED             0xF800
#define GREEN           0x07E0
#define CYAN            0x07FF
#define MAGENTA         0xF81F
#define YELLOW          0xFFE0
#define WHITE           0xFFFF
*/

#define BUTTON_A 9
#define BUTTON_B 6
#define BUTTON_C 5

// A7 is already rigged for Adafruit Feathers to split the battery
// volts with 100k resistors
#define BATTERY1_VOLTS_PIN PIN_A0
#define BATTERY2_VOLTS_PIN PIN_A1

// internet clock
WiFiUDP ntpUDP;
long timeZoneCorrection = -4 * 60 * 60;
NTPClient clock(ntpUDP, timeZoneCorrection);

// default pins for WiFi: 2, 4, 7, 8
// ids defined in WiFiSettings.h
AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, WIFI_SSID, WIFI_PASS);

// feed timers
const int FEED_INTERVAL = 10;
FeedTimer   feedTimer(&clock, FEED_INTERVAL * 60, false, 5 * 60);

// Adafruit IO feeds
AdafruitIO_Feed* voltage1Feed = io.feed("boat.voltage1");
AdafruitIO_Feed* voltage2Feed = io.feed("boat.voltage2");

#define TFT_CS   9 // TFT select pin
#define TFT_RST  10 // Or set to -1 and connect to Arduino RESET pin
#define TFT_DC   11 // TFT display/command pin

//Adafruit_SSD1351 display(128, 128, &SPI, TFT_CS, TFT_DC, TFT_RST);
Adafruit_SSD1306 display(128, 64, &Wire);

RunningAverager displayVolts1(100);
RunningAverager displayVolts2(100);
TimedAverager averageVolts1(FEED_INTERVAL * 60 * 1000, 100);
TimedAverager averageVolts2(FEED_INTERVAL * 60 * 1000, 100);

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

   WiFi.setPins(8, 7, 4, 2);

   // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
   if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
      Serial.println("SSD1306 allocation failed");
      for (;;); // Don't proceed, loop forever
   }
   //display.begin();
   display.setTextColor(SSD1306_WHITE);

   //display.setRotation(1);
   //display.setTextSize(1);
   //   display.setTextColor(WHITE, BLACK);
   //display.fillScreen(BLACK);
   display.setCursor(0, 0);
   display.display();

   clock.begin();
   clock.setUpdateInterval(24 * 60 * 60 * 1000);
   clock.update();

   feedTimer.begin();

   analogReadResolution(12);

   WiFi.maxLowPowerMode();

   //display.fillScreen(BLACK);
}

Stopwatch sw;
Stopwatch sw3;
double ms;
double ms3;

void loop() {

   sw.reset();
   uint8_t status = WiFi.status();

   Serial.println(Util::WiFiStatus2Str(status));
   if (status == WL_CONNECTED) {
      io.run();
      //clock.update();
      if (feedTimer.ready()) {
         voltage1Feed->save(averageVolts1.get());
         voltage2Feed->save(averageVolts2.get());
      }
   }
   else if (status == WL_CONNECT_FAILED) {
      // this is the same as "connecting"
   }
   else {
      WiFi.begin(WIFI_SSID, WIFI_PASS);
   }
   ms = sw.elapsedMillis();


   float r1 = 9.96;
   float r2 = 99.2;
   float v1Actual = 13.27;
   float v2Actual = 13.84;
   float v1Expected = 13.27;
   float v2Expected = 13.84;
   float v1 = Util::readVolts(BATTERY1_VOLTS_PIN, 4096, (v1Expected / v1Actual) * (r1 + r2) / r1);
   r1 = 9.97;
   r2 = 99;
   float v2 = Util::readVolts(BATTERY2_VOLTS_PIN, 4096, (v2Expected / v2Actual) * (r1 + r2) / r1);
   averageVolts1.set(v1);
   averageVolts2.set(v2 - v1);
   displayVolts1.set(v1);
   displayVolts2.set(v2 - v1);

   sw3.reset();

   display.clearDisplay();
   //   display.setTextColor(YELLOW, BLACK);
   display.setTextSize(2);
   display.setCursor(0, 0);
   display.println(displayVolts1.get(), 3);
   display.println(displayVolts2.get(), 3);

   //   display.setTextColor(WHITE, BLACK);
//   display.setFont();
   display.setTextSize(1);
   display.print(ms, 1);
   display.print(" ms WIFI");
   display.println();
   display.print(ms3, 1);
   display.print(" ms DISP");
   display.println();


   display.setTextSize(0);
   display.setCursor(0, 120);
   display.setCursor(0, 56);
   display.print(Util::WiFiStatus2Str(status));
   display.print("        ");
   display.display();

   ms3 = sw3.elapsedMillis();
}
