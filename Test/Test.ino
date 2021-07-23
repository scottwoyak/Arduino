
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <Util.h>

Adafruit_SH1107 display = Adafruit_SH1107(64, 128, &Wire);

void setup() {
   Serial.begin(115200);

   analogReadResolution(12);

   display.begin(0x3C, true); // Address 0x3C default

   // Clear the buffer.
   display.clearDisplay();
   display.display();

   display.setRotation(1);

   // text display tests
   display.setTextSize(2);
   display.setTextColor(SH110X_WHITE);
}

void loop() {
   display.clearDisplay();
   display.setCursor(0, 0);
   display.printf("Bat: %4.3f\n", Util::readVolts(PIN_A0));
   display.printf("Out: %4.3f\n", Util::readVolts(PIN_A1));
   display.display();
   delay(100);
}


/*

#include "Arduino.h"
#include "WiFiSettings.h"
// use the Adafruit C1500 WiFi board (via Feather M0 WiFi)
#define USE_WINC1500
#include <AdafruitIO.h>
#include <AdafruitIO_WiFi.h>

#include <Util.h>

// Use default pins for WiFi: 2, 4, 7, 8
AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, WIFI_SSID, WIFI_PASS);

AdafruitIO_Feed* testFeed = io.feed("Test");

void setup(void) {

   // start serial port
   Serial.begin(115200);

   // wait 5 seconds for the serial monitor to open
   while (millis() < 2000 && !Serial)
      ;
   delay(500);

   // connect to the wireless
   Util::connectToWifi(WIFI_SSID, WIFI_PASS);

   // connect to io.adafruit.com
   Util::connectToAdafruitIO(&io);

// we are connected
   analogReadResolution(12);
   Serial.println(io.statusText());



   Serial.println("Starting Test Sketch");

   pinMode(LED_BUILTIN, OUTPUT);


   WiFi.maxLowPowerMode();

}


void loop(void) {

   digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));

   String str;
   float volts = analogRead(PIN_A0);
   volts *= 2;    // we divided by 2, so multiply back
   volts *= 3.3;  // Multiply by 3.3V, our reference voltage
   volts /= 4096; // convert to voltage
   str += volts;

   digitalWrite(PIN_A4, HIGH); // turn off charger
   volts = analogRead(PIN_A0);
   volts *= 2;    // we divided by 2, so multiply back
   volts *= 3.3;  // Multiply by 3.3V, our reference voltage
   volts /= 4096; // convert to voltage
   str += "|";
   str += volts;
   delay(2000);
   digitalWrite(PIN_A4, LOW); // turn on charger

   volts = analogRead(PIN_A7);
   volts *= 2;    // we divided by 2, so multiply back
   volts *= 3.3;  // Multiply by 3.3V, our reference voltage
   volts /= 4096; // convert to voltage
   str += "|";
   str += volts;

   String str;
   float volts = analogRead(A7);
   volts *= 2;    // we divided by 2, so multiply back
   volts *= 3.3;  // Multiply by 3.3V, our reference voltage
   volts /= 4096; // convert to voltage
   str += String(volts, 4);

   Serial.println(str);
   //   testFeed->save(str);
   delay(10 * 1000);
}
*/