
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
#include <Adafruit_SHT31.h>
#include <Adafruit_BME280.h>

// use the Adafruit C1500 WiFi board (via Feather M0 WiFi)
#define USE_WINC1500
#include <AdafruitIO.h>
#include <AdafruitIO_WiFi.h>

// our passwords not under version control
#include "WiFiSettings.h"

Adafruit_SHT31 sht30 = Adafruit_SHT31();
Adafruit_BME280 bme280;

// default pins for WiFi: 2, 4, 7, 8
// ids defined in WiFiSettings.h
AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, WIFI_SSID, WIFI_PASS);

// since we're using the WiFi board we can't use hardware SPI
#define FRAM_CS 19
#define FRAM_MOSI 18
#define FRAM_MISO 17
#define FRAM_SCK 16

// internet clock
WiFiUDP ntpUDP;
long timeZoneCorrection = -4 * 60 * 60;
NTPClient clock(ntpUDP, timeZoneCorrection);

// Adafruit IO feeds
AdafruitIO_Feed* temperatureFeed = io.feed("air.temperature");
AdafruitIO_Feed* temperature2Feed = io.feed("air.temperature2");
AdafruitIO_Feed* minTemperatureFeed = io.feed("air.temperature-min");
AdafruitIO_Feed* maxTemperatureFeed = io.feed("air.temperature-max");
AdafruitIO_Feed* humidityFeed = io.feed("air.humidity");
AdafruitIO_Feed* humidity2Feed = io.feed("air.humidity2");
AdafruitIO_Feed* pressureFeed = io.feed("air.barometric-pressure");
AdafruitIO_Feed* logFeed = io.feed("Log");

// feed timers
DailyTimer dailyTimer(&clock);
FeedTimer feedTimer(&clock, 60);

// persistent memory
FramSpiEx fram(FRAM_SCK, FRAM_MISO, FRAM_MOSI, FRAM_CS);

// logging mechanism
Logger Error(
   new SerialLogHandler(),
   new FeedLogHandler(logFeed)
);

#define ADDRESS1 100
#define ADDRESS2 104

MinMaxValue temperatureMinMax(
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

   if (sht30.begin(0x44) == false) {
      Error.println("SHT31 sensor initialization failed");
   }
   sht30.heater(false);

   if (bme280.begin() == false) {
      Error.println("BME280 sensor initialization failed");
   }

   clock.begin();
   clock.setUpdateInterval(24 * 60 * 60 * 1000);
   clock.update();

   if (fram.begin() == false) {
      Error.println("FRAM initialization failed");
   }

   dailyTimer.begin();
   feedTimer.begin();
   Watchdog.reset();

   analogReadResolution(12);
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

   if (feedTimer.ready()) {

      float temperature = 32 + (9.0 / 5.0) * sht30.readTemperature();
      temperatureMinMax.setValue(temperature);
      temperatureFeed->save(temperatureMinMax.getValue());
      float temperature2 = 32 + (9.0 / 5.0) * bme280.readTemperature();
      temperature2Feed->save(temperature2);

      if (dailyTimer.ready()) {
         minTemperatureFeed->save(temperatureMinMax.getMin());
         maxTemperatureFeed->save(temperatureMinMax.getMax());
         temperatureMinMax.resetMinMax();
      }

      float humidity = sht30.readHumidity();
      humidityFeed->save(humidity);
      float humidity2 = bme280.readHumidity();
      humidity2Feed->save(humidity2);

      float pressure = bme280.readPressure();
      pressureFeed->save(pressure);

      Serial.print(clock.getFormattedTime());
      Serial.print(" Temp = ");
      Serial.print(temperatureMinMax.getValue());
      Serial.print(" Humidity: ");
      Serial.print(humidity);
      Serial.print(" Pressure: ");
      Serial.print(pressure);
      Serial.println();
   }

   Watchdog.reset();

   // don't delay longer than the watchdog - 16s!
   delay(min(5000, feedTimer.msUntilNextSave()));
}
