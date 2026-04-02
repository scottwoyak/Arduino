
// undefine to use the remote server
//#define TELEMETRY_LOCAL

#include <WiFi.h>
#include <WiFiMulti.h>
#include <WiFiClientSecure.h>

#include <WebSocketsClient.h>
#include "SerialX.h"
#include "WiFiSettings.h"
#include "RateTracker.h"
#include "TelemetryClient.h"
#include "Url.h"
#include "WindMeter.h"

WiFiMulti wifi;
RollingRateTracker rate(10);
TelemetryPublisher client("Wind/Studio", 3);
WindMeter wind(A5);

float getValue()
{
   constexpr ulong period = 2 * 1000 * 1000;
   float x = 2 * std::numbers::pi * (((float)(micros() % period)) / period);
   return sin(x);
}

void setup()
{
   SerialX::begin();
   wind.begin();

   // Connect to WiFi
   wifi.addAP(WIFI_SSID, WIFI_PASSWORD);

   Serial.println("Wind Publisher");

   Serial.print("WiFi...");
   while (wifi.run() != WL_CONNECTED)
   {
      Serial.print(".");
   }
   Serial.println("OK");

   client.setCallbacks(onConnected, onDisconnected, onText, onError, onStarted);
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

void onText(std::string payload)
{
   rate.tick();
}

void onError(std::string msg)
{
   Util::reset();
}

void onStarted()
{
   rate.reset();
}

void loop()
{
   float speed = wind.getSpeed();
   client.setValue(speed);
   client.loop(); // Continuously poll for events and maintain connection
}
