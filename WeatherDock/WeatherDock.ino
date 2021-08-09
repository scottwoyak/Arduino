// Use the Adafruit C1500 WiFi board (via Feather M0 WiFi)
#define USE_WINC1500
#include <AdafruitIO.h>
#include <AdafruitIO_WiFi.h>
#include <Adafruit_SleepyDog.h>
#include <NTPClient.h>
#include <WiFi101.h>
#include <WiFiUdp.h>
#include <Util.h>
#include <AccumulatingAverager.h>
#include "WiFiSettings.h"
#include <FeedTimer.h>
#include <Logger.h>
#include <WindMeter.h>
#include <Adafruit_SHT31.h>
#include <I2CMultiplexor.h>

class SHT35M {
private:
   Adafruit_SHT31 _sensor;
   I2CMultiplexor* _multiplexor;
   uint8_t _port;

public:
   SHT35M(I2CMultiplexor* multiplexor, uint8_t port) {
      this->_multiplexor = multiplexor;
      this->_port = port;
   }

   bool begin(uint8_t address) {
      this->_multiplexor->select(this->_port);
      return this->_sensor.begin(address);
   }

   float readTemperature() {
      this->_multiplexor->select(this->_port);
      return this->_sensor.readTemperature();
   }
};

// Use default pins for WiFi: 2, 4, 7, 8
AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, WIFI_SSID, WIFI_PASS);

// Pins
#define WIND_PIN 17
#define BATTERY_VOLTS_PIN PIN_A0
#define BATTERY_CHARGING_VOLTS_PIN PIN_A7

// wind speed sensor
WindMeter windMeter(WIND_PIN);

// water temperature sensor
I2CMultiplexor i2cMultiplexor;
SHT35M shtSurface(&i2cMultiplexor, 2);
SHT35M sht2Feet(&i2cMultiplexor, 3);
SHT35M sht4Feet(&i2cMultiplexor, 4);

// internet clock
WiFiUDP ntpUDP;
long timeZoneCorrection = -4 * 60 * 60;
NTPClient clock(ntpUDP, timeZoneCorrection);

// Adafruit IO feeds
AdafruitIO_Feed* tempSurfaceFeed = io.feed("dock.water-temperature-surface");
AdafruitIO_Feed* temp2FeetFeed = io.feed("dock.water-temperature-2-feet");
AdafruitIO_Feed* temp4FeetFeed = io.feed("dock.water-temperature-4-feet");
AdafruitIO_Feed* windMaxFeed = io.feed("dock.wind-speed-max");
AdafruitIO_Feed* windMinFeed = io.feed("dock.wind-speed-min");
AdafruitIO_Feed* windAvgFeed = io.feed("dock.wind-speed-avg");
AdafruitIO_Feed* windNowFeed = io.feed("dock.wind-speed-now");
AdafruitIO_Feed* batteryVoltsFeed = io.feed("dock.battery-volts");
AdafruitIO_Feed* batteryChargingVoltsFeed = io.feed("dock.battery-charging-volts");
AdafruitIO_Feed* batteryPercentFeed = io.feed("dock.battery-percent");
AdafruitIO_Feed* logFeed = io.feed("dock.log");

// values
AccumulatingAverager tempSurface(20, 90);
AccumulatingAverager temp2Feet(20, 90);
AccumulatingAverager temp4Feet(20, 90);
AccumulatingAverager batteryVolts(2, 5);
AccumulatingAverager chargingVolts(2, 5);

// feed timers
FeedTimer windFeedTimer(&clock, 10 * 60);
FeedTimer waterTempFeedTimer(&clock, 5 * 60);
FeedTimer batteryFeedTimer(&clock, 5 * 60);
FeedTimer windNowFeedTimer(&clock, 5);

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

   i2cMultiplexor.begin();
   shtSurface.begin(0x44);
   sht2Feet.begin(0x44);
   sht4Feet.begin(0x44);

   // high resolution for voltage measurement
   pinMode(BATTERY_CHARGING_VOLTS_PIN, INPUT);
   pinMode(BATTERY_VOLTS_PIN, INPUT);
   analogReadResolution(12);

   windMeter.begin();
   clock.begin();
   clock.setUpdateInterval(24 * 60 * 60 * 1000);
   clock.update();

   windNowFeedTimer.begin();
   batteryFeedTimer.begin();
   windFeedTimer.begin();
   waterTempFeedTimer.begin();

   // WiFi.lowPowerMode();
   WiFi.maxLowPowerMode();
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

   // read the voltage at the battery
   batteryVolts.set(Util::readVolts(BATTERY_VOLTS_PIN));
   chargingVolts.set(Util::readVolts(BATTERY_CHARGING_VOLTS_PIN));

   // read temperatures
   tempSurface.set(Util::C2F(shtSurface.readTemperature()));
   temp2Feet.set(Util::C2F(sht2Feet.readTemperature()));
   temp4Feet.set(Util::C2F(sht4Feet.readTemperature()));

   if (windNowFeedTimer.ready()) {
      windNowFeed->save(windMeter.getCurrent());

      Serial.print("Current Wind Speed: ");
      Serial.print(windMeter.getCurrent());
      Serial.println();
   }

   if (windFeedTimer.ready()) {
      float windMin = windMeter.getMin();
      float windAvg = windMeter.getAvg();
      float windMax = windMeter.getMax();
      windMeter.reset();

      windMinFeed->save(windMin);
      windAvgFeed->save(windAvg);
      windMaxFeed->save(windMax);

      Serial.println("Wind Speed:");
      Serial.print("   Min: ");
      Serial.print(windMin);
      Serial.println();
      Serial.print("   Avg: ");
      Serial.print(windAvg);
      Serial.println();
      Serial.print("   Max: ");
      Serial.print(windMax);
      Serial.println();
   }

   if (batteryFeedTimer.ready()) {
      float bVolts = batteryVolts.get();
      float bChargingVolts = chargingVolts.get();
      float bPercent = Util::voltsToPercent(bVolts);
      batteryVolts.reset();
      chargingVolts.reset();

      batteryVoltsFeed->save(bVolts);
      batteryChargingVoltsFeed->save(bChargingVolts);
      batteryPercentFeed->save(bPercent);

      Serial.println("Battery:");
      Serial.print("   Volts: ");
      Serial.print(bVolts);
      Serial.println();
      Serial.print("   Charging Volts: ");
      Serial.print(bChargingVolts);
      Serial.println();
      Serial.print("   Percent: ");
      Serial.print(bPercent);
      Serial.println();
   }

   if (waterTempFeedTimer.ready()) {

      float tSurface = tempSurface.get();
      float t2Feet = temp2Feet.get();
      float t4Feet = temp4Feet.get();

      tempSurfaceFeed->save(tSurface);
      temp2FeetFeed->save(t2Feet);
      temp4FeetFeed->save(t4Feet);

      tempSurface.reset();
      temp2Feet.reset();
      temp4Feet.reset();

      Serial.println("Water Temperatures:");
      Serial.print("   Surface: ");
      Serial.print(tSurface);
      Serial.println();
      Serial.print("   2 Feet: ");
      Serial.print(t2Feet);
      Serial.println();
      Serial.print("   4 Feet: ");
      Serial.print(t4Feet);
      Serial.println();
   }

   Watchdog.reset();
}
