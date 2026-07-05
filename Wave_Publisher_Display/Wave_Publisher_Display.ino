
#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include <WiFi.h>
#include "SerialX.h"
#include "WiFiSettings.h"
#include "TelemetryClient.h"
#include "Url.h"
#include "Stopwatch.h"

Arduino arduino;
constexpr uint8_t TRIGGER_PIN = 6;
constexpr uint8_t ECHO_PIN = 5;

Format distFormatCm("###.## cm");
Format distFormatIn("###.## in");

constexpr auto NUM_DECIMALS = 1;
TelemetryPublisher client("Waves/Lake", NUM_DECIMALS);

void setup()
{
   arduino.begin();
   SerialX::begin();
   Serial.println("Wave Publisher Display");

   pinMode(TRIGGER_PIN, OUTPUT);
   pinMode(ECHO_PIN, INPUT);

   // Connect to WiFi
   WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
   Serial.print("WiFi...");
   while (WiFi.status() != WL_CONNECTED)
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

Stopwatch sw;

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
      float distance = durationMicros * 0.034 / 2;

      // avoid echos
      delayMicroseconds(durationMicros);

      client.setValue(distance);

      arduino.setTextSize(5);
      arduino.setCursor(0, 0);
      arduino.println(distance, distFormatCm, Color::VALUE);
      arduino.println(distance * (1/2.54), distFormatIn, Color::VALUE);

      Serial.println(sw.elapsedMillis() + String(" ms"));
      sw.reset();
   }
   else
   {
      arduino.setTextSize(3);
      arduino.setCursor(0, 0);
      arduino.println("Not Connected", Color::WHITE);
   }

   client.loop(); // Continuously poll for events and maintain connection
}
