#include <DHT.h>
#include <FeedTimer.h>
#include <NTPClient.h>
#include <Stopwatch.h>
#include <MinMaxValue.h>
#include <WiFi101.h>
#include <WiFiUdp.h>
#include <FramSpiEx.h>
#include <ValueStoreFram.h>
#include <DailyTimer.h>
#include <Adafruit_SleepyDog.h>
#include <Logger.h>
#include <Util.h>

// use the Adafruit C1500 WiFi board (via Feather M0 WiFi)
#define USE_WINC1500
#include <AdafruitIO.h>
#include <AdafruitIO_WiFi.h>

// our passwords not under version control
#include "WiFiSettings.h"

// default pins for WiFi: 2, 4, 7, 8
// ids defined in WiFiSettings.h
AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, WIFI_SSID, WIFI_PASS);

// Pins
#define DHT_PIN 14

// since we're using the WiFi board we can't use hardware SPI
#define FRAM_CS 19
#define FRAM_MOSI 18
#define FRAM_MISO 17
#define FRAM_SCK 16

// temperature/humidity sensor
#define DHT_TYPE DHT22 // DHT 22  (AM2302)
DHT dht(DHT_PIN, DHT_TYPE);

// internet clock
WiFiUDP ntpUDP;
long timeZoneCorrection = -4 * 60 * 60;
NTPClient clock(ntpUDP, timeZoneCorrection);

// Adafruit IO feeds
AdafruitIO_Feed* tempFeed = io.feed("AirTemp");
AdafruitIO_Feed* minTempFeed = io.feed("AirTempMin");
AdafruitIO_Feed* maxTempFeed = io.feed("AirTempMax");
AdafruitIO_Feed* humidityFeed = io.feed("air.humidity");
AdafruitIO_Feed* logFeed = io.feed("Log");

// feed timers
DailyTimer dailyTimer(&clock);
FeedTimer tempTimer(&clock, 60);

// persistent memory
FramSpiEx fram(FRAM_SCK, FRAM_MISO, FRAM_MOSI, FRAM_CS);

// logging mechanism
Logger Error(
   new SerialLogHandler(),
   new FeedLogHandler(logFeed)
);

#define ADDRESS1 100
#define ADDRESS2 104

MinMaxValue tempF(
   new ValueStoreSimple(),
   new ValueStoreFram(&fram, ADDRESS1),
   new ValueStoreFram(&fram, ADDRESS2)
);

void setup(void) {

   // for the Feather M0, max time is 16s. Need to call Watchdog.reset() by then
   Watchdog.enable();

   // start serial port
   Serial.begin(115200);

   // wait a few seconds for the serial monitor to open
   while (millis() < 2000 && !Serial)
      ;
   delay(500);

   Serial.println("Starting WeatherCabin Sketch");

   // connect to the wireless
   Util::connectToWifi(WIFI_SSID, WIFI_PASS);
   Watchdog.reset();

   // connect to io.adafruit.com
   Util::connectToAdafruitIO(&io);
   Watchdog.reset();

   // we are connected
   Serial.println(io.statusText());
   logFeed->save("Starting WeatherCabin Sketch");

   dht.begin();
   clock.begin();
   clock.setUpdateInterval(24 * 60 * 60 * 1000);
   clock.update();

   if (fram.begin() == false) {
      Error.println("FRAM initialization failed");
   }

   dailyTimer.begin();
   tempTimer.begin();
   Watchdog.reset();
}

void loop(void) {

   // io.run(); is required for all sketches.
   // it should always be present at the top of your loop
   // function. it keeps the client connected to
   // io.adafruit.com, and processes any incoming data.
   if (io.run() != AIO_CONNECTED) {
      Error.println("Error: Could not reconnect to Adafruit IO");
      return;
   }

   if (tempTimer.ready()) {

      float dhttemp = dht.readTemperature(true);
      tempF.setValue(dhttemp);
      tempFeed->save(tempF.getValue());

      if (dailyTimer.ready()) {
         minTempFeed->save(tempF.getMin());
         maxTempFeed->save(tempF.getMax());
         tempF.resetMinMax();
      }

      float humidity = dht.readHumidity();
      humidityFeed->save(humidity);

      Serial.print(clock.getFormattedTime());
      Serial.print(" Temp = ");
      Serial.print(tempF.getValue());
      Serial.print(" Humidity: ");
      Serial.println(humidity);
   }

   Watchdog.reset();

   // don't delay longer than the watchdog - 16s!
   delay(min(5000, tempTimer.msUntilNextSave()));
}
