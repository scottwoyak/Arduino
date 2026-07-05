/// <summary>
/// Telemetry data subscriber with display feedback.
/// </summary>
/// <remarks>
/// Subscribes to a telemetry topic and displays received data along with
/// query rate (WebSocket message rate) and change rate (data update frequency).
/// Implements callback-based event handling for connection lifecycle and data flow.
/// 
/// Uncomment TELEMETRY_LOCAL to use a local telemetry server instead of the remote.
/// Hardware: Feather ESP32 with WiFi and TFT display.
/// </remarks>

// Uncomment to use local telemetry server instead of remote
// #define TELEMETRY_LOCAL

#include <Arduino.h>
#include <WiFi.h>
#include <cmath>

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include "RollingRate.h"
#include "SerialX.h"
#include "Stopwatch.h"
#include "TelemetryClient.h"
#include "Url.h"

#include "WiFiSettings.h"

constexpr const char* TELEMETRY_TOPIC = "Test";
// constexpr const char* TELEMETRY_TOPIC = "Waves/Lake";

constexpr unsigned long RATE_UPDATE_INTERVAL_MS = 1000;

Arduino arduino;
Stopwatch sw(false);

RollingRate queryRate(100);
RollingRate changeRate(100);

TelemetrySubscriber client(TELEMETRY_TOPIC);

Point16 queryRatePos;
Point16 changeRatePos;

Format rateFormat("###/s");

float lastValue = NAN;

void onConnected()
{
   Serial.println("Telemetry: WebSocket Connected");
}

void onDisconnected()
{
   Serial.println("Telemetry: WebSocket Disconnected");
   delay(1000);
   Util::reset();
}

void onError(std::string msg)
{
   arduino.setTextSize(2);
   arduino.clearDisplay();
   arduino.display.setTextWrap(true);
   arduino.println(msg, Color::RED);
   Serial.println("Telemetry Error: " + String(msg.c_str()));
   Util::reset(10);
}

void onStarted()
{
   arduino.printlnR("OK", Color::VALUE);
   delay(1000);

   arduino.clearDisplay();
   arduino.setCursor(0, 0);
   arduino.setTextSize(3);
   arduino.println("Subscriber", Color::HEADING);
   arduino.moveCursorY(4);

   arduino.setTextSize(2);
   arduino.println("Topic: ", client.getTopic());

   Url url(client.getUrl().c_str());
   arduino.display.setTextWrap(true);
   arduino.println("Host: ", url.getHost(), Color::VALUE2);

   arduino.print("Query Rate: ", "---");
   queryRatePos = arduino.getCursor();
   arduino.println();

   arduino.print("Change Rate: ", "---");
   changeRatePos = arduino.getCursor();
   arduino.println();

   sw.start();
}

void onReceiveText(std::string msg)
{
   queryRate.tick();
}

void setup()
{
   SerialX::begin();
   arduino.begin();

   WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

   arduino.setTextSize(2);
   arduino.setCursorY(-arduino.charH());
   arduino.echoToSerial = true;
   arduino.println("Subscriber", Color::GRAY);

   arduino.println("Initializing", Color::HEADING2);
   arduino.moveCursorY(4);

   arduino.print("WiFi...", Color::LABEL);
   while (WiFi.status() != WL_CONNECTED)
   {
      arduino.print(".", Color::LABEL);
   }
   arduino.printlnR("OK", Color::VALUE);
   arduino.moveCursorY(1);

   arduino.print("WebSocket...", Color::LABEL);

   client.setCallbacks(onConnected, onDisconnected, nullptr, onReceiveText, onError, onStarted);
   client.beginSSL(TELEMETRY_HOST, TELEMETRY_PORT);
}

void loop()
{
   client.loop();

   if (!std::isnan(client.getValue()) && client.getValue() != lastValue)
   {
      lastValue = client.getValue();
      Serial.println(lastValue);
      changeRate.tick();
   }

   if (sw.elapsedMillis() > RATE_UPDATE_INTERVAL_MS)
   {
      arduino.setTextSize(2);

      arduino.setCursor(queryRatePos);
      arduino.println(queryRate.get(), rateFormat, Color::VALUE);

      arduino.setCursor(changeRatePos);
      arduino.println(changeRate.get(), rateFormat, Color::VALUE);

      sw.reset();
   }
}
