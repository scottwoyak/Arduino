
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
   SerialX::begin();
   wind.begin();

   pinMode(LED_BUILTIN, OUTPUT);

   // Connect to WiFi
   wifi.addAP(WIFI_SSID, WIFI_PASSWORD);

   Serial.println("Wind Publisher");

   Serial.print("WiFi...");
   while (wifi.run() != WL_CONNECTED)
   {
      Serial.print(".");
   }
   Serial.println("OK");

   client.setCallbacks(onConnected, onDisconnected, nullptr, onError, nullptr);
   client.beginSSL(TELEMETRY_HOST, TELEMETRY_PORT);
}

void onConnected()
{
   Serial.println("Connected");
}

void onDisconnected()
{
   Serial.println("Disconnected");
   delay(1000); // time for Serial to print
   Util::reset();
}

void onError(std::string msg)
{
   Serial.print("Error: ");
   Serial.println(msg.c_str());
   Util::reset();
}

void loop()
{
   // Waveshare crashes if we set the LED value in the interrupt, so we manually do it
   rgbLedWrite(LED_BUILTIN, wind.ledState() ? 255 : 0, 0, 0);

   // without a delay, the waveshare crashes
   delay(1);

   float speed = wind.getSpeed();
   client.setValue(speed);
   client.loop(); // Continuously poll for events and maintain connection
}
