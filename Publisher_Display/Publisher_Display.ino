/// <summary>
/// Telemetry data publisher with display feedback.
/// </summary>
/// <remarks>
/// Publishes sinusoidal test data to a telemetry server via WebSocket connection.
/// Displays connection status, topic, host, and message rate on a TFT display.
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
#include <numbers>

#include "Feather.h"
#include "RollingRate.h"
#include "SerialX.h"
#include "Stopwatch.h"
#include "TelemetryClient.h"
#include "Timer.h"
#include "Url.h"

#include "WiFiSettings.h"

constexpr const char* TELEMETRY_TOPIC = "Test";
constexpr unsigned long PUBLISH_INTERVAL_MS = 100;
constexpr unsigned long RATE_UPDATE_INTERVAL_MS = 1000;
constexpr unsigned long SINUSOID_PERIOD_US = 2000000;

Feather feather;
Stopwatch sw(false);
Timer publishTimer(PUBLISH_INTERVAL_MS);
RollingRate rate(100);

TelemetryPublisher client(TELEMETRY_TOPIC, 3);
Point16 ratePos;

Format rateFormat("###/s");

/// <summary>
/// Generates a sinusoidal test value for publishing.
/// </summary>
/// <returns>Sine value oscillating between -1 and 1 with a fixed period</returns>
float getValue()
{
   float x = 2 * std::numbers::pi * (((float)(micros() % SINUSOID_PERIOD_US)) / SINUSOID_PERIOD_US);
   return std::sin(x);
}

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

void onText(std::string payload)
{
   rate.tick();
}

void onError(std::string msg)
{
   feather.setTextSize(2);
   feather.clearDisplay();
   feather.display.setTextWrap(true);
   feather.println(msg, Color::RED);
   Serial.println("Telemetry Error: " + String(msg.c_str()));
   Util::reset(10);
}

void onStarted()
{
   feather.printlnR("OK", Color::VALUE);
   delay(1000);

   feather.clearDisplay();
   feather.setCursor(0, 0);
   feather.setTextSize(3);
   feather.println("Publisher", Color::HEADING);
   feather.moveCursorY(4);

   feather.setTextSize(2);
   feather.println("Topic: ", client.getTopic());

   Url url(client.getUrl().c_str());
   feather.display.setTextWrap(true);
   feather.println(" Host: ", url.getHost(), Color::VALUE2);

   feather.print("Rate: ", "---");
   ratePos = feather.getCursor();
   feather.println();

   rate.reset();
   sw.start();
}

void setup()
{
   SerialX::begin();
   feather.begin();

   WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

   feather.setTextSize(2);
   feather.setCursorY(-feather.charH());
   feather.println("Publisher", Color::GRAY);

   feather.setCursor(0, 0);
   feather.println("Initializing", Color::HEADING2);
   feather.moveCursorY(4);

   feather.print("WiFi...", Color::LABEL);
   while (WiFi.status() != WL_CONNECTED)
   {
      feather.print(".", Color::LABEL);
   }
   feather.printlnR("OK", Color::VALUE);

   feather.print("WebSocket...", Color::LABEL);

   client.setCallbacks(onConnected, onDisconnected, nullptr, onText, onError, onStarted);
   client.beginSSL(TELEMETRY_HOST, TELEMETRY_PORT);
}

void loop()
{
   if (publishTimer.ready())
   {
      float value = getValue();
      client.setValue(value);
   }

   client.loop();

   if (sw.elapsedMillis() > RATE_UPDATE_INTERVAL_MS)
   {
      feather.setCursor(ratePos);
      feather.println(rate.get(), rateFormat, Color::VALUE);
      sw.reset();
   }
}
