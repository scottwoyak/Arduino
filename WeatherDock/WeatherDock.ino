// Use the Adafruit C1500 WiFi board (via Feather M0 WiFi)
#define USE_WINC1500
#include <AdafruitIO.h>
#include <AdafruitIO_WiFi.h>
#include <Adafruit_SleepyDog.h>
#include <NTPClient.h>
#include <Stopwatch.h>
#include <WiFi101.h>
#include <WiFiUdp.h>

// Temperature Sensors
#include <DallasTemperatureEx.h>
#include <OneWire.h>

#include "WiFiSettings.h"
#include <FeedTimer.h>
#include <Logger.h>
#include <Stopwatch.h>
#include <WindMeter.h>

// Use default pins for WiFi: 2, 4, 7, 8
AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, WIFI_SSID, WIFI_PASS);

// Pins
#define TEMPERATURE_BUS_PIN 11
#define WIND_PIN 17
#define VBAT_PIN A7

// wind speed sensor
WindMeter windMeter(WIND_PIN);

// water temperature sensor
OneWire oneWire(TEMPERATURE_BUS_PIN);
DallasTemperatureEx temperatureSensor(&oneWire);
DeviceAddress addressSurface = { 40, 191, 172, 117, 208, 1, 60, 198 };
DeviceAddress address2Foot = { 40, 95, 55, 117, 208, 1, 60, 40 };
DeviceAddress address4Foot = { 40, 254, 49, 12, 13, 0, 0, 52 };

// internet clock
WiFiUDP ntpUDP;
long timeZoneCorrection = -4 * 60 * 60;
NTPClient clock(ntpUDP, timeZoneCorrection);

// Adafruit IO feeds
AdafruitIO_Feed* tempSurfaceFeed = io.feed("water-temperature.surface");
AdafruitIO_Feed* temp2FeetFeed = io.feed("water-temperature.2-feet");
AdafruitIO_Feed* temp4FeetFeed = io.feed("water-temperature.4-feet");
AdafruitIO_Feed* windMaxFeed = io.feed("WindMax");
AdafruitIO_Feed* windMinFeed = io.feed("WindMin");
AdafruitIO_Feed* windAvgFeed = io.feed("WindAvg");
AdafruitIO_Feed* windCurrentFeed = io.feed("WindCurrent");
AdafruitIO_Feed* batteryVoltsFeed = io.feed("battery.volts");
AdafruitIO_Feed* batteryChargeFeed = io.feed("battery.charge");
AdafruitIO_Feed* logFeed = io.feed("Log");

// feed timers
FeedTimer windFeedTimer(&clock, 10 * 60);
FeedTimer waterTempFeedTimer(&clock, 60);
FeedTimer mainTimer(&clock, 2);

// logging mechanism
Logger Error(
   new SerialLogHandler(),
   new FeedLogHandler(logFeed)
);

/*
 * The setup function. We only start the sensors here
 */
void setup(void) {

   Watchdog.enable(60 * 1000);

   // start serial port
   Serial.begin(115200);

   // wait 5 seconds for the serial monitor to open
   while (millis() < 2000 && !Serial)
      ;
   delay(500);

   Serial.println("Starting WeatherDock Sketch");

   // connect to io.adafruit.com
   Serial.print("Connecting to Adafruit IO");
   io.connect();

   // wait for a connection
   while (io.status() < AIO_CONNECTED) {
      Serial.print(".");
      delay(500);
   }

   // we are connected
   Serial.println();
   Serial.println(io.statusText());

   logFeed->save("Starting WeatherDock Sketch");

   temperatureSensor.setResolution(12);
   temperatureSensor.begin();

   windMeter.begin();
   clock.begin();
   clock.setUpdateInterval(24 * 60 * 60 * 1000);
   clock.update();

   windFeedTimer.begin();
   mainTimer.begin();
   waterTempFeedTimer.begin();

   // WiFi.lowPowerMode();
   WiFi.maxLowPowerMode();
}

double getVolts() {
   float volts = analogRead(VBAT_PIN);
   volts *= 2;    // we divided by 2, so multiply back
   volts *= 3.3;  // Multiply by 3.3V, our reference voltage
   volts /= 1024; // convert to voltage
   return volts;
}

/*
 * Main function, get and show the temperature
 */
void loop(void) {

   clock.update();

   // io.run(); is required for all sketches.
   // it should always be present at the top of your loop
   // function. it keeps the client connected to
   // io.adafruit.com, and processes any incoming data.
   if (io.run() != AIO_CONNECTED) {
      Error.println("Error: Could not reconnect to Adafruit IO");
      return;
   }

   if (mainTimer.ready()) {

      windCurrentFeed->save(windMeter.getCurrent());
      /*
      Serial.print(clock.getFormattedTime());
      Serial.print(" Wind Speed: ");
      Serial.print(windMeter.getCurrent());
      Serial.print(" max=");
      Serial.println(windMeter.getMax());

      float delta = 0.1125;
      temperatureSensor.requestTemperatures();
      float tempSurface = temperatureSensor.getTempF(addressSurface) + 6 * delta;
      float temp2Feet = temperatureSensor.getTempF(address2Foot);
      float temp4Feet = temperatureSensor.getTempF(address4Foot) + 4 * delta;
      tempSurface = temperatureSensor.getTempFByIndex(0);
      Serial.print("Water Temp: ");
      Serial.print(tempSurface);
      Serial.print(" ");
      Serial.print(temp2Feet);
      Serial.print(" ");
      Serial.println(temp4Feet);
  */

  // read the voltage with the charger off
  /*
  digitalWrite(SOLAR_ENABLE_PIN, HIGH);
  Serial.println(getVolts());
  delay(100);
  Serial.println(getVolts());
  delay(100);
  Serial.println(getVolts());
  delay(100);
  Serial.println(getVolts());
  delay(100);
  Serial.println(getVolts());
  delay(100);
  Serial.println(getVolts());
  digitalWrite(SOLAR_ENABLE_PIN, LOW);
  */

      if (windFeedTimer.ready()) {

         windMinFeed->save(windMeter.getMin());
         windAvgFeed->save(windMeter.getAvg());
         windMaxFeed->save(windMeter.getMax());
         windMeter.reset();

         // read the raw voltage including the charger
         float volts = getVolts();
         batteryVoltsFeed->save(volts);
         Serial.print("Volts: ");
         Serial.print(volts);

         /*
               // read the voltage with the charger off
               digitalWrite(SOLAR_ENABLE_PIN, HIGH);
               float charge = analogRead(VBAT_PIN);
               charge *= 2;    // we divided by 2, so multiply back
               charge *= 3.3;  // Multiply by 3.3V, our reference voltage
               charge /= 1024; // convert to voltage
               const float MAX_VOLTS = 4.17;
               const float MIN_VOLTS = 3.6;
               charge = 100 * (charge - MIN_VOLTS) / (MAX_VOLTS - MIN_VOLTS);
               batteryChargeFeed->save(charge);
               digitalWrite(SOLAR_ENABLE_PIN, LOW);
               Serial.print(" ");
               Serial.print(charge);
               Serial.println("%");
             }
         */

         if (waterTempFeedTimer.ready()) {

            // get the temperatures
            temperatureSensor.requestTemperatures();

            // apply corrections from our own testing of these particular sensors
            float delta = 0.1125;
            float tempSurface =
               temperatureSensor.getTempF(addressSurface) + 6 * delta;
            float temp2Feet = temperatureSensor.getTempF(address2Foot);
            float temp4Feet = temperatureSensor.getTempF(address4Foot) + 4 * delta;

            if (tempSurface > -100) {
               tempSurfaceFeed->save(tempSurface);
            }
            else {
               Error.println("Error: Could not read surface temperature data");
            }
            if (temp2Feet > -100) {
               temp2FeetFeed->save(temp2Feet);
            }
            else {
               Error.println("Error: Could not read '2 feet' temperature data");
            }
            if (temp2Feet > -100) {
               temp4FeetFeed->save(temp4Feet);
            }
            else {
               Error.println("Error: Could not read '4 feet' temperature data");
            }
         }
      }

      Watchdog.reset();
      delay(mainTimer.msUntilNextSave());
      // Stopwatch s;
      // Watchdog.sleep(mainTimer.msUntilNextSave());
      // USBDevice.attach();
      // Serial.println(s.elapsedMillis());
   }
}
