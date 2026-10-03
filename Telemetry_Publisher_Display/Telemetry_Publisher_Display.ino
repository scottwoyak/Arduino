//
// Telemetry data publisher with display feedback.
//
// Publishes mock sensor test data to the TelemetryServer using
// TelemetryClient. Displays the topic, server, and message rate on a TFT display,
// along with a scatter plot of the published values.
//
// Connects to the Raspberry Pi TelemetryServer, falling back to the production server if
// it can't be reached.
// Change TEST_SENSOR_TYPE
// Hardware: Feather ESP32 with WiFi and TFT display.
//

#include <Arduino.h>

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include "DeviceSketch.h"
#include "LibraryVersion.h"
#include "ScatterPlot.h"
#include "SerialX.h"
#include "Table.h"
#include "TelemetryClient.h"
#include "Timer.h"
#include "WiFiSettings.h"

// Selects the mock sensor used to generate published test data.
#define TEST_SENSOR_TYPE WaveTestSensor
#include "TestSensor.h"

const auto VERSION = MakeVersion("1.0");
constexpr auto SKETCH_NAME = "Telemetry_Publisher_Display";
constexpr uint32_t BAUD_RATE = 115200;
constexpr auto TOPIC = "Test";
constexpr uint32_t PUBLISH_INTERVAL_MS = 10;
constexpr uint32_t RATE_UPDATE_INTERVAL_MS = 1000;

Arduino arduino;
DeviceSketch device(&arduino, { .sketchName = SKETCH_NAME, .version = VERSION, .enableOTA = true });
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

   device.beginBanner();
   device.beginConnect();
   sensor.begin();
   client.setToken(TELEMETRY_DEVICE_TOKEN);
   client.setFallbackEndpoint(TELEMETRY_SERVER_PRODUCTION_HOST, TELEMETRY_SERVER_PRODUCTION_PORT, TELEMETRY_SERVER_PRODUCTION_USE_TLS);
   client.beginPublisher(TELEMETRY_SERVER_RASPBERRY_HOST, TELEMETRY_SERVER_RASPBERRY_PORT, TELEMETRY_SERVER_RASPBERRY_USE_TLS);

   arduino.printlnInitStatus("Topic", TOPIC);

   device.beginLogger();
}

void loop()
{
   device.loop();
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
      table.setValue(1, client.getUrl(), Color::VALUE2);
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
