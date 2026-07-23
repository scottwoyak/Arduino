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
#include "Url.h"

#include "WiFiSettings.h"

constexpr const char* TELEMETRY_TOPIC = "Test";
// constexpr const char* TELEMETRY_TOPIC = "Waves/Lake";

constexpr unsigned long RATE_UPDATE_INTERVAL_MS = 1000;
constexpr uint16_t RATE_NUM_SAMPLES = 100;

Arduino arduino;
Stopwatch sw(false);

RollingRate queryRate(RATE_NUM_SAMPLES);
RollingRate changeRate(RATE_NUM_SAMPLES);

TelemetrySubscriber client(TELEMETRY_TOPIC);

Format topicFormat(20);
Format hostFormat(24);
Format rateFormat("###/s");
DisplayTable table(&arduino, 0, 0);

float lastValue = NAN;

///
/// <summary>
/// Called when the WebSocket connection to the telemetry server is established.
/// </summary>
///
void onConnected()
{
   Serial.println("Telemetry: WebSocket Connected");
}

///
/// <summary>
/// Called when the WebSocket connection to the telemetry server is lost.
/// Restarts the device so it can reconnect from a clean state.
/// </summary>
///
void onDisconnected()
{
   Serial.println("Telemetry: WebSocket Disconnected");
   delay(1000);
   Util::reset();
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
   table.addRow("Topic", topicFormat, Color::LABEL, Color::VALUE);
   table.addRow("Host", hostFormat, Color::LABEL, Color::VALUE2);
   table.addRow("Query Rate", rateFormat, Color::LABEL, Color::VALUE);
   table.addRow("Change Rate", rateFormat, Color::LABEL, Color::VALUE);

   Url url(client.getUrl().c_str());
   table.setValue(0, client.getTopic(), Color::VALUE);
   table.setValue(1, url.getHost(), Color::VALUE2);
   table.setNoValue(2, Color::VALUE);
   table.setNoValue(3, Color::VALUE);
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
      table.setValue(2, queryRate.get());
      table.setValue(3, changeRate.get());
      sw.reset();
   }

   table.draw();
}
