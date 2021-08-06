
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

// our passwords not under version control
#include "WiFiSettings.h"

// Pins
const uint8_t BATTERY_VOLTS_PIN = PIN_A3;
const uint8_t BATTERY_CHARGING_VOLTS_PIN = PIN_A7;

// default pins for WiFi: 2, 4, 7, 8
// ids defined in WiFiSettings.h
AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, WIFI_SSID, WIFI_PASS);

// internet clock
WiFiUDP ntpUDP;
long timeZoneCorrection = -4 * 60 * 60;
NTPClient clock(ntpUDP, timeZoneCorrection);

// Adafruit IO feeds
AdafruitIO_Feed* temperatureFeed = io.feed("cabin.temperature");
AdafruitIO_Feed* enclosureTemperatureFeed = io.feed("cabin.enclosure-temperature");
AdafruitIO_Feed* humidityFeed = io.feed("cabin.humidity");
AdafruitIO_Feed* pressureFeed = io.feed("cabin.pressure");
AdafruitIO_Feed* batteryVoltsFeed = io.feed("cabin.battery-volts");
AdafruitIO_Feed* batteryChargingVoltsFeed = io.feed("cabin.battery-charging-volts");
AdafruitIO_Feed* batteryPercentFeed = io.feed("cabin.battery-percent");
AdafruitIO_Feed* logFeed = io.feed("Log");

// feed timers
FeedTimer timer(&clock, 5 * 60);

// temperature/humidity sensor
Adafruit_SHT31 sht30;

// pressure sensor
Adafruit_LPS35HW lps33hw;

// logging mechanism
Logger Error(
   new SerialLogHandler(),
   new FeedLogHandler(logFeed)
);

void setup(void) {

   // start serial port
   Serial.begin(115200);

   // wait a few seconds for the serial monitor to open
   while (millis() < 2000 && !Serial)
   {
   };
   delay(500);

   Serial.println("Starting WeatherWoods Sketch");

   Watchdog.enable(10 * 1000);

   // connect to the wireless
   Util::connectToWifi(WIFI_SSID, WIFI_PASS);
   Watchdog.reset();

   // connect to io.adafruit.com
   Util::connectToAdafruitIO(&io);
   Watchdog.reset();

   // we are connected
   Serial.println(io.statusText());
   logFeed->save("Starting WeatherWoods Sketch");

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

   pinMode(BATTERY_VOLTS_PIN, INPUT);
   analogReadResolution(12);

   WiFi.maxLowPowerMode();
}

AccumulatingAverager temperature;
AccumulatingAverager enclosureTemperature;
AccumulatingAverager humidity;
AccumulatingAverager pressure;
AccumulatingAverager batteryVolts;
AccumulatingAverager chargingVolts;

void loop(void) {

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

   batteryVolts.set(Util::readVolts(BATTERY_VOLTS_PIN));
   chargingVolts.set(Util::readVolts(BATTERY_CHARGING_VOLTS_PIN));

   temperature.set(Util::C2F(sht30.readTemperature()));
   humidity.set(sht30.readHumidity());

   lps33hw.takeMeasurement();
   pressure.set(100 * lps33hw.readPressure() / 3386.39);
   enclosureTemperature.set(Util::C2F(lps33hw.readTemperature()));

   if (timer.ready()) {

      // weather stats
      temperatureFeed->save(temperature.get());
      enclosureTemperatureFeed->save(enclosureTemperature.get());
      humidityFeed->save(humidity.get());
      pressureFeed->save(pressure.get());

      Serial.print("Temp: ");
      Serial.print(temperature.get());
      Serial.print(" Humidity: ");
      Serial.print(humidity.get());
      Serial.print(" Pressure: ");
      Serial.print(pressure.get());
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
