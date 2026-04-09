
//
// Wind Publisher for Waveshare S3 Zero
//

#include <WiFiMulti.h>
#include "SerialX.h"
#include "WiFiSettings.h"
#include "TelemetryClient.h"
#include "Url.h"
#include "WindMeter.h"

WiFiMulti wifi;

constexpr auto NUM_DECIMALS = 2;
TelemetryPublisher client("Wind/Studio", NUM_DECIMALS);

constexpr auto WIND_PIN = 1;
WindMeter wind(WIND_PIN, 0);

void setup()
{
   pinMode(LED_BUILTIN, OUTPUT);
   rgbLedWrite(LED_BUILTIN, 255, 255, 255);
   delay(1000); // just to show the LED

   SerialX::begin();
   Serial.println("Wind Publisher");

   wind.begin();

   // Connect to WiFi
   rgbLedWrite(LED_BUILTIN, 0, 0, 255); // blue while connecting to WiFi
   wifi.addAP(WIFI_SSID, WIFI_PASSWORD);
   Serial.print("WiFi...");
   while (wifi.run() != WL_CONNECTED)
   {
      Serial.print(".");
   }
   Serial.println("OK");

   rgbLedWrite(LED_BUILTIN, 0, 255, 0); // green while connecting to the server
   client.setCallbacks(onConnected, onDisconnected, nullptr, onError, nullptr);
   client.beginSSL(TELEMETRY_HOST, TELEMETRY_PORT);
   delay(1000); // just to show the LED
}

void onConnected()
{
   Serial.println("Connected");
}

void onDisconnected()
{
   rgbLedWrite(LED_BUILTIN, 255, 0, 0); // red on error
   Serial.println("Disconnected");
   delay(1000); // time for Serial to print and LED to show
   Util::reset();
}

void onError(std::string msg)
{
   rgbLedWrite(LED_BUILTIN, 255, 0, 0); // red on error
   Serial.print("Error: ");
   Serial.println(msg.c_str());
   delay(1000); // time for Serial to print and LED to show
   Util::reset();
}

void loop()
{
   if (client.isStarted())
   {
      // Waveshare crashes if we set the LED value in the interrupt, so we manually do it
      if (wind.ledState())
      {
         // orange
         rgbLedWrite(LED_BUILTIN, 255, 165, 0);
      }
      else
      {
         digitalWrite(LED_BUILTIN, LOW);
      }

      // without a delay, the waveshare crashes
      delay(1);

      float speed = wind.getSpeed();
      client.setValue(speed);
   }

   client.loop(); // Continuously poll for events and maintain connection
}
