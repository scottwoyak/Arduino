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

// ----------- The Board
Arduino arduino;
NeoPixelStatus status(&arduino.neoPixel);

// ----------- Sensor
TestSensor sensor;
Timer publishTimer(PUBLISH_INTERVAL_MS);

// ----------- Display Items
constexpr unsigned long RATE_UPDATE_INTERVAL_MS = 1000;
Timer rateDisplayTimer(RATE_UPDATE_INTERVAL_MS);
constexpr const char* TOPIC_FORMAT = "                    ";
constexpr const char* HOST_FORMAT = "                        ";
constexpr const char* RATE_FORMAT = "###/s";
Table table(&arduino, 0, 0);

// ----------- Published Value Scatter Plot (bottom of display, 5 second rolling span)
constexpr unsigned long PLOT_SPAN_MS = 5000UL;
ScatterPlot valuePlot(&arduino, Rect16{}, "##.#s", "###.###");
TimedScatterPlotSeries* valueSeries = valuePlot.createTimedSeries(PLOT_SPAN_MS);
constexpr uint8_t VALUE_SERIES_POINT_SIZE = 1;

TelemetryEventHandler telemetryHandler(&status, &arduino);
TelemetryPublisher client(TELEMETRY_TOPIC, TELEMETRY_DECIMAL_PLACES, &status, &telemetryHandler);
bool needsInitialDisplay = true;

void setup()
{
   SerialX::begin();
   arduino.begin();
   status.begin();
   status.setStatus(Status::STARTED);

   valueSeries->pointSize = VALUE_SERIES_POINT_SIZE;

   arduino.beginInit();

   arduino.initWifi(WIFI_SSID, WIFI_PASSWORD, &status);

   status.setStatus(Status::WEB_CONNECTING);
   arduino.initClient("WebSocket", []() { client.beginSSL(TELEMETRY_HOST, TELEMETRY_PORT); });

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

   if (client.isStarted() && needsInitialDisplay)
   {
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

      rateDisplayTimer.reset();
      needsInitialDisplay = false;
   }

   if (rateDisplayTimer.ready())
   {
      table.setValue(2, client.getRate());
   }

   table.draw();

   valueSeries->updateWindow(millis());
   valuePlot.draw();
}
