//
// Telemetry data publisher with display feedback.
//
// Publishes mock sensor test data to the DeviceHub TelemetryServer using
// TelemetryClient. Displays the topic, server, and message rate on a TFT display,
// along with a scatter plot of the published values.
//
// Change the TELEMETRY_SERVER_* define below to select the server.
// Change TEST_SENSOR_TYPE below to select a different mock sensor (see TestSensor.h for options).
// Hardware: Feather ESP32 with WiFi and TFT display.
//

#include <Arduino.h>

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include "ScatterPlot.h"
#include "SerialX.h"
#include "Table.h"
#include "TelemetryClient.h"
#include "Timer.h"
#include "WiFiSettings.h"

// Selects the mock sensor used to generate published test data.
#define TEST_SENSOR_TYPE WaveTestSensor
#include "TestSensor.h"

// Selects the server: TELEMETRY_SERVER_LOCAL, TELEMETRY_SERVER_RASPBERRY or TELEMETRY_SERVER_PRODUCTION.
#define TELEMETRY_SERVER_RASPBERRY

#if defined(TELEMETRY_SERVER_LOCAL)
constexpr auto SERVER_HOST = TELEMETRY_SERVER_LOCAL_HOST;
constexpr uint16_t SERVER_PORT = TELEMETRY_SERVER_LOCAL_PORT;
constexpr bool SERVER_USE_TLS = TELEMETRY_SERVER_LOCAL_USE_TLS;
#elif defined(TELEMETRY_SERVER_RASPBERRY)
constexpr auto SERVER_HOST = TELEMETRY_SERVER_RASPBERRY_HOST;
constexpr uint16_t SERVER_PORT = TELEMETRY_SERVER_RASPBERRY_PORT;
constexpr bool SERVER_USE_TLS = TELEMETRY_SERVER_RASPBERRY_USE_TLS;
#else
constexpr auto SERVER_HOST = TELEMETRY_SERVER_PRODUCTION_HOST;
constexpr uint16_t SERVER_PORT = TELEMETRY_SERVER_PRODUCTION_PORT;
constexpr bool SERVER_USE_TLS = TELEMETRY_SERVER_PRODUCTION_USE_TLS;
#endif

constexpr uint32_t BAUD_RATE = 115200;
constexpr auto TOPIC = "Test";
constexpr uint32_t PUBLISH_INTERVAL_MS = 10;
constexpr uint32_t RATE_UPDATE_INTERVAL_MS = 1000;

Arduino arduino;
TestSensor sensor;
TelemetryClient client;
Timer publishTimer(PUBLISH_INTERVAL_MS);
Timer rateDisplayTimer(RATE_UPDATE_INTERVAL_MS);
Table table(&arduino, 0, 0);

// ----------- Published Value Scatter Plot (bottom of display, 5 second rolling span)
constexpr unsigned long PLOT_SPAN_MS = 5000UL;
ScatterPlot valuePlot(&arduino, Rect16{}, "##.#s", "###.###");
TimedScatterPlotSeries* valueSeries = valuePlot.createTimedSeries(PLOT_SPAN_MS);
constexpr uint8_t VALUE_SERIES_POINT_SIZE = 1;

bool needsInitialDisplay = true;

void setup()
{
   SerialX::begin(BAUD_RATE);
   arduino.begin();

   valueSeries->pointSize = VALUE_SERIES_POINT_SIZE;

   arduino.beginInit();
   arduino.initWifi(WIFI_SSID, WIFI_PASSWORD, nullptr, false);
   sensor.begin();
   client.setToken(TELEMETRY_DEVICE_TOKEN);
#if defined(TELEMETRY_SERVER_LOCAL) || defined(TELEMETRY_SERVER_RASPBERRY)
   if (TELEMETRY_SERVER_PRODUCTION_ENABLED)
   {
      client.setFallbackEndpoint(
         TELEMETRY_SERVER_PRODUCTION_HOST,
         TELEMETRY_SERVER_PRODUCTION_PORT,
         TELEMETRY_SERVER_PRODUCTION_USE_TLS);
   }
#endif
   client.beginPublisher(SERVER_HOST, SERVER_PORT, SERVER_USE_TLS);

   arduino.printlnInitStatus("Server", SERVER_HOST);
   arduino.printlnInitStatus("Topic", TOPIC);
}

void loop()
{
   client.loop();

   if (publishTimer.ready())
   {
      float value = sensor.get();
      client.publish(TOPIC, value);

      if (!needsInitialDisplay)
      {
         valueSeries->add(value);
      }
   }

   if (!client.isReady())
   {
      // Redraw the table/plot once the connection (re)starts.
      needsInitialDisplay = true;
      return;
   }

   if (needsInitialDisplay)
   {
      arduino.clearDisplay();
      arduino.setCursor(0, 0);
      arduino.setTextSize(3);
      arduino.println("Publisher", Color::HEADING);
      arduino.moveCursorY(4);

      arduino.setTextSize(2);
      table.clearRows();
      table.setPosition(0, arduino.getCursor().y);
      table.addRow("Topic", "                    ");
      table.addRow("Host", "                        ", Color::VALUE2);
      table.addRow("Rate", "###/s");

      table.setValue(0, TOPIC, Color::VALUE);
      table.setValue(1, SERVER_HOST, Color::VALUE2);
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
