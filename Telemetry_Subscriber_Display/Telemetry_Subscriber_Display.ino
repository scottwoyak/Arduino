//
// Telemetry Subscriber Display
//
// Subscribes to a topic on the TelemetryServer using TelemetryClient
// and displays the topic, server, and receive rate (how often values arrive from the
// server) on the display, along with a scatter plot of the received values.
//
// On disconnect the underlying WebSocket keeps retrying in the background, and the
// display is redrawn once the subscription is acknowledged again.
//
// Connects to the Raspberry Pi TelemetryServer, falling back to the production server if
// it can't be reached.
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

const auto VERSION = MakeVersion("1.0");
constexpr auto SKETCH_NAME = "Telemetry_Subscriber_Display";
constexpr uint32_t BAUD_RATE = 115200;
constexpr auto TOPIC = "Test";

TelemetryConfig TELEMETRY_CONFIG = {
   .topic = TOPIC,
   .primary = TELEMETRY_RASPBERRY_ENDPOINT,
   .fallback = TELEMETRY_PRODUCTION_ENDPOINT,
   .deviceToken = TELEMETRY_DEVICE_TOKEN,
   .clientToken = TELEMETRY_CLIENT_TOKEN,
};
constexpr uint32_t RATE_UPDATE_INTERVAL_MS = 1000;

SketchConfig SKETCH_CONFIG = {
   .sketchName = SKETCH_NAME,
   .version = VERSION,
   .cpuFrequencyMhz = 240,
   .enableOTA = true,
};

DeviceSketch sketch(SKETCH_CONFIG);

TelemetrySubscriber client(TELEMETRY_CONFIG, sketch.getStatus());
Timer rateDisplayTimer(RATE_UPDATE_INTERVAL_MS);
Table table(&sketch.arduino, 0, 0);

// ----------- Received Value Scatter Plot (bottom of display, 5 second rolling span)
constexpr unsigned long PLOT_SPAN_MS = 5000UL;
constexpr auto VALUE_FORMAT = "###.###";
ScatterPlot valuePlot(&sketch.arduino, Rect16{}, "##.#s", VALUE_FORMAT);
TimedScatterPlotSeries* valueSeries = valuePlot.createTimedSeries(PLOT_SPAN_MS);
constexpr uint8_t VALUE_SERIES_POINT_SIZE = 1;

bool needsInitialDisplay = true;

void setup()
{
   SerialX::begin(BAUD_RATE);

   valueSeries->pointSize = VALUE_SERIES_POINT_SIZE;

   client.onSample([](const std::string& topic, double value, int64_t dtMicros)
   {
      if (!needsInitialDisplay)
      {
         valueSeries->add(value);
      }
   });

   sketch.beginBanner();
   sketch.beginConnect();

   // Other sketch classes (PublisherSketch, ViewerSketch) print this topic line themselves.
   sketch.arduino.printlnInitStatus("Topic... ", std::string("\"") + TOPIC + "\"");
   client.connect(&sketch.arduino, sketch.getStatus());

   sketch.beginLogger();
   sketch.completeInitialization();
}

void loop()
{
   sketch.loop();
   client.loop();

   if (!client.isReady())
   {
      // Redraw the table/plot once the subscription (re)starts.
      needsInitialDisplay = true;
      return;
   }

   if (needsInitialDisplay)
   {
      sketch.arduino.clearDisplay();
      sketch.arduino.setCursor(0, 0);
      sketch.arduino.setTextSize(3);
      sketch.arduino.println("Subscriber", Color::HEADING);
      sketch.arduino.moveCursorY(4);

      sketch.arduino.setTextSize(2);
      table.clearRows();
      table.setPosition(0, sketch.arduino.getCursor().y);
      table.addRow("Topic", "                    ");
      table.addRow("Host", "                        ", Color::VALUE2);
      table.addRow("Rate", "###/s");

      table.setValue(0, TOPIC, Color::VALUE);
      table.setValue(1, client.getHost(), Color::VALUE2);
      table.setValueNone(2);
      table.draw();

      constexpr int16_t PLOT_TOP_PADDING_PX = 5;
      int16_t plotTop = table.getRect().bottom() + PLOT_TOP_PADDING_PX;
      valuePlot.setRect(0, plotTop, sketch.arduino.width(), sketch.arduino.height() - plotTop);
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

   if (rateDisplayTimer.ready())
   {
      table.setValue(2, client.getRate());
   }

   table.draw();

   valueSeries->updateWindow(millis());
   valuePlot.draw();
}
