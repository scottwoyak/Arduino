// Use the Adafruit C1500 WiFi board (via Feather M0 WiFi)
#define USE_WINC1500
#include <AdafruitIO.h>
#include <AdafruitIO_WiFi.h>
#include <Adafruit_SleepyDog.h>
#include <NTPClient.h>
#include <WiFi101.h>
#include <WiFiUdp.h>
#include <Util.h>
#include <TimedAverager.h>
#include "WiFiSettings.h"
#include <FeedTimer.h>
#include <Logger.h>
#include <WindMeter.h>
#include <Adafruit_SHT31.h>
#include <I2CMultiplexor.h>
#include <Battery.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <Adafruit_INA219.h>


class SHT35M {
private:
   Adafruit_SHT31 _sensor;
   I2CMultiplexor* _multiplexor;
   uint8_t _port;
   bool _enabled = true;

public:
   SHT35M(I2CMultiplexor* multiplexor, uint8_t port) {
      this->_multiplexor = multiplexor;
      this->_port = port;
   }

   bool begin(uint8_t address) {
      this->_multiplexor->select(this->_port);
      if (this->_sensor.begin(address) == false) {
         this->_enabled = false;
         return false;
      }
      else {
         this->_enabled = true;
         return true;
      }
   }

   void enable(bool flag = true) {
      this->_enabled = flag;
   }

   float readTemperature() {
      if (this->_enabled) {
         this->_multiplexor->select(this->_port);
         return this->_sensor.readTemperature();
      }
      else {
         return NAN;
      }
   }
};

// for resetting the Arduino
void(*resetFunc)(void) = 0;

// Use default pins for WiFi: 2, 4, 7, 8
AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, WIFI_SSID, WIFI_PASS);

// 4 line OLED display
Adafruit_SH1107 display(64, 128, &Wire);

// volt/current sensor
Adafruit_INA219 ina;

// Pins
#define WIND_PIN 10
#define SOLAR_VOLTS_PIN PIN_A0
#define SENSOR_POWER_PIN 1

// OLED buttons
#define BUTTON_A 9
#define BUTTON_B 6
#define BUTTON_C 5

// wind speed sensor
WindMeter windMeter(WIND_PIN);

// water temperature sensor
I2CMultiplexor i2cMultiplexor;
SHT35M shtSurface(&i2cMultiplexor, 0);
SHT35M shtBottom(&i2cMultiplexor, 1);

// internet clock
WiFiUDP ntpUDP;
long timeZoneCorrection = -4 * 60 * 60;
NTPClient clock(ntpUDP, timeZoneCorrection);

// Adafruit IO feeds
AdafruitIO_Feed* tempSurfaceFeed = io.feed("bragg.water-temperature-surface");
AdafruitIO_Feed* tempBottomFeed = io.feed("bragg.water-temperature-bottom");
AdafruitIO_Feed* windMaxFeed = io.feed("bragg.wind-speed-max");
AdafruitIO_Feed* windMinFeed = io.feed("bragg.wind-speed-min");
AdafruitIO_Feed* windAvgFeed = io.feed("bragg.wind-speed-avg");
AdafruitIO_Feed* windNowFeed = io.feed("bragg.wind-speed-now");
AdafruitIO_Feed* solarVoltsFeed = io.feed("bragg.solar-volts");
AdafruitIO_Feed* batteryVoltsFeed = io.feed("bragg.battery-volts");
AdafruitIO_Feed* batteryMilliAmpsFeed = io.feed("bragg.battery-milliamps");
AdafruitIO_Feed* batteryPercentFeed = io.feed("bragg.battery-percent");
AdafruitIO_Feed* logFeed = io.feed("bragg.log");
AdafruitIO_Feed* resetFeed = io.feed("bragg.reset");
AdafruitIO_Feed* sensorResetFeed = io.feed("bragg.sensor-reset");
AdafruitIO_Feed* logRequestFeed = io.feed("bragg.log-request");
AdafruitIO_Feed* resetDelayFeed = io.feed("bragg.reset-delay");

// values
AccumulatingAverager tempSurface(20, 90);
AccumulatingAverager tempBottom(20, 90);
AccumulatingAverager solarVolts;
AccumulatingAverager batteryVolts;
AccumulatingAverager batteryMilliAmps;

// feed timers
FeedTimer windFeedTimer(&clock, 10 * 60, false);
FeedTimer waterTempFeedTimer(&clock, 15 * 60, false);
FeedTimer batteryFeedTimer(&clock, 10 * 60, false);
FeedTimer windNowFeedTimer(&clock, 5, false);

ulong sensorResetCount = 0;
ulong resetDelay = 0;

// logging mechanism
Logger ioLogger(
   new SerialLogHandler(),
   new FeedLogHandler(logFeed)
);

Logger logger(
   new SerialLogHandler(),
   new DisplayLogHandler(&display)
);

String getValues() {
   // read and print initial values
   String msg;
   msg = "";
   /*
   msg += "Temperatures:\n";
   Serial.println("Temperatures");
   msg += "  Surface: " + String(Util::C2F(shtSurface.readTemperature())) + "\n";
   msg += "  Bottom: " + String(Util::C2F(shtBottom.readTemperature())) + "\n";
   Serial.println("Wind Speed");
   msg += "Wind Speeds:\n";
   msg += "  Now: " + String(windMeter.getCurrent()) + "\n";
   msg += "  Min: " + String(windMeter.getMin()) + "\n";
   msg += "  Avg: " + String(windMeter.getAvg()) + "\n";
   msg += "  Max: " + String(windMeter.getMax()) + "\n";
   msg += "Battery:\n";
*/
   Serial.println("Volts");
   msg += "  Volts: " + String(ina.getBusVoltage_V()) + "\n";
   Serial.println("MilliAmps");
   msg += "  MilliAmps: " + String(ina.getCurrent_mA()) + "\n";
   Serial.println("Solar");
   msg += "Solar:\n";

   float r1 = 22;
   float r2 = 99.6;
   float sVolts = Util::readVolts(SOLAR_VOLTS_PIN, 4096, (r1 + r2) / r1);
   msg += "  Volts: " + String(sVolts) + "\n";
   return msg;
}

Stopwatch oledTimer(false);

enum OLED_Button {
   A,
   B,
   C,
   None
} oledButton;

static void buttonAPress() {
   oledButton = OLED_Button::A;
}

static void buttonBPress() {
   oledButton = OLED_Button::B;
}

static void buttonCPress() {
   oledButton = OLED_Button::C;
}

/*
 * The setup function. We only start the sensors here
 */
void setup(void) {

   Watchdog.enable(); // max = 16s

   pinMode(SENSOR_POWER_PIN, OUTPUT);
   digitalWrite(SENSOR_POWER_PIN, HIGH);

   pinMode(BUTTON_A, INPUT_PULLUP);
   pinMode(BUTTON_B, INPUT_PULLUP);
   pinMode(BUTTON_C, INPUT_PULLUP);

   // start serial port
   Serial.begin(115200);

   // wait 5 seconds for the serial monitor to open
   while (millis() < 2000 && !Serial)
      ;
   delay(500);

   display.begin(0x3C, true); // Address 0x3C default
   display.setTextSize(1);
   display.setTextColor(SH110X_WHITE);
   display.clearDisplay();
   display.setRotation(0);
   display.setCursor(0, 0);
   display.display();

   logger.println("Starting");

   // connect to WiFi
   logger.println("WiFi...");
   if (Util::connectToWifi(WIFI_SSID, WIFI_PASS) == false) {
      logger.println("Failed - Restarting");
      delay(1000000);
   }
   Watchdog.reset();

   // connect to io.adafruit.com
   logger.println("IO...");
   Util::connectToAdafruitIO(&io);
   Watchdog.reset();

   logger.println("io logging...");
   ioLogger.println("Starting WeatherDock Sketch");

   //   display.clearDisplay();
   //   display.setCursor(0, 0);

   logger.println("begin");
   logger.print("-multi ");
   i2cMultiplexor.begin();
   i2cMultiplexor.scan();
   logger.println("ok");

   logger.print("-surface ");
   if (shtSurface.begin(0x44) == false) {
      logger.println("X");
   }
   else {
      logger.println("ok");
   }

   logger.print("-bottom ");
   shtBottom.begin(0x44);
   if (shtBottom.begin(0x44) == false) {
      logger.println("X");
   }
   else {
      logger.println("ok");
   }

   logger.print("-wind ");
   windMeter.begin();
   logger.println("ok");

   logger.print("-clock ");
   clock.begin();
   clock.setUpdateInterval(24 * 60 * 60 * 1000);
   clock.update();
   logger.println("ok");

   logger.print("-feeds ");
   windNowFeedTimer.begin();
   batteryFeedTimer.begin();
   windFeedTimer.begin();
   waterTempFeedTimer.begin();
   logger.println("ok");

   logger.print("-ina ");
   if (ina.begin() == false) {
      logger.println("X");
   }
   else {
      logger.println("ok");
   }

   WiFi.maxLowPowerMode();

   Watchdog.reset();

   // read and print initial values
   logger.println("get values");
   Serial.println(getValues());
   Watchdog.reset();

   logger.println("\nready!!!");
   delay(3000);

   // register handler callbacks
   resetFeed->onMessage(handleMessageReset);
   sensorResetFeed->onMessage(handleMessageSensorReset);
   logRequestFeed->onMessage(handleMessageLogRequest);
   resetDelayFeed->onMessage(handleMessageResetDelay);
   //resetDelayFeed->get();

   Watchdog.reset();

   attachInterrupt(digitalPinToInterrupt(BUTTON_A), buttonAPress, FALLING);
   attachInterrupt(digitalPinToInterrupt(BUTTON_B), buttonBPress, FALLING);
   attachInterrupt(digitalPinToInterrupt(BUTTON_C), buttonCPress, FALLING);

   // start the timer that automatically turns off the display
   oledTimer.start();
}

void handleMessageLogRequest(AdafruitIO_Data* data) {
   Serial.println("Log Request");
   if (data->toPinLevel() == HIGH) {

      String msg;

      // log messages when failing
      //   on / off for each sensor
      //

      float total = (tempSurface.getCount() + tempSurface.getBadCount());
      msg = "*Failures: " +
         String(tempSurface.getBadCount()) + " " +
         String(tempBottom.getBadCount()) + " of " +
         String(total, 0) + " " +
         "Resets: " + String(sensorResetCount);
      ioLogger.println(msg);
      Watchdog.reset();
   }
}

void handleMessageReset(AdafruitIO_Data* data) {
   resetFunc();
}

void resetSensors() {

   digitalWrite(SENSOR_POWER_PIN, LOW);
   delay(resetDelay); // is this needed?
   digitalWrite(SENSOR_POWER_PIN, HIGH);

   i2cMultiplexor.begin();
   shtSurface.begin(0x44);
   shtBottom.begin(0x44);

   sensorResetCount++;
}

void handleMessageSensorReset(AdafruitIO_Data* data) {

   if (data->toPinLevel() == HIGH) {
      ioLogger.println("Resetting Sensors");
      resetSensors();

      float tSurface = Util::C2F(shtSurface.readTemperature());
      float tBottom = Util::C2F(shtBottom.readTemperature());

      Serial.println("Water Temperatures:");
      Serial.print("   Surface: ");
      Serial.print(tSurface);
      Serial.println();
      Serial.print("   Bottom: ");
      Serial.println();

      ioLogger.print("Surface: " + String(tSurface));
      ioLogger.print("Bottom: " + String(tBottom));
   }
}

void handleMessageResetDelay(AdafruitIO_Data* data) {
   Serial.print("Setting reset delay to: ");
   Serial.println(data->toLong());
   resetDelay = data->toLong();
}

Stopwatch loopTimer;

/*
 * Main function, get and show the temperature
 */
void loop(void) {

   // io.run(); is required for all sketches.
   // it should always be present at the top of your loop
   // function. it keeps the client connected to
   // io.adafruit.com, and processes any incoming data.
   if (io.run() != AIO_CONNECTED) {
      ioLogger.println("Error: Could not reconnect to Adafruit IO");
      return;
   }
   // io.run() can take a little while
   Watchdog.reset();

   clock.update();
   Watchdog.reset();

   loopTimer.reset();
   loopTimer.start();

   // read sensor values
   batteryVolts.set(ina.getBusVoltage_V());
   batteryMilliAmps.set(ina.getCurrent_mA());
   float r1 = 22;
   float r2 = 99.6;
   solarVolts.set(Util::readVolts(SOLAR_VOLTS_PIN, 4096, (r1 + r2) / r1));

   Watchdog.reset();

   // read temperatures
   if (tempSurface.set(Util::C2F(shtSurface.readTemperature())) == false) {
      resetSensors();
   }
   Watchdog.reset();
   if (tempBottom.set(Util::C2F(shtBottom.readTemperature())) == false) {
      resetSensors();
   }
   Watchdog.reset();

   loopTimer.stop();

   if (oledButton != OLED_Button::None) {
      oledTimer.reset();
      oledTimer.start();
      display.oled_command(SH110X_DISPLAYON);
      /*
      if (oledTimer.isRunning()) {
         oledTimer.stop();
         oledTimer.reset();
         display.oled_command(SH110X_DISPLAYOFF);
      }
      else {
         oledTimer.start();
         display.oled_command(SH110X_DISPLAYON);
      }
*/
   }

   // turn off the display if needed
   if (digitalRead(BUTTON_A) == HIGH &&
      digitalRead(BUTTON_B) == HIGH &&
      digitalRead(BUTTON_C) == HIGH &&
      oledTimer.elapsedMillis() > 10 * 1000) {
      display.oled_command(SH110X_DISPLAYOFF);
      oledButton = OLED_Button::None;
      oledTimer.stop();
      oledTimer.reset();
   }

   // display values if needed
   if (oledTimer.isRunning()) {
      if (oledButton == OLED_Button::C) {
         display.clearDisplay();
         display.setRotation(0);
         display.setCursor(0, 0);

         display.println("Loop:");
         display.print(" ");
         display.print(loopTimer.elapsedMillis(), 1);
         display.print(" ms");
         display.println();
         display.print(" ");
         display.print(1000 / loopTimer.elapsedMillis(), 0);
         display.print("/s");
         display.println();
         display.println();

         display.println("WiFi: ");
         display.print(WiFi.RSSI());
         display.print(" dBm");
         display.println();

         display.print("IO: ");
         display.println(io.statusText());

         display.display();
      }
      else {
         bool useLastValue = (oledButton == OLED_Button::B);
         display.clearDisplay();
         display.setRotation(0);
         display.setCursor(0, 0);

         display.println("Batt: ");
         display.print(" ");
         display.print(batteryVolts.get(useLastValue), 4);
         display.print(" v");
         display.println();

         display.print(" ");
         display.print(batteryMilliAmps.get(useLastValue), 1);
         display.print(" mA");
         display.println();

         display.println();
         display.print("Solar:");
         display.println();
         display.print(" ");
         display.print(solarVolts.get(useLastValue), 3);
         display.print(" v");
         display.println();

         display.println();
         display.print("Temp:");
         display.println();
         display.print(" ");
         display.print(tempSurface.get(useLastValue), 1);
         display.print(" F");
         display.println();
         display.print(" ");
         display.print(tempBottom.get(useLastValue), 1);
         display.print(" F");
         display.println();

         display.println();
         display.print("Wind:");
         display.println();
         display.print(" min: ");
         display.print(windMeter.getMin(), 1);
         display.println();
         display.print(" max: ");
         display.print(windMeter.getMax(), 1);
         display.println();
         display.print(" avg: ");
         display.print(windMeter.getAvg(), 1);
         display.println();
         display.print(" now: ");
         display.print(windMeter.getCurrent(), 1);
         display.println();

         display.display();
      }
   }

   // report values to IO
   /*
   if (windNowFeedTimer.ready()) {
      windNowFeed->save(windMeter.getCurrent());
      Watchdog.reset();
   }

   if (windFeedTimer.ready()) {
      float windMin = windMeter.getMin();
      float windAvg = windMeter.getAvg();
      float windMax = windMeter.getMax();
      windMeter.reset();

      windMinFeed->save(windMin);
      windAvgFeed->save(windAvg);
      windMaxFeed->save(windMax);
      Watchdog.reset();
   }
*/

   if (batteryFeedTimer.ready()) {
      float bVolts = batteryVolts.get();
      float bMilliAmps = batteryMilliAmps.get();
      float bPercent = Util::voltsToPercent(bVolts);
      float sVolts = solarVolts.get();

      batteryVoltsFeed->save(bVolts);
      batteryMilliAmpsFeed->save(bMilliAmps);
      batteryPercentFeed->save(bPercent);
      solarVoltsFeed->save(sVolts);

      batteryVolts.reset();
      batteryMilliAmps.reset();
      solarVolts.reset();

      Watchdog.reset();
   }

   if (waterTempFeedTimer.ready()) {

      float tSurface = tempSurface.get();
      float tBottom = tempBottom.get();

      tempSurfaceFeed->save(tSurface);
      tempBottomFeed->save(tBottom);

      if (sensorResetCount > 0) {
         float total = (tempSurface.getCount() + tempSurface.getBadCount());
         String msg = "Failures: " +
            String(tempSurface.getBadCount()) + " " +
            String(tempBottom.getBadCount()) + " of " +
            String(total, 0) + " " +
            "Resets: " + String(sensorResetCount);
         ioLogger.println(msg);
         sensorResetCount = 0;
      }
      else {
         float total = (tempSurface.getCount() + tempSurface.getBadCount());
         ioLogger.println("ok - " + String(total));
      }

      tempSurface.reset();
      tempBottom.reset();
      Watchdog.reset();
   }

   Watchdog.reset();
}
