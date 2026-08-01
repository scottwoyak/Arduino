//
// Telemetry data subscriber with display feedback.
//
// Subscribes to a telemetry topic and displays received data along with
// query rate (WebSocket message rate) and change rate (data update frequency).
// Implements callback-based event handling for connection lifecycle and data flow.
//
// Uncomment TELEMETRY_LOCAL to use a local telemetry server instead of the remote.
// Hardware: Feather ESP32 with WiFi and TFT display.
//

// Uncomment to use local telemetry server instead of remote
#define TELEMETRY_LOCAL

#include <Arduino.h>
#include <WiFi.h>
#include <cmath>

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include "Table.h"
#include "DisplayValue.h"
#include "RollingRate.h"
#include "SerialX.h"
#include "Stopwatch.h"
#include "TelemetryClient.h"
#include "Timer.h"
#include "Url.h"

#include "WiFiSettings.h"

constexpr const char* TELEMETRY_TOPIC = "Test";
// constexpr const char* TELEMETRY_TOPIC = "Waves/Lake";

constexpr unsigned long RATE_UPDATE_INTERVAL_MS = 1000;
constexpr uint16_t RATE_NUM_SAMPLES = 100;
constexpr float RECONNECT_COUNTDOWN_SECS = 5.0f;

Arduino arduino;
Stopwatch sw(false);
TimerSecs reconnectTimer(RECONNECT_COUNTDOWN_SECS);

RollingRate queryRate(RATE_NUM_SAMPLES);
RollingRate changeRate(RATE_NUM_SAMPLES);

TelemetrySubscriber client(TELEMETRY_TOPIC);

constexpr const char* TOPIC_FORMAT = "                    ";
constexpr const char* HOST_FORMAT = "                        ";
constexpr const char* RATE_FORMAT = "###/s";
Table table(&arduino, 0, 0);

// ----------- Disconnected/Reconnect Display
constexpr uint8_t DISCONNECT_TEXT_SIZE = 2;
DisplayValue reasonLine(&arduino, Format(32, Format::Alignment::LEFT), DISCONNECT_TEXT_SIZE, DisplayValue::Alignment::LEFT);
DisplayValue statusLine(&arduino, Format(32, Format::Alignment::LEFT), DISCONNECT_TEXT_SIZE, DisplayValue::Alignment::LEFT);

float lastValue = NAN;
bool disconnected = false;
bool started = false;
uint8_t lastCountdownSecs = 0;
std::string disconnectReason = "";
std::string lastErrorMsg = "";

///
/// <summary>
/// Called when the WebSocket connection to the telemetry server is established.
/// </summary>
///
void onConnected()
{
   Serial.println("Telemetry: WebSocket Connected");
   disconnected = false;
}

///
/// <summary>
/// Called when the WebSocket connection to the telemetry server is lost. Shows a
/// reconnect countdown with the disconnect reason rather than resetting the device;
/// the WebSocket client retries the connection automatically.
/// </summary>
/// <param name="reason">The disconnect reason reported by TelemetryClient.</param>
///
void onDisconnected(std::string reason)
{
   Serial.println("Telemetry: WebSocket Disconnected: " + String(reason.c_str()));
   disconnected = true;
   started = false;
   lastValue = NAN;
   disconnectReason = reason;
   lastCountdownSecs = 0;
   reconnectTimer.reset();
}

///
/// <summary>
/// Called when a telemetry client error occurs (e.g. the topic hasn't been released yet
/// from a prior connection). Remembers the error so it can be shown above the reconnect
/// countdown; TelemetryClient automatically retries the start/subscribe request after a
/// short delay, so the device is not reset here.
/// </summary>
/// <param name="msg">Error message to display</param>
///
void onError(std::string msg)
{
   lastErrorMsg = msg;
   lastCountdownSecs = 0;
   Serial.println("Telemetry Error: " + String(msg.c_str()));
}

///
/// <summary>
/// Called once the telemetry connection is fully started. Draws the main display layout.
/// </summary>
///
void onStarted()
{
   arduino.printlnR("OK", Color::VALUE);
   delay(1000);

   lastErrorMsg.clear();

   arduino.clearDisplay();
   arduino.setCursor(0, 0);
   arduino.setTextSize(3);
   arduino.println("Subscriber", Color::HEADING);
   arduino.moveCursorY(4);

   arduino.setTextSize(2);
   table.setPosition(0, arduino.getCursor().y);
   table.addRow("Topic", TOPIC_FORMAT);
   table.addRow("Host", HOST_FORMAT, Color::VALUE2);
   table.addRow("Query Rate", RATE_FORMAT);
   table.addRow("Change Rate", RATE_FORMAT);

   Url url(client.getUrl().c_str());
   table.setValue(0, client.getTopic(), Color::VALUE);
   table.setValue(1, url.getHost(), Color::VALUE2);
   table.setNoValue(2);
   table.setNoValue(3);
   table.draw();

   sw.start();
   started = true;
}

///
/// <summary>
/// Called when a text message is received from the telemetry server.
/// </summary>
/// <param name="msg">Message payload (unused)</param>
///
void onReceiveText(std::string msg)
{
   queryRate.tick();
}

///
/// <summary>
/// Draws the disconnect reason (or last error, if any) and a countdown until the next
/// automatic reconnect/retry attempt, redrawing only when the displayed second changes.
/// Uses DisplayValue sprites so only these two lines are repainted, leaving the rest of
/// the display (e.g. any prior data) undisturbed.
/// </summary>
/// <param name="reasonText">The disconnect reason or error message to display.</param>
/// <param name="secsLeft">Seconds remaining until the next retry attempt.</param>
///
void drawReconnectCountdown(const std::string& reasonText, uint8_t secsLeft)
{
   if (secsLeft == lastCountdownSecs)
   {
      return;
   }
   lastCountdownSecs = secsLeft;

   reasonLine.draw(reasonText, Color::RED);

   std::string statusText = "Reconnecting in " + std::to_string(secsLeft) + "s";
   statusLine.draw(statusText, Color::LIME);
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

   arduino.print("WiFi...", Color::BLUE);
   while (WiFi.status() != WL_CONNECTED)
   {
      arduino.print(".", Color::BLUE);
   }
   arduino.printlnR("OK", Color::VALUE);
   arduino.moveCursorY(1);

   arduino.print("WebSocket...", Color::LIME);

   arduino.setTextSize(DISCONNECT_TEXT_SIZE);
   int16_t reasonY = arduino.height() / 3;
   reasonLine.setPosition(0, reasonY);
   statusLine.setPosition(0, reasonY + reasonLine.height() + 4);

   client.setCallbacks(onConnected, onDisconnected, nullptr, onReceiveText, onError, onStarted);
   client.beginSSL(TELEMETRY_HOST, TELEMETRY_PORT);
}

void loop()
{
   client.loop();

   if (disconnected)
   {
      uint8_t secsLeft = static_cast<uint8_t>(ceil(reconnectTimer.remaining()));
      drawReconnectCountdown(disconnectReason, secsLeft);
      return;
   }

   if (client.isStartRetryPending())
   {
      uint8_t secsLeft = static_cast<uint8_t>(ceil(client.getStartRetryRemainingSecs()));
      drawReconnectCountdown(lastErrorMsg, secsLeft);
      return;
   }

   if (!started)
   {
      // waiting for the subscribe acknowledgement from the server; don't draw
      // any data until the topic has been officially started
      return;
   }

   if (!std::isnan(client.getValue()) && client.getValue() != lastValue)
   {
      lastValue = client.getValue();
      Serial.println(lastValue);
      changeRate.tick();
   }

   if (sw.elapsedMillis() > RATE_UPDATE_INTERVAL_MS)
   {
      table.setValue(2, queryRate.get());
      table.setValue(3, changeRate.get());
      sw.reset();
   }

   table.draw();
}
