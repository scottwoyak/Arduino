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
#include <ArduinoLowPower.h>

// Temperature Sensors
#include <DallasTemperature.h>
#include <OneWire.h>

#include "WiFiSettings.h"
#include <FeedTimer.h>
#include <FlashErrorMsg.h>
#include <Stopwatch.h>
#include <WindMeter.h>

FlashErrorMsg Error;

// Use default pins for WiFi: 2, 4, 7, 8
AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, WIFI_SSID, WIFI_PASS);

// Pins
#define LED_PIN 13
#define WIND_PIN 17

// wind speed sensor
WindMeter windMeter(WIND_PIN);

// set up the Adafruit IO feeds
AdafruitIO_Feed *testFeed = io.feed("Test");

FeedTimer mainTimer(5);

WiFiUDP ntpUDP;
long timeZoneCorrection = -4 * 60 * 60;
NTPClient clock(ntpUDP, timeZoneCorrection);

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

  Serial.println("Starting Test Sketch");
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

  Error.sendToFeedIfExists(testFeed);
  testFeed->save("Starting Test Sketch");

  clock.begin();
  clock.update();

  mainTimer.begin(&clock);

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
    Serial.println("Error: Could not reconnect to Adafruit IO");
    return;
  }

  if (mainTimer.ready()) {
    testFeed->save(clock.getFormattedTime());
  }

  unsigned long mil = millis();
  unsigned long mic = micros();

  //delay(1);
  Serial.println("going to sleep");
  digitalWrite(LED_BUILTIN, LOW);
  LowPower.idle(0);
  digitalWrite(LED_BUILTIN, HIGH);

  unsigned long m0 = (micros() - mic);
  unsigned long m1 = (millis() - mil);

  String s1("Millis: ");
  s1 += m1;
  s1 += " Micros: ";
  s1 += m0;
  testFeed->save(s1);
  /*
  Serial.print("Millis: ");
  Serial.println(m1);
  Serial.print("Micros: ");
  Serial.println(m0);
  */

  delay(5000);
  //delay(mainTimer.msUntilNextSave());
  // Stopwatch s;
  // Watchdog.sleep(mainTimer.msUntilNextSave());
  // USBDevice.attach();
  // Serial.println(s.elapsedMillis());
}
