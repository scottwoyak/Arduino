#include <WiFiMulti.h>
#include "SerialX.h"
#include "WiFiSettings.h"
#include "TelemetryClient.h"
#include "Url.h"
#include <Stopwatch.h>

constexpr uint8_t TRIGGER_PIN = 6;
constexpr uint8_t ECHO_PIN = 5;

WiFiMulti wifi;

constexpr auto NUM_DECIMALS = 1;
TelemetryPublisher client("Waves/Lake", NUM_DECIMALS);

void setup()
{
   SerialX::begin();
   Serial.println("Wave Publisher");

   pinMode(TRIGGER_PIN, OUTPUT);
   pinMode(ECHO_PIN, INPUT);

   // Connect to WiFi
   wifi.addAP(WIFI_SSID, WIFI_PASSWORD);
   Serial.print("WiFi...");
   while (wifi.run() != WL_CONNECTED)
   {
      Serial.print(".");
   }
   Serial.println("OK");

   client.setCallbacks(onConnected, onDisconnected, onSendText, onReceiveText, onError, nullptr);
   client.beginSSL(TELEMETRY_HOST, TELEMETRY_PORT);
   delay(1000); // just to show the LED
}

void onConnected()
{
   Serial.println("Connected");
}

void onDisconnected()
{
   Serial.println("Disconnected");
   delay(5000); // time for Serial to print and LED to show
   Util::reset();
}

void onError(std::string msg)
{
   Serial.print("Error: ");
   Serial.println(msg.c_str());
   delay(5000); // time for Serial to print and LED to show
   Util::reset();
}

void replaceAll(std::string& str, const std::string& from, const std::string& to)
{
   if (from.empty()) return;
   size_t start_pos = 0;
   while ((start_pos = str.find(from, start_pos)) != std::string::npos)
   {
      str.replace(start_pos, from.length(), to);
      start_pos += to.length(); // Move past the new replacement
   }
}

void onSendText(std::string msg)
{
   Serial.print(">>> ");
   replaceAll(msg, "\n", "\\n");
   msg = '"' + msg + '"';
   Serial.println(msg.c_str());
}

void onReceiveText(std::string msg)
{
   Serial.print("<<< ");
   replaceAll(msg, "\n", "\\n");
   msg = '"' + msg + '"';
   Serial.println(msg.c_str());
}

void loop()
{
   if (client.isStarted())
   {
      digitalWrite(TRIGGER_PIN, LOW);
      delayMicroseconds(2);

      digitalWrite(TRIGGER_PIN, HIGH);
      delayMicroseconds(10);
      digitalWrite(TRIGGER_PIN, LOW);

      long durationMicros = pulseIn(ECHO_PIN, HIGH);
      float distanceCM = durationMicros * 0.034 / 2;
      float distanceIN = distanceCM * (1 / 2.54);
      client.setValue(distanceCM);

      // avoid echos
      delay(50);

      Serial.println("Distance: " + String(distanceCM) + " cm   " + String(distanceIN) + " in   ");
   }

   client.loop(); // Continuously poll for events and maintain connection
}
