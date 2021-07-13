// Use the Adafruit C1500 WiFi board (via Feather M0 WiFi)
#define USE_WINC1500
#include <AdafruitIO.h>
#include <AdafruitIO_WiFi.h>
#include <Adafruit_SleepyDog.h>
#include <ArduinoLowPower.h>
#include <DHT.h>
#include <NTPClient.h>
#include <Stopwatch.h>
#include <Value.h>
#include <WiFi101.h>
#include <WiFiUdp.h>

// Temperature Sensors
#include <DallasTemperatureEx.h>
#include <OneWire.h>

#include "WiFiSettings.h"
#include <FeedTimer.h>
#include <Stopwatch.h>
#include <WindMeter.h>

// Use default pins for WiFi: 2, 4, 7, 8
AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, WIFI_SSID, WIFI_PASS);

// Pins
#define LED_PIN 13
#define WIND_PIN 17
#define TEMPERATURE_BUS_PIN 12
#define DHT_PIN 11
#define DHT_TYPE DHT22 // DHT 22  (AM2302)

// humidity sensor
DHT dht(DHT_PIN, DHT_TYPE);

// wind speed sensor
WindMeter windMeter(WIND_PIN);

// water temperature sensor
OneWire oneWire(TEMPERATURE_BUS_PIN);
DallasTemperatureEx temperatureSensor(&oneWire);

// set up the Adafruit IO feeds
AdafruitIO_Feed *testFeed = io.feed("Test");

FeedTimer mainTimer(5);

WiFiUDP ntpUDP;
long timeZoneCorrection = -4 * 60 * 60;
NTPClient clock(ntpUDP, timeZoneCorrection);

DeviceAddress addressSurface = {40, 254, 49, 12, 13, 0, 0, 52};
DeviceAddress address2Foot = {40, 95, 55, 117, 208, 1, 60, 40};
DeviceAddress address4Foot = {40, 191, 172, 117, 208, 1, 60, 198};

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

  testFeed->save("Starting Test Sketch");

  clock.begin();
  clock.update();

  temperatureSensor.begin();
  temperatureSensor.setResolution(12);
  mainTimer.begin(&clock);

  dht.begin();

  // WiFi.lowPowerMode();
  WiFi.maxLowPowerMode();

  temperatureSensor.printAddresses();
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

  // delay(1);
  // Serial.println("going to sleep");
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

  temperatureSensor.requestTemperatures();
  float delta = 0.1125;
  Serial.print(temperatureSensor.getTempF(addressSurface) + 6 * delta, 5);
  Serial.print(' ');
  Serial.print(temperatureSensor.getTempF(address2Foot), 5);
  Serial.print(' ');
  Serial.print(temperatureSensor.getTempF(address4Foot) + 4 * delta, 5);
  Serial.println(' ');

  // Get humidity event and print its value.
  float humidity = dht.readHumidity();
  if (isnan(humidity)) {
    Serial.println(F("Error reading humidity!"));
  } else {
    Serial.print(F("Humidity: "));
    Serial.print(humidity);
    Serial.println(F("%"));
  }

  delay(5000);
  // delay(mainTimer.msUntilNextSave());
  // Stopwatch s;
  // Watchdog.sleep(mainTimer.msUntilNextSave());
  // USBDevice.attach();
  // Serial.println(s.elapsedMillis());
}
