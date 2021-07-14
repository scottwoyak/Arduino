#include <DHT.h>
#include <FeedTimer.h>
#include <NTPClient.h>
#include <Stopwatch.h>
#include <Value.h>
#include <WiFi101.h>
#include <WiFiUdp.h>

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
#define DHT_PIN 18

// temperature/humidity sensor
#define DHT_TYPE DHT22 // DHT 22  (AM2302)
DHT dht(DHT_PIN, DHT_TYPE);

// set up the 'digital' feed
AdafruitIO_Feed* tempFeed = io.feed("AirTemp");
AdafruitIO_Feed* minTempFeed = io.feed("AirTempMin");
AdafruitIO_Feed* maxTempFeed = io.feed("AirTempMax");
AdafruitIO_Feed* humidityFeed = io.feed("air.humidity");
AdafruitIO_Feed* logFeed = io.feed("Log");

FeedTimer minMaxTimer(24 * 60 * 60, false);
FeedTimer tempTimer(60);

WiFiUDP ntpUDP;
long timeZoneCorrection = -4 * 60 * 60;
NTPClient clock(ntpUDP, timeZoneCorrection);

Value tempF;

void setup(void) {

   // start serial port
   Serial.begin(115200);

   // wait 5 seconds for the serial monitor to open
   while (millis() < 2000 && !Serial)
      ;
   delay(500);

   Serial.println("Starting WeatherCabin Sketch");

   // connect to io.adafruit.com
   Serial.print("Connecting to Adafruit IO");
   io.connect();

   // wait for a connection
   while (io.status() < AIO_CONNECTED) {
      Serial.print(".");
      delay(500);
   }
   Serial.println();

   // we are connected
   Serial.println(io.statusText());
   logFeed->save("Starting WeatherCabin Sketch");

   dht.begin();
   clock.begin();
   clock.update();

   minMaxTimer.begin(&clock);
   tempTimer.begin(&clock);
   // WiFi.lowPowerMode();
}

void loop(void) {

   // io.run(); is required for all sketches.
   // it should always be present at the top of your loop
   // function. it keeps the client connected to
   // io.adafruit.com, and processes any incoming data.
   if (io.run() != AIO_CONNECTED) {
      Serial.println("Error: Could not reconnect to Adafruit IO");
      return;
   }

   if (tempTimer.ready()) {

      float dhttemp = dht.readTemperature(true);
      tempF.setValue(dhttemp);
      tempFeed->save(tempF.getValue());

      if (minMaxTimer.ready()) {
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

   delay(tempTimer.msUntilNextSave());
}
