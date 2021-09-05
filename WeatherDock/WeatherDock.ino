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
#include <Battery.h>

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

// battery
Battery battery(BATTERY_VOLTS_PIN, BATTERY_CHARGING_VOLTS_PIN);

// wind speed sensor
WindMeter windMeter(WIND_PIN);

// water temperature sensor
I2CMultiplexor i2cMultiplexor;
SHT35M shtSurface(&i2cMultiplexor, 2);
SHT35M sht2Feet(&i2cMultiplexor, 3);
SHT35M sht4Feet(&i2cMultiplexor, 4);
SHT35M shtBottom(&i2cMultiplexor, 5);

// internet clock
WiFiUDP ntpUDP;
long timeZoneCorrection = -4 * 60 * 60;
NTPClient clock(ntpUDP, timeZoneCorrection);

// Adafruit IO feeds
AdafruitIO_Feed* tempSurfaceFeed = io.feed("dock.water-temperature-surface");
AdafruitIO_Feed* temp2FeetFeed = io.feed("dock.water-temperature-2-feet");
AdafruitIO_Feed* temp4FeetFeed = io.feed("dock.water-temperature-4-feet");
AdafruitIO_Feed* tempBottomFeed = io.feed("dock.water-temperature-bottom");
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
AccumulatingAverager tempBottom(20, 90);

// feed timers
FeedTimer windFeedTimer(&clock, 10 * 60, false);
FeedTimer waterTempFeedTimer(&clock, 15 * 60, false);
FeedTimer batteryFeedTimer(&clock, 10 * 60, false);
FeedTimer windNowFeedTimer(&clock, 5, false);

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

   Serial.print("begin() temp sensors");
   i2cMultiplexor.begin();
   shtSurface.begin(0x44);
   sht2Feet.begin(0x44);
   sht4Feet.begin(0x44);
   shtBottom.begin(0x44);
   Serial.println(" - ok");

   Serial.print("begin() battery");
   battery.begin();
   Serial.println(" - ok");

   Serial.print("begin() wind meter");
   windMeter.begin();
   Serial.println(" - ok");

   Serial.print("begin() clock");
   clock.begin();
   clock.setUpdateInterval(24 * 60 * 60 * 1000);
   clock.update();
   Serial.println(" - ok");

   Serial.print("begin() feed timers");
   windNowFeedTimer.begin();
   batteryFeedTimer.begin();
   windFeedTimer.begin();
   waterTempFeedTimer.begin();
   Serial.println(" - ok");

   WiFi.maxLowPowerMode();

   Watchdog.reset();
   Serial.println("Taking Initial Measurements...");
   delay(5000);
   Watchdog.reset();

   // read and print initial values
   String msg;
   msg = "Initial Values:\n";
   msg += "  Temperatures:\n";
   msg += "    Surface: " + String(Util::C2F(shtSurface.readTemperature())) + "\n";
   msg += "    2 Feet: " + String(Util::C2F(sht2Feet.readTemperature())) + "\n";
   msg += "    4 Feet: " + String(Util::C2F(sht4Feet.readTemperature())) + "\n";
   msg += "    Bottom: " + String(Util::C2F(shtBottom.readTemperature())) + "\n";
   msg += "  Wind Speeds:\n";
   msg += "    Now: " + String(windMeter.getCurrent()) + "\n";
   msg += "    Min: " + String(windMeter.getMin()) + "\n";
   msg += "    Avg: " + String(windMeter.getAvg()) + "\n";
   msg += "    Max: " + String(windMeter.getMax()) + "\n";
   msg += "  Battery:\n";
   msg += "    Volts: " + String(battery.getBatteryVolts()) + "\n";
   msg += "    Charging: " + String(battery.getChargingVolts()) + "\n";
   Serial.println(msg);

   Watchdog.reset();
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
   battery.read();

   // read temperatures
   tempSurface.set(Util::C2F(shtSurface.readTemperature()));
   temp2Feet.set(Util::C2F(sht2Feet.readTemperature()));
   temp4Feet.set(Util::C2F(sht4Feet.readTemperature()));
   tempBottom.set(Util::C2F(shtBottom.readTemperature()));

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
      float bVolts = battery.getBatteryVolts();
      float bChargingVolts = battery.getChargingVolts();
      float bPercent = Util::voltsToPercent(bVolts);
      battery.reset();

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
      float tBottom = tempBottom.get();

      tempSurfaceFeed->save(tSurface);
      temp2FeetFeed->save(t2Feet);
      temp4FeetFeed->save(t4Feet);
      tempBottomFeed->save(tBottom);

      tempSurface.reset();
      temp2Feet.reset();
      temp4Feet.reset();
      tempBottom.reset();

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
      Serial.print("   Bottom: ");
      Serial.print(tBottom);
      Serial.println();
   }

   Watchdog.reset();
}
