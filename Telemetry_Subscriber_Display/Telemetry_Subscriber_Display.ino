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
// - Resets the device on disconnect or telemetry error.
//
// Uncomment TELEMETRY_LOCAL to use a local telemetry server instead of the remote.
// Hardware: Feather ESP32 with WiFi and TFT display.
//

// Uncomment to use local telemetry server instead of remote
#define TELEMETRY_LOCAL

#include <Arduino.h>

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif
#ifndef ARDUINO_NEOPIXEL_SUPPORTED
#error "This sketch requires a board with onboard NeoPixel LED support (e.g. Feather ESP32-S3 or Waveshare ESP32-S3-Zero)."
#endif

#include "ScatterPlot.h"
#include "SerialX.h"
#include "Status.h"
#include "Table.h"
#include "TelemetryClient.h"
#include "Timer.h"
#include "Url.h"

#include "WiFiSettings.h"

//constexpr const char* TELEMETRY_TOPIC = "Tests/Sin1";
constexpr const char* TELEMETRY_TOPIC = "Test";
//constexpr const char* TELEMETRY_TOPIC = "Waves/Lake";

constexpr unsigned long RATE_UPDATE_INTERVAL_MS = 1000;

Arduino arduino;
NeoPixelStatus status(&arduino.neoPixel);
Timer rateDisplayTimer(RATE_UPDATE_INTERVAL_MS);

constexpr const char* TOPIC_FORMAT = "                    ";
constexpr const char* HOST_FORMAT = "                        ";
constexpr const char* RATE_FORMAT = "###/s";
Table table(&arduino, 0, 0);

// ----------- Received Value Scatter Plot (bottom of display, 5 second rolling span)
constexpr unsigned long PLOT_SPAN_MS = 5000UL;
constexpr const char* VALUE_FORMAT = "###.###";
ScatterPlot valuePlot(&arduino, Rect16{}, "##.#s", VALUE_FORMAT);
TimedScatterPlotSeries* valueSeries = valuePlot.createTimedSeries(PLOT_SPAN_MS);
constexpr uint8_t VALUE_SERIES_POINT_SIZE = 1;
float lastPlottedValue = NAN;

TelemetryEventHandler telemetryHandler(&status, &arduino);
TelemetrySubscriber client(TELEMETRY_TOPIC, &status, &telemetryHandler);
bool needsInitialDisplay = true;

void setup()
{
   SerialX::begin();
   arduino.begin();
   status.begin();

   valueSeries->pointSize = VALUE_SERIES_POINT_SIZE;

   arduino.beginInit();
   arduino.initWifi(WIFI_SSID, WIFI_PASSWORD, &status);

   arduino.initClient("WebSocket", []() { client.beginSSL(TELEMETRY_HOST, TELEMETRY_PORT); }, &status);

#ifdef TELEMETRY_LOCAL
   arduino.println("Server", "Local");
#else
   arduino.println("Server", "Remote");
#endif
   arduino.println("Topic", TELEMETRY_TOPIC);

   arduino.clearLoggers();
}

void loop()
{
   client.loop();

   if (client.isStarted() && needsInitialDisplay)
   {
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

      constexpr int16_t PLOT_TOP_PADDING_PX = 5;
      int16_t plotTop = table.getRect().bottom() + PLOT_TOP_PADDING_PX;
      valuePlot.setRect(0, plotTop, arduino.width(), arduino.height() - plotTop);
      valuePlot.setYAxisFormat(VALUE_FORMAT);
      valuePlot.setShowXMinMaxValue(false);
      valuePlot.setShowXRangeValue(true);
      valuePlot.setShowYRangeValue(false);
      valuePlot.setYAxisMode(ScatterPlot::AxisMode::GROW_ONLY);
      valueSeries->showPoints = true;
      valueSeries->showLines = false;
      valuePlot.clear();

      rateDisplayTimer.reset();
      needsInitialDisplay = false;
   }

   if (!client.isStarted())
   {
      // waiting for the subscribe acknowledgement from the server; don't draw
      // any data until the topic has been officially started
      return;
   }

   if (rateDisplayTimer.ready())
   {
      table.setValue(2, client.getRate());
   }

   table.draw();

   float value = client.getValue();
   if (value != lastPlottedValue)
   {
      valueSeries->add(value);
      lastPlottedValue = value;
   }

   valueSeries->updateWindow(millis());
   valuePlot.draw();
}
