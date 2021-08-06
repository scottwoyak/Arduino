// use the Adafruit C1500 WiFi board (via Feather M0 WiFi)
#define USE_WINC1500
#include <AdafruitIO.h>
#include <AdafruitIO_WiFi.h>
#include <Logger.h>
#include <Util.h>

#include "WiFiSettings.h"

// Use default pins for WiFi: 2, 4, 7, 8
AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, WIFI_SSID, WIFI_PASS);

AdafruitIO_Feed* windSpeedFeed = io.feed("wind-speed.now");
AdafruitIO_Feed* temperatureFeed = io.feed("air.temperature");

// logging mechanism
Logger Error(
   new SerialLogHandler()
   //   new DisplayLogHandler(logFeed)
);

void windSpeedCallback(AdafruitIO_Data* data) {
   Serial.print("Wind Speed: ");
   Serial.print(data->toDouble());
   Serial.println();
}

void temperatureCallback(AdafruitIO_Data* data) {
   Serial.print("Temperature: ");
   Serial.print(data->toDouble());
   Serial.println();
}

void setup() {
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
   Serial.println(io.statusText());

   Serial.println("Starting Weather Display Sketch");

   windSpeedFeed->onMessage(windSpeedCallback);
   temperatureFeed->onMessage(temperatureCallback);

   windSpeedFeed->get();
   temperatureFeed->get();

   /*
   display.begin(0x3C, true); // Address 0x3C default

   // Clear the buffer.
   display.clearDisplay();
   display.display();

   display.setRotation(1);

   // text display tests
   display.setTextSize(2);
   display.setTextColor(SH110X_WHITE);
   */
}

void loop() {

   // io.run(); is required for all sketches.
   // it should always be present at the top of your loop
   // function. it keeps the client connected to
   // io.adafruit.com, and processes any incoming data.
   if (io.run() != AIO_CONNECTED) {
      Error.println("Error: Could not reconnect to Adafruit IO");
      return;
   }

   /*
   display.clearDisplay();
   display.setCursor(0, 0);
   display.printf("Bat: %4.3f\n", Util::readVolts(PIN_A0));
   display.printf("Out: %4.3f\n", Util::readVolts(PIN_A1));
   display.display();
   delay(100);
*/
}
