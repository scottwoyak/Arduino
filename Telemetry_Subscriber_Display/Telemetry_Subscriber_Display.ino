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

#include "DisplayTable.h"
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
DisplayTable table(&arduino, 0, 0);

float lastValue = NAN;
bool disconnected = false;
uint8_t lastCountdownSecs = 0;
std::string disconnectReason = "";

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
   disconnectReason = reason;
   lastCountdownSecs = 0;
   reconnectTimer.reset();
}

///
/// <summary>
/// Called when a telemetry client error occurs. Displays the error and resets the device.
/// </summary>
/// <param name="msg">Error message to display</param>
///
void onError(std::string msg)
{
   arduino.setTextSize(2);
   arduino.clearDisplay();
   arduino.display.setTextWrap(true);
   arduino.println(msg, Color::RED);
   Serial.println("Telemetry Error: " + String(msg.c_str()));
   Util::reset(10);
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
/// Draws a "Disconnected" message with a countdown until the next automatic
/// reconnect attempt, redrawing only when the displayed second changes.
/// </summary>
///
void drawReconnectCountdown()
{
   uint8_t secsLeft = static_cast<uint8_t>(ceil(reconnectTimer.remaining()));
   if (secsLeft == lastCountdownSecs)
   {
      return;
   }
   lastCountdownSecs = secsLeft;

   arduino.clearDisplay();
   arduino.setCursor(0, 0);
   arduino.setTextSize(3);
   arduino.println("Disconnected", Color::RED);
   arduino.moveCursorY(4);

   arduino.setTextSize(2);
   arduino.println(disconnectReason, Color::LABEL);
   arduino.moveCursorY(4);

   arduino.print("Reconnecting in ", Color::GREEN);
   arduino.println(secsLeft, Color::VALUE);
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

   arduino.print("WebSocket...", Color::GREEN);

   client.setCallbacks(onConnected, onDisconnected, nullptr, onReceiveText, onError, onStarted);
   client.beginSSL(TELEMETRY_HOST, TELEMETRY_PORT);
}

void loop()
{
   client.loop();

   if (disconnected)
   {
      drawReconnectCountdown();
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
