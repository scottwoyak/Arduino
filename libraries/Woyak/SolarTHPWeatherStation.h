#pragma once

#include <FeedTimer.h>
#include <NTPClient.h>
#include <Stopwatch.h>
#include <MinMaxValue.h>
#include <WiFi101.h>
#include <WiFiUdp.h>
#include <Logger.h>
#include <Util.h>
#include <Adafruit_SHT31.h>
#include <AccumulatingAverager.h>
#include <RunningAverager.h>
#include <Adafruit_SleepyDog.h>
#include <Adafruit_LPS35HW.h>
#include <I2C.h>
#include <Battery.h>

// use the Adafruit C1500 WiFi board (via Feather M0 WiFi)
#define USE_WINC1500
#include <AdafruitIO.h>
#include <AdafruitIO_WiFi.h>

// internet clock
WiFiUDP ntpUDP;
long timeZoneCorrection = -4 * 60 * 60;
NTPClient clock(ntpUDP, timeZoneCorrection);

// default pins for WiFi: 2, 4, 7, 8
// ids defined in WiFiSettings.h
AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, WIFI_SSID, WIFI_PASS);

// temperature/humidity sensor
Adafruit_SHT31 sht30;

// pressure sensor
Adafruit_LPS35HW lps33hw;

// Adafruit IO feeds
AdafruitIO_Feed* temperatureFeed;
AdafruitIO_Feed* avgTemperatureFeed;
AdafruitIO_Feed* enclosureTemperatureFeed;
AdafruitIO_Feed* humidityRelativeFeed;
AdafruitIO_Feed* humidityAbsoluteFeed;
AdafruitIO_Feed* dewPointFeed;
AdafruitIO_Feed* pressureFeed;
AdafruitIO_Feed* batteryVoltsFeed;
AdafruitIO_Feed* batteryChargingVoltsFeed;
AdafruitIO_Feed* batteryPercentFeed;
AdafruitIO_Feed* logFeed;

// logging mechanism
Logger Error(
   new SerialLogHandler(),
   new FeedLogHandler(logFeed)
);

// feed intervals in mins
const int WEATHER_INTERVAL = 10;
const int BATTERY_INTERVAL = 15;

AccumulatingAverager temperature(-10, 110);
RunningAverager avgTemperature(24 * 60 / WEATHER_INTERVAL); // 24 hrs of samples
AccumulatingAverager enclosureTemperature(-10, 200);
AccumulatingAverager humidity(0, 100);
AccumulatingAverager pressure(27, 31);

// dewPoint function NOAA
// reference (1) : http://wahiduddin.net/calc/density_algorithms.htm
// reference (2) : http://www.colorado.edu/geography/weather_station/Geog_site/about.htm
//
double dewPoint(double celsius, double humidity)
{
   // (1) Saturation Vapor Pressure = ESGG(T)
   double RATIO = 373.15 / (273.15 + celsius);
   double RHS = -7.90298 * (RATIO - 1);
   RHS += 5.02808 * log10(RATIO);
   RHS += -1.3816e-7 * (pow(10, (11.344 * (1 - 1 / RATIO))) - 1);
   RHS += 8.1328e-3 * (pow(10, (-3.49149 * (RATIO - 1))) - 1);
   RHS += log10(1013.246);

   // factor -3 is to adjust units - Vapor Pressure SVP * humidity
   double VP = pow(10, RHS - 3) * humidity;

   // (2) DEWPOINT = F(Vapor Pressure)
   double T = log(VP / 0.61078); // temp var
   return (241.88 * T) / (17.558 - T);
}

class SolarTHPWeatherStation {
private:
   String _name;
   Battery _battery;
   bool _pressureSensorExists = false;

   // feed timers
   FeedTimer weatherTimer;
   FeedTimer batteryTimer;

   const char* dup(String str) {
      return strdup((this->_name + str).c_str());
   }

public:
   SolarTHPWeatherStation(
      String name,
      uint8_t batteryVoltsPin,
      uint8_t batteryChargingVoltsPin,
      uint8_t weatherIntervalMins = WEATHER_INTERVAL
   ) :
      weatherTimer(&clock, weatherIntervalMins * 60),
      batteryTimer(&clock, BATTERY_INTERVAL * 60),
      _battery(batteryVoltsPin, batteryChargingVoltsPin)
   {
      this->_name = name;

      // Adafruit IO feeds
      temperatureFeed = io.feed(this->dup(".temperature"));
      avgTemperatureFeed = io.feed(this->dup(".avg-temperature"));
      enclosureTemperatureFeed = io.feed(this->dup(".enclosure-temperature"));
      humidityRelativeFeed = io.feed(this->dup(".humidity-relative"));
      humidityAbsoluteFeed = io.feed(this->dup(".humidity-absolute"));
      dewPointFeed = io.feed(this->dup(".dew-point"));
      pressureFeed = io.feed(this->dup(".pressure"));
      batteryVoltsFeed = io.feed(this->dup(".battery-volts"));
      batteryChargingVoltsFeed = io.feed(this->dup(".battery-charging-volts"));
      batteryPercentFeed = io.feed(this->dup(".battery-percent"));
      logFeed = io.feed(this->dup(".log"));
   }

   void setup() {
      // start serial port
      Serial.begin(115200);

      // wait a few seconds for the serial monitor to open
      while (millis() < 2000 && !Serial)
      {
      };
      delay(500);

      Serial.println("Starting " + this->_name + " Weather Sketch");

      Watchdog.enable(10 * 1000);

      // connect to the wireless
      Util::connectToWifi(WIFI_SSID, WIFI_PASS);
      Watchdog.reset();

      // connect to io.adafruit.com
      Util::connectToAdafruitIO(&io);
      Watchdog.reset();

      // we are connected
      Serial.println(io.statusText());
      logFeed->save("Starting " + this->_name + " Weather Sketch");

      Serial.print("sht30.begin()");
      if (sht30.begin(0x44) == false) {
         Error.println("SHT30 (Temperature/Humidity) sensor initialization failed");
      }
      Serial.println(" - ok");

      if (I2C::exists(LPS35HW_I2CADDR_DEFAULT)) {
         this->_pressureSensorExists = true;
         Serial.print("lps33hw.begin()");
         if (!lps33hw.begin_I2C()) {
            Error.println("LPS33HW (Pressure) sensor initialization failed");
         }
         Serial.println(" - ok");
      }

      Serial.print("clock.begin()");
      clock.begin();
      clock.setUpdateInterval(24 * 60 * 60 * 1000);
      clock.update();
      Serial.println(" - ok");

      Serial.print("timer.begin()");
      weatherTimer.begin();
      batteryTimer.begin();
      Serial.println(" - ok");

      this->_battery.begin();

      WiFi.maxLowPowerMode();
   }

   void loop() {
      Watchdog.reset();
      clock.update();

      // io.run(); is required for all sketches.
      // it should always be present at the top of your loop
      // function. it keeps the client connected to
      // io.adafruit.com, and processes any incoming data.
      if (io.run() != AIO_CONNECTED) {
         Error.println("Error: Could not reconnect to Adafruit IO");
         return;
      }

      this->_battery.read();

      temperature.set(Util::C2F(sht30.readTemperature()));
      humidity.set(sht30.readHumidity());

      if (this->_pressureSensorExists) {
         Serial.print("Reading Pressure");
         lps33hw.takeMeasurement();
         pressure.set(100 * lps33hw.readPressure() / 3386.39);
         enclosureTemperature.set(Util::C2F(lps33hw.readTemperature()));
         Serial.println(" - ok");
      }

      if (this->weatherTimer.ready()) {

         float tempF = temperature.get();
         float tempC = Util::F2C(tempF);
         float humidityRelative = humidity.get();
         float h = (humidityRelative / 1000) * 3386.39;
         float x = pow(2.718281828, (17.67 * tempC) / (tempC + 243.5));
         float humidityAbsolute = (6.112 * x * h) / (273.15 + tempC);
         float dp = Util::C2F(dewPoint(tempC, humidityRelative));

         //
         // Dew Point
         //
         // Below 55°F       Air Feels Dry
         //       55 ~60°F   Air Feels Comfortable
         //       60 ~64°F   Air Fairly Humid
         //       65 ~69°F   Humid
         //       70 ~75°F   Very Humid
         // Above 75°F       Oppressive
         //

         avgTemperature.set(tempF);

         // weather feeds
         temperatureFeed->save(temperature.get());
         avgTemperatureFeed->save(avgTemperature.get());
         humidityRelativeFeed->save(humidityRelative);
         humidityAbsoluteFeed->save(humidityAbsolute);
         dewPointFeed->save(dp);

         if (this->_pressureSensorExists) {
            pressureFeed->save(pressure.get());
            enclosureTemperatureFeed->save(enclosureTemperature.get());
         }

         Serial.print("Temp: ");
         Serial.print(tempF);
         Serial.print("F");
         Serial.println();

         Serial.print("Avg Temp: ");
         Serial.print(avgTemperature.get());
         Serial.print("F");
         Serial.println();

         Serial.print("Humidity (Relative): ");
         Serial.print(humidityRelative);
         Serial.print("%");
         Serial.println();

         Serial.print("Humidity (Absolute): ");
         Serial.print(humidityAbsolute);
         Serial.print("g/m3");
         Serial.println();

         Serial.print("Dew Point: ");
         Serial.print(dp);
         Serial.println("F");

         if (this->_pressureSensorExists) {
            Serial.print("Pressure: ");
            Serial.print(pressure.get());
            Serial.print("in");
            Serial.println();
         }

         temperature.reset();
         enclosureTemperature.reset();
         humidity.reset();
         pressure.reset();
      }

      if (this->batteryTimer.ready()) {

         // battery stats
         batteryVoltsFeed->save(this->_battery.getBatteryVolts());
         batteryChargingVoltsFeed->save(this->_battery.getChargingVolts());

         batteryPercentFeed->save(this->_battery.getPercent());

         Serial.print("Charging Volts: ");
         Serial.print(this->_battery.getChargingVolts());
         Serial.println();

         Serial.print("Battery Volts: ");
         Serial.print(this->_battery.getBatteryVolts());
         Serial.println();

         Serial.print("Charge Percent: ");
         Serial.print(this->_battery.getPercent());
         Serial.print("%");
         Serial.println();

         this->_battery.reset();
      }
   }
};