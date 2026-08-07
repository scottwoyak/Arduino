//
// Telemetry Subscriber Display
//
// Subscribes to a telemetry topic over a WebSocket connection and displays the topic,
// host, and query rate (how often values can be retrieved from the server) on the
// display.
//
// Behavior:
// - Connects to WiFi, then opens a WebSocket connection to the telemetry server and
//   subscribes to the configured topic.
// - Shows a reconnect countdown with the disconnect reason if the connection drops.
// - Resets the device on telemetry error.
//
// Uncomment TELEMETRY_LOCAL to use a local telemetry server instead of the remote.
// Hardware: Feather ESP32 with WiFi and TFT display.
//

// Uncomment to use local telemetry server instead of remote
#define TELEMETRY_LOCAL

#include <Arduino.h>
#include <cmath>

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif
#ifndef ARDUINO_LED_SUPPORTED
#error "This sketch requires a board with onboard NeoPixel LED support (e.g. Feather ESP32-S3 or Waveshare ESP32-S3-Zero)."
#endif

#include "Table.h"
#include "DisplayValue.h"
#include "TimedRate.h"
#include "SerialX.h"
#include "Status.h"
#include "Stopwatch.h"
#include "TelemetryClient.h"
#include "Timer.h"
#include "Url.h"

#include "WiFiSettings.h"

constexpr const char* TELEMETRY_TOPIC = "Tests/Sin1";
// constexpr const char* TELEMETRY_TOPIC = "Test";
// constexpr const char* TELEMETRY_TOPIC = "Waves/Lake";

constexpr unsigned long RATE_UPDATE_INTERVAL_MS = 1000;
constexpr float RECONNECT_COUNTDOWN_SECS = 5.0f;

Arduino arduino;
NeoPixelStatus status(&arduino.neoPixel);
Stopwatch sw(false);
TimerSecs reconnectTimer(RECONNECT_COUNTDOWN_SECS);

TimedRate rate(5000);

TelemetrySubscriber client(TELEMETRY_TOPIC);

constexpr const char* TOPIC_FORMAT = "                    ";
constexpr const char* HOST_FORMAT = "                        ";
constexpr const char* RATE_FORMAT = "###/s";
Table table(&arduino, 0, 0);

// ----------- Disconnected/Reconnect Display
constexpr uint8_t DISCONNECT_TEXT_SIZE = 2;
DisplayValue reasonLine(&arduino, Format(32, Format::Alignment::LEFT), DISCONNECT_TEXT_SIZE, DisplayValue::Alignment::LEFT);
DisplayValue statusLine(&arduino, Format(32, Format::Alignment::LEFT), DISCONNECT_TEXT_SIZE, DisplayValue::Alignment::LEFT);

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
   disconnectReason = reason;
   lastCountdownSecs = 0;
   reconnectTimer.reset();
}

///
/// <summary>
/// Called when a telemetry client error occurs (e.g. the topic hasn't been released yet
/// from a prior connection). Resets the device after a short delay so it can attempt a
/// fresh connection/handshake.
/// </summary>
/// <param name="msg">Error message to display</param>
///
void onError(std::string msg)
{
   lastErrorMsg = msg;
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
   status.setStatus(Status::READY);
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
   table.addRow("Rate", RATE_FORMAT);

   Url url(client.getUrl().c_str());
   table.setValue(0, client.getTopic(), Color::VALUE);
   table.setValue(1, url.getHost(), Color::VALUE2);
   table.setValueNone(2);
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
   rate.tick();
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
   status.begin();
   status.setStatus(Status::STARTED);

   arduino.printHeader("Initializing");
   arduino.initWifi(WIFI_SSID, WIFI_PASSWORD, &status);
   arduino.setTextSize(DISCONNECT_TEXT_SIZE);
   int16_t reasonY = arduino.height() / 3;
   reasonLine.setPosition(0, reasonY);
   statusLine.setPosition(0, reasonY + reasonLine.height() + 4);

   client.setCallbacks(onConnected, onDisconnected, nullptr, onReceiveText, onError, onStarted);
   arduino.beginClient("WebSocket", []() { client.beginSSL(TELEMETRY_HOST, TELEMETRY_PORT); }, &status);
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

   if (!started)
   {
      // waiting for the subscribe acknowledgement from the server; don't draw
      // any data until the topic has been officially started
      return;
   }

   if (sw.elapsedMillis() > RATE_UPDATE_INTERVAL_MS)
   {
      table.setValue(2, rate.get());
      sw.reset();
   }

   table.draw();
}
