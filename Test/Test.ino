// Use the Adafruit C1500 WiFi board (via Feather M0 WiFi)
#define USE_WINC1500
#include <AdafruitIO.h>
#include <AdafruitIO_WiFi.h>
#include <Averager.h>
#include <NTPClient.h>
#include <Stopwatch.h>
#include <WiFi101.h>
#include <WiFiUdp.h>

#include "WiFiSettings.h"

// Use default pins for WiFi: 2, 4, 7, 8
AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, WIFI_SSID, WIFI_PASS);

// Pins

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

   Averager a(3);

   a.set(1);
   Serial.println(a.get());
   a.set(2);
   Serial.println(a.get());
   a.set(3);
   Serial.println(a.get());
   a.set(4);
   Serial.println(a.get());
}

/*
 * Main function, get and show the temperature
 */
void loop(void) {
}
