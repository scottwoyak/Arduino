// Use the Adafruit C1500 WiFi board (via Feather M0 WiFi)
#define USE_WINC1500
#include <AdafruitIO.h>
#include <AdafruitIO_WiFi.h>
#include <Adafruit_SleepyDog.h>
#include <NTPClient.h>
#include <WiFi101.h>
#include <WiFiUdp.h>
#include <Util.h>

// Temperature Sensors
#include <DallasTemperatureEx.h>
#include <OneWire.h>

#include "WiFiSettings.h"
#include <FeedTimer.h>
#include <Logger.h>
#include <WindMeter.h>

// Use default pins for WiFi: 2, 4, 7, 8
AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, WIFI_SSID, WIFI_PASS);

// Pins
#define TEMPERATURE_BUS_PIN 11
#define WIND_PIN 17
#define BATTERY_VOLTS_PIN PIN_A0
#define BATTERY_CHARGING_VOLTS_PIN PIN_A7
#define SOLAR_ENABLE_PIN PIN_A1

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
AdafruitIO_Feed* windMaxFeed = io.feed("wind-speed.max");
AdafruitIO_Feed* windMinFeed = io.feed("wind-speed.min");
AdafruitIO_Feed* windAvgFeed = io.feed("wind-speed.avg");
AdafruitIO_Feed* windCurrentFeed = io.feed("wind-speed.now");
AdafruitIO_Feed* batteryVoltsFeed = io.feed("battery.volts");
AdafruitIO_Feed* batteryChargingVoltsFeed = io.feed("battery.charging-volts");
AdafruitIO_Feed* batteryPercentFeed = io.feed("battery.percent");
AdafruitIO_Feed* logFeed = io.feed("Log");

// feed timers
FeedTimer windFeedTimer(&clock, 10 * 60);
FeedTimer waterTempFeedTimer(&clock, 60);
FeedTimer batteryFeedTimer(&clock, 10 * 60, false);
FeedTimer nowTimer(&clock, 10);

// logging mechanism
Logger Error(
   new SerialLogHandler(),
   new FeedLogHandler(logFeed)
);

/*
 * The setup function. We only start the sensors here
 */
void setup(void) {

   Watchdog.enable(); // max = 16s

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

   analogReadResolution(12);

   windMeter.begin();
   clock.begin();
   clock.setUpdateInterval(24 * 60 * 60 * 1000);
   clock.update();

   nowTimer.begin();
   batteryFeedTimer.begin();
   windFeedTimer.begin();
   waterTempFeedTimer.begin();

   // WiFi.lowPowerMode();
   WiFi.maxLowPowerMode();
}

float batteryVolts = 0;

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

   if (nowTimer.ready()) {
      windCurrentFeed->save(windMeter.getCurrent());

      // read the voltage at the battery
      batteryVolts = max(Util::readVolts(BATTERY_VOLTS_PIN), batteryVolts);
   }

   if (windFeedTimer.ready()) {
      windMinFeed->save(windMeter.getMin());
      windAvgFeed->save(windMeter.getAvg());
      windMaxFeed->save(windMeter.getMax());
      windMeter.reset();
   }

   if (batteryFeedTimer.ready()) {
      batteryVoltsFeed->save(batteryVolts);

      const float MAX_VOLTS = 4.17;
      const float MIN_VOLTS = 3.5;
      float percent = 100 * (batteryVolts - MIN_VOLTS) / (MAX_VOLTS - MIN_VOLTS);
      percent = constrain(percent, 0, 100);
      batteryPercentFeed->save(percent);

      // read the output volts to detect the charging volts
      float chargingVolts = Util::readVolts(BATTERY_CHARGING_VOLTS_PIN);
      batteryChargingVoltsFeed->save(chargingVolts);

      Serial.print("Volts: ");
      Serial.print(batteryVolts);
      Serial.print("|");
      Serial.print(chargingVolts);
      Serial.print(" ");
      Serial.print(percent);
      Serial.print("%");
      Serial.println();

      batteryVolts = 0;
   }

   if (waterTempFeedTimer.ready()) {

      // get the temperatures
      temperatureSensor.requestTemperatures();

      // apply corrections from our own testing of these particular sensors
      float delta = 0.1125;
      float tempSurface =
         temperatureSensor.getTempF(addressSurface) + 6 * delta;
      float temp2Feet = temperatureSensor.getTempF(address2Foot);
      float temp4Feet = temperatureSensor.getTempF(address4Foot) + 4 * delta;

      if (tempSurface < -100 || tempSurface > 100) {
         tempSurface = NAN;
         Error.println("Error: Could not read surface temperature data");
      }
      if (temp2Feet < -100 || temp2Feet > 100) {
         temp2Feet = NAN;
         Error.println("Error: Could not read '2 feet' temperature data");
      }
      if (temp4Feet < -100 || temp4Feet > 100) {
         temp4Feet = NAN;
         Error.println("Error: Could not read '4 feet' temperature data");
      }

      tempSurfaceFeed->save(tempSurface);
      temp2FeetFeed->save(temp2Feet);
      temp4FeetFeed->save(temp4Feet);
   }

   Watchdog.reset();
   delay(nowTimer.msUntilNextSave());
}
