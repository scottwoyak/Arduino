// Use the Adafruit C1500 WiFi board (via Feather M0 WiFi)
#define USE_WINC1500
#include <AdafruitIO.h>
#include <AdafruitIO_WiFi.h>
#include <Averager.h>
#include <NTPClient.h>
#include <Stopwatch.h>
#include <WiFi101.h>
#include <WiFiUdp.h>
#include <ValueStoreFram.h>
#include <DailyTimer.h>

#include "WiFiSettings.h"

// Use default pins for WiFi: 2, 4, 7, 8
AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, WIFI_SSID, WIFI_PASS);

// Pins
#include "Adafruit_FRAM_SPI.h"

// since we're using the WiFi board we can't use hardware SPI
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

   /*
   typedef enum {
      WL_NO_SHIELD = 255,
      WL_IDLE_STATUS = 0,
      WL_NO_SSID_AVAIL,
      WL_SCAN_COMPLETED,
      WL_CONNECTED,
      WL_CONNECT_FAILED,
      WL_CONNECTION_LOST,
      WL_DISCONNECTED,
      WL_AP_LISTENING,
      WL_AP_CONNECTED,
      WL_AP_FAILED,
      WL_PROVISIONING,
      WL_PROVISIONING_FAILED
   } wl_status_t;
*/

   WiFi.setPins(8, 7, 4, 2);
   int status = WL_IDLE_STATUS;
   while (status != WL_CONNECTED) {
      Serial.print("Attempting to connect to SSID: ");
      Serial.println(WIFI_SSID);
      // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
      status = WiFi.begin(WIFI_SSID, WIFI_PASS);

      // wait 10 seconds for connection:
      delay(10000);
   }

   Serial.print("WiFi status: ");
   Serial.println(WiFi.status());
   clock.begin();
   clock.update();
   delay(1000);
   Serial.println(clock.getFormattedTime());

   fram.begin();

   DailyTimer dt(&clock);
   dt.ready();
}


/*
 * Main function, get and show the temperature
 */
void loop(void) {
}
