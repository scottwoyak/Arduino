//
// Telemetry data publisher with display feedback.
//
// Publishes mock sensor test data to a telemetry server via WebSocket connection.
// Displays connection status, topic, host, and message rate on a TFT display.
// Implements callback-based event handling for connection lifecycle and data flow.
//
// Uncomment TELEMETRY_LOCAL to use a local telemetry server instead of the remote.
// Change TEST_SENSOR_TYPE below to select a different mock sensor (see TestSensor.h for options).
// Hardware: Feather ESP32 with WiFi and TFT display.
//

// Uncomment to use local telemetry server instead of remote
#define TELEMETRY_LOCAL

#include <Arduino.h>
#include <WiFi.h>

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include "Table.h"
#include "RollingRate.h"
#include "ScatterPlot.h"
#include "SerialX.h"
#include "Stopwatch.h"
#include "TelemetryClient.h"

// Selects the mock sensor used to generate published test data.
#define TEST_SENSOR_TYPE WaveTestSensor
#include "TestSensor.h"

#include "Timer.h"
#include "Url.h"

#include "WiFiSettings.h"

// ----------- Telemetry
constexpr const char* TELEMETRY_TOPIC = "Test";
constexpr unsigned long PUBLISH_INTERVAL_MS = 100;
constexpr uint8_t TELEMETRY_DECIMAL_PLACES = 3;
TelemetryPublisher client(TELEMETRY_TOPIC, TELEMETRY_DECIMAL_PLACES);

// ----------- The Board
Arduino arduino;

// ----------- Sensor
TestSensor sensor;
Timer publishTimer(PUBLISH_INTERVAL_MS);

// ----------- Display Items
constexpr unsigned long RATE_UPDATE_INTERVAL_MS = 1000;
constexpr uint16_t RATE_NUM_SAMPLES = 10;
Stopwatch sw(false);
RollingRate rate(RATE_NUM_SAMPLES);
constexpr const char* TOPIC_FORMAT = "                    ";
constexpr const char* HOST_FORMAT = "                        ";
constexpr const char* RATE_FORMAT = "###/s";
Table table(&arduino, 0, 0);

// ----------- Published Value Scatter Plot (bottom of display, 5 second rolling span)
constexpr unsigned long PLOT_SPAN_MS = 5000UL;
ScatterPlot valuePlot(&arduino, Rect16{}, "##.#s", "###.###");
TimedScatterPlotSeries* valueSeries = valuePlot.createTimedSeries(PLOT_SPAN_MS);
constexpr uint8_t VALUE_SERIES_POINT_SIZE = 1;

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
void onDisconnected(std::string reason)
{
   Serial.println("Telemetry: WebSocket Disconnected: " + String(reason.c_str()));
   delay(1000);
   Util::reset();
}

///
/// <summary>
/// Called when a text message is received from the telemetry server.
/// </summary>
/// <param name="payload">Message payload (unused)</param>
///
void onText(std::string payload)
{
   rate.tick();
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
   arduino.println("Publisher", Color::HEADING);
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

   constexpr int16_t PLOT_TOP_PADDING_PX = 5;
   int16_t plotTop = table.getRect().bottom() + PLOT_TOP_PADDING_PX;
   valuePlot.setRect(0, plotTop, arduino.width(), arduino.height() - plotTop);
   valuePlot.setYAxisFormat(sensor.getFormatStr().c_str());
   valuePlot.setShowXMinMaxValue(false);
   valuePlot.setShowXRangeValue(true);
   valuePlot.setShowYRangeValue(false);
   valuePlot.setYAxisMode(ScatterPlot::AxisMode::GROW_ONLY);
   valueSeries->showPoints = true;
   valueSeries->showLines = false;
   valuePlot.clear();

   rate.reset();
   sw.start();
}

void setup()
{
   SerialX::begin();
   arduino.begin();

   valueSeries->pointSize = VALUE_SERIES_POINT_SIZE;

   WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

   arduino.setTextSize(2);
   arduino.setCursorY(-arduino.charH());
   arduino.println("Publisher", Color::GRAY);

   arduino.setCursor(0, 0);
   arduino.println("Initializing", Color::HEADING2);
   arduino.moveCursorY(4);

   arduino.print("WiFi...", Color::LABEL);
   while (WiFi.status() != WL_CONNECTED)
   {
      arduino.print(".", Color::LABEL);
   }
   arduino.printlnR("OK", Color::VALUE);

   arduino.print("WebSocket...", Color::LABEL);

   client.setCallbacks(onConnected, onDisconnected, nullptr, onText, onError, onStarted);
   client.beginSSL(TELEMETRY_HOST, TELEMETRY_PORT);

   sensor.begin();
}

void loop()
{
   if (publishTimer.ready())
   {
      float value = sensor.get();
      client.setValue(value);
      valueSeries->add(value);
   }

   client.loop();

   if (sw.elapsedMillis() > RATE_UPDATE_INTERVAL_MS)
   {
      table.setValue(2, rate.get());
      sw.reset();
   }

   table.draw();

   valueSeries->updateWindow(millis());
   valuePlot.draw();
}
