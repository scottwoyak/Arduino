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
#include <Adafruit_SleepyDog.h>
#include <Adafruit_LPS35HW.h>

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

// feed timers
FeedTimer timer(&clock, 5 * 60);

// temperature/humidity sensor
Adafruit_SHT31 sht30;

// pressure sensor
Adafruit_LPS35HW lps33hw;

// Adafruit IO feeds
AdafruitIO_Feed* temperatureFeed;
AdafruitIO_Feed* enclosureTemperatureFeed;
AdafruitIO_Feed* humidityRelativeFeed;
AdafruitIO_Feed* humidityAbsoluteFeed;
AdafruitIO_Feed* pressureFeed;
AdafruitIO_Feed* pressureMinFeed;
AdafruitIO_Feed* pressureMaxFeed;
AdafruitIO_Feed* batteryVoltsFeed;
AdafruitIO_Feed* batteryChargingVoltsFeed;
AdafruitIO_Feed* batteryPercentFeed;
AdafruitIO_Feed* logFeed;

// logging mechanism
Logger Error(
   new SerialLogHandler(),
   new FeedLogHandler(logFeed)
);

AccumulatingAverager temperature(-10, 110);
AccumulatingAverager enclosureTemperature(-10, 200);
AccumulatingAverager humidity(0, 100);
AccumulatingAverager pressure(27, 31);
AccumulatingAverager batteryVolts(2, 5);
AccumulatingAverager chargingVolts(2, 5);

class SolarTHPWeatherStation {
private:
   String _name;
   uint8_t _batteryVoltsPin;
   uint8_t _batteryChargingVoltsPin;

private:
   const char* dup(String str) {
      return strdup((this->_name + str).c_str());
   }

public:
   SolarTHPWeatherStation(String name, uint8_t batteryVoltsPin, uint8_t batteryChargingVoltsPin) {

      this->_name = name;
      this->_batteryVoltsPin = batteryVoltsPin;
      this->_batteryChargingVoltsPin = batteryChargingVoltsPin;

      // Adafruit IO feeds
      temperatureFeed = io.feed(this->dup(".temperature"));
      enclosureTemperatureFeed = io.feed(this->dup(".enclosure-temperature"));
      humidityRelativeFeed = io.feed(this->dup(".humidity-relative"));
      humidityAbsoluteFeed = io.feed(this->dup(".humidity-absolute"));
      pressureFeed = io.feed(this->dup(".pressure"));
      pressureMinFeed = io.feed(this->dup(".pressure-min"));
      pressureMaxFeed = io.feed(this->dup(".pressure-max"));
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

      if (sht30.begin(0x44) == false) {
         Error.println("SHT30 (Temperature/Humidity) sensor initialization failed");
      }
      sht30.heater(false);

      if (!lps33hw.begin_I2C()) {
         Error.println("LPS33HW (Pressure) sensor initialization failed");
      }

      clock.begin();
      clock.setUpdateInterval(24 * 60 * 60 * 1000);
      clock.update();

      timer.begin();

      pinMode(this->_batteryVoltsPin, INPUT);
      pinMode(this->_batteryChargingVoltsPin, INPUT);
      analogReadResolution(12);

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

      batteryVolts.set(Util::readVolts(this->_batteryVoltsPin));
      chargingVolts.set(Util::readVolts(this->_batteryChargingVoltsPin));

      temperature.set(Util::C2F(sht30.readTemperature()));
      humidity.set(sht30.readHumidity());

      lps33hw.takeMeasurement();
      pressure.set(100 * lps33hw.readPressure() / 3386.39);
      enclosureTemperature.set(Util::C2F(lps33hw.readTemperature()));

      if (timer.ready()) {

         float tempF = temperature.get();
         float tempC = Util::F2C(tempF);
         float humidityRelative = humidity.get();
         float h = (humidityRelative / 1000) * 3386.39;
         float x = pow(2.718281828, (17.67 * tempC) / (tempC + 243.5));
         float humidityAbsolute = (6.112 * x * h) / (273.15 + tempC);

         // weather feeds
         temperatureFeed->save(tempF);
         enclosureTemperatureFeed->save(enclosureTemperature.get());
         humidityRelativeFeed->save(humidityRelative);
         humidityAbsoluteFeed->save(humidityAbsolute);
         pressureFeed->save(pressure.get());
         pressureMinFeed->save(pressure.getMin());
         pressureMaxFeed->save(pressure.getMax());

         Serial.print("Temp: ");
         Serial.print(temperature.get());
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

         Serial.print("Pressure: ");
         Serial.print(pressure.get());
         Serial.print("in");
         Serial.println();

         temperature.reset();
         enclosureTemperature.reset();
         humidity.reset();
         pressure.reset();

         // battery stats
         batteryVoltsFeed->save(batteryVolts.get());
         batteryChargingVoltsFeed->save(chargingVolts.get());

         float percent = Util::voltsToPercent(batteryVolts.get());
         batteryPercentFeed->save(percent);

         chargingVolts.reset();
         batteryVolts.reset();
      }
   }
};