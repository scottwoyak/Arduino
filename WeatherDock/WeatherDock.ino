// Use the Adafruit C1500 WiFi board (via Feather M0 WiFi)
#define USE_WINC1500
#include <AdafruitIO.h>
#include <AdafruitIO_WiFi.h>
#include <Adafruit_SleepyDog.h>
#include <NTPClient.h>
#include <Stopwatch.h>
#include <Value.h>
#include <WiFi101.h>
#include <WiFiUdp.h>

// Temperature Sensors
#include <DallasTemperature.h>
#include <OneWire.h>

#include "WiFiSettings.h"
#include <FeedTimer.h>
#include <FlashErrorMsg.h>
#include <Stopwatch.h>
#include <WindMeter.h>

// Use default pins for WiFi: 2, 4, 7, 8
AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, WIFI_SSID, WIFI_PASS);

// Pins
#define LED_PIN 13
#define TEMPERATURE_BUS_PIN 12
#define WIND_PIN 17
#define VBATPIN A7

// wind speed sensor
WindMeter windMeter(WIND_PIN);

// water temperature sensor
OneWire oneWire(TEMPERATURE_BUS_PIN);
DallasTemperature temperatureSensor(&oneWire);

// set up the Adafruit IO feeds
AdafruitIO_Feed *surfaceTempFeed = io.feed("SurfaceTemp");
AdafruitIO_Feed *underwaterTempFeed = io.feed("UnderwaterTemp");
AdafruitIO_Feed *windMaxFeed = io.feed("WindMax");
AdafruitIO_Feed *windMinFeed = io.feed("WindMin");
AdafruitIO_Feed *windAvgFeed = io.feed("WindAvg");
AdafruitIO_Feed *windCurrentFeed = io.feed("WindCurrent");
AdafruitIO_Feed *batteryFeed = io.feed("Battery");
AdafruitIO_Feed *logFeed = io.feed("Log");

FeedTimer windFeedTimer(10 * 60);
FeedTimer waterTempFeedTimer(60);
FeedTimer mainTimer(2);

WiFiUDP ntpUDP;
long timeZoneCorrection = -4 * 60 * 60;
NTPClient clock(ntpUDP, timeZoneCorrection);

FlashErrorMsg Error;

/*
 * The setup function. We only start the sensors here
 */
void setup(void) {

  // start serial port
  Serial.begin(115200);

  // wait 5 seconds for the serial monitor to open
  while (millis() < 2000 && !Serial)
    ;
  delay(500);

  Serial.println("Starting WeatherDock Sketch");

  // build an error message from the string saved from the last shutdown
  Error.printLnIfExists();

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

  Error.sendToFeedIfExists(logFeed);

  // Start up the library
  temperatureSensor.begin();
  windMeter.begin();
  clock.begin();
  clock.update();

  windFeedTimer.begin(&clock);
  mainTimer.begin(&clock);
  waterTempFeedTimer.begin(&clock);

  Watchdog.enable(5 * 1000);

  // clear the saved error
  Error.clear();

  // WiFi.lowPowerMode();
  WiFi.maxLowPowerMode();
}

/*
 * Main function, get and show the temperature
 */
void loop(void) {

  // io.run(); is required for all sketches.
  // it should always be present at the top of your loop
  // function. it keeps the client connected to
  // io.adafruit.com, and processes any incoming data.
  if (io.run() != AIO_CONNECTED) {
    Error.save("Error: Could not reconnect to Adafruit IO");
    return;
  }
  Error.clear();

  if (mainTimer.ready()) {

    Watchdog.reset();

    windCurrentFeed->save(windMeter.getCurrent());
    Serial.print(clock.getFormattedTime());
    Serial.print(" Wind Speed: ");
    Serial.print(windMeter.getCurrent());
    Serial.print(" max=");
    Serial.println(windMeter.getMax());

    if (windFeedTimer.ready()) {

      int deviceCount = temperatureSensor.getDeviceCount();
      Serial.print("Device Count: ");
      Serial.println(deviceCount);

      windMinFeed->save(windMeter.getMin());
      windAvgFeed->save(windMeter.getAvg());
      windMaxFeed->save(windMeter.getMax());
      windMeter.reset();

      float battery = analogRead(VBATPIN);
      battery *= 2;    // we divided by 2, so multiply back
      battery *= 3.3;  // Multiply by 3.3V, our reference voltage
      battery /= 1024; // convert to voltage

      batteryFeed->save(battery);
    }

    if (waterTempFeedTimer.ready()) {

      // call sensors.requestTemperatures() to issue a global temperature
      // request to all devices on the bus
      temperatureSensor.requestTemperatures();

      // After we got the temperatures, we can print them here.
      // We use the function ByIndex, and as an example get the temperature from
      // first sensor only.
      float surfaceTemp = temperatureSensor.getTempFByIndex(0);
      float underwaterTemp = temperatureSensor.getTempFByIndex(1);

      // Check if reading was successful
      if (surfaceTemp > -100) {
        Serial.print("Surface Temperature is: ");
        Serial.println(surfaceTemp);
        surfaceTempFeed->save(surfaceTemp);
      } else {
        Error.save("Error: Could not read surface temperature data");
      }

      if (underwaterTemp > -100) {
        Serial.print("Underwater Temperature is: ");
        Serial.println(underwaterTemp);
        underwaterTempFeed->save(underwaterTemp);
      } else {
        Error.save("Error: Could not read underwater temperature data");
      }
    }

    delay(mainTimer.msUntilNextSave());
    // Stopwatch s;
    // Watchdog.sleep(mainTimer.msUntilNextSave());
    // USBDevice.attach();
    // Serial.println(s.elapsedMillis());
  }
}
