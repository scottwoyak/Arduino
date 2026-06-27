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

#include "Feather.h"
#include "RollingRate.h"
#include "SerialX.h"
#include "Stopwatch.h"
#include "TelemetryClient.h"
#include "Url.h"

#include "WiFiSettings.h"

constexpr const char* TELEMETRY_TOPIC = "Test";
// constexpr const char* TELEMETRY_TOPIC = "Waves/Lake";

constexpr unsigned long RATE_UPDATE_INTERVAL_MS = 1000;

Feather feather;
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
   feather.println("Subscriber", Color::HEADING);
   feather.moveCursorY(4);

   feather.setTextSize(2);
   feather.println("Topic: ", client.getTopic());

   Url url(client.getUrl().c_str());
   feather.display.setTextWrap(true);
   feather.println("Host: ", url.getHost(), Color::VALUE2);

   feather.print("Query Rate: ", "---");
   queryRatePos = feather.getCursor();
   feather.println();

   feather.print("Change Rate: ", "---");
   changeRatePos = feather.getCursor();
   feather.println();

   sw.start();
}

void onReceiveText(std::string msg)
{
   queryRate.tick();
}

void setup()
{
   SerialX::begin();
   feather.begin();

   WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

   feather.setTextSize(2);
   feather.setCursorY(-feather.charH());
   feather.echoToSerial = true;
   feather.println("Subscriber", Color::GRAY);

   feather.println("Initializing", Color::HEADING2);
   feather.moveCursorY(4);

   feather.print("WiFi...", Color::LABEL);
   while (WiFi.status() != WL_CONNECTED)
   {
      feather.print(".", Color::LABEL);
   }
   feather.printlnR("OK", Color::VALUE);
   feather.moveCursorY(1);

   feather.print("WebSocket...", Color::LABEL);

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
      feather.setTextSize(2);

      feather.setCursor(queryRatePos);
      feather.println(queryRate.get(), rateFormat, Color::VALUE);

      feather.setCursor(changeRatePos);
      feather.println(changeRate.get(), rateFormat, Color::VALUE);

      sw.reset();
   }
}
