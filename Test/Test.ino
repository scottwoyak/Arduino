// Use the Adafruit C1500 WiFi board (via Feather M0 WiFi)
#define USE_WINC1500
#include <AdafruitIO.h>
#include <AdafruitIO_WiFi.h>
#include <Averager.h>
#include <NTPClient.h>
#include <Stopwatch.h>
#include <WiFi101.h>
#include <WiFiUdp.h>
#include <FramValueStore.h>

#include "WiFiSettings.h"

// Use default pins for WiFi: 2, 4, 7, 8
AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, WIFI_SSID, WIFI_PASS);

// Pins
#include "Adafruit_FRAM_SPI.h"

/* Example code for the Adafruit SPI FRAM breakout */
uint8_t FRAM_CS = 0;
uint8_t FRAM_SCK = 5;
uint8_t FRAM_MISO = 21;
uint8_t FRAM_MOSI = 20;

//Adafruit_FRAM_SPI fram = Adafruit_FRAM_SPI(FRAM_CS);  // use hardware SPI

//Or use software SPI, any pins!
FramSpiEx fram(FRAM_SCK, FRAM_MISO, FRAM_MOSI, FRAM_CS);

uint16_t          addr = 0;

WiFiUDP ntpUDP;
long timeZoneCorrection = -4 * 60 * 60;
NTPClient clock(ntpUDP, timeZoneCorrection);

void t(IValueStore& store) {
   Serial.println("setting value");
   store.set(3.1415926);
   Serial.println("getting value");
   Serial.println(store.get(), 8);
}

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

   if (fram.begin()) {
      Serial.println("Found SPI FRAM");
   }
   else {
      Serial.println("No SPI FRAM found ... check your connections\r\n");
      while (1);
   }

   // Read the first byte
   uint8_t test = fram.read8(0x0);
   Serial.print("Restarted "); Serial.print(test); Serial.println(" times");

   // Test write ++
   fram.writeEnable(true);
   fram.write8(0x0, test + 1);
   fram.writeEnable(false);

   Serial.print("float: ");
   Serial.println(sizeof(float));
   Serial.print("double: ");
   Serial.println(sizeof(double));

   BasicValueStore bvs;
   FramValueStore fvs(&fram, 100);

   t(bvs);
   t(fvs);
}


/*
 * Main function, get and show the temperature
 */
void loop(void) {
}
