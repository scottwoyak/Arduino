//
// Telemetry data publisher with display feedback.
//
// Publishes mock sensor test data to the TelemetryServer using
// TelemetryClient. Displays the topic, server, and message rate on a TFT display,
// along with a scatter plot of the published values.
//
// Connects to the Raspberry Pi TelemetryServer, falling back to the production server if
// it can't be reached.
// Hold buttonA to suspend drawing the plot, which maximizes the publish rate.
// Change TEST_SENSOR_TYPE
// Hardware: Feather ESP32 with WiFi and TFT display.
//

#include <Arduino.h>

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#ifndef ARDUINO_BUTTON_A_SUPPORTED
#error "This sketch requires a board with buttonA."
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

TelemetryConfig TELEMETRY_CONFIG = {
   .topic = TOPIC,
   .decimals = 3,
   .primary = TELEMETRY_RASPBERRY_ENDPOINT,
   .fallback = TELEMETRY_PRODUCTION_ENDPOINT,
   .deviceToken = TELEMETRY_DEVICE_TOKEN,
   .clientToken = TELEMETRY_CLIENT_TOKEN,
};
constexpr uint32_t PUBLISH_INTERVAL_MS = 10;
constexpr uint32_t RATE_UPDATE_INTERVAL_MS = 1000;
constexpr unsigned long PLOT_SPAN_MS = 5000UL;

SketchConfig SKETCH_CONFIG = {
   .sketchName = SKETCH_NAME,
   .version = VERSION,
   .cpuFrequencyMhz = 240,
   .enableOTA = true,
};

DeviceSketch sketch(SKETCH_CONFIG);

TestSensor sensor;
TelemetryPublisher client(TELEMETRY_CONFIG, sketch.getStatus());
Timer publishTimer(PUBLISH_INTERVAL_MS);
Timer rateDisplayTimer(RATE_UPDATE_INTERVAL_MS);
Table table(&sketch.arduino, 0, 0);

// ----------- Published Value Scatter Plot (bottom of display, 5 second rolling span)
ScatterPlot valuePlot(&sketch.arduino, Rect16{}, "##.#s", "###.###");
TimedScatterPlotSeries* valueSeries = valuePlot.createTimedSeries(PLOT_SPAN_MS);
constexpr uint8_t VALUE_SERIES_POINT_SIZE = 1;

bool needsInitialDisplay = true;
bool plotNeedsDraw = false;

void setup()
{
   SerialX::begin(BAUD_RATE);

   valueSeries->pointSize = VALUE_SERIES_POINT_SIZE;

   sketch.begin();
   sensor.begin();

   // Other sketch classes (PublisherSketch, ViewerSketch) print this topic line themselves.
   sketch.arduino.printlnInitStatus("Topic... ", std::string("\"") + TOPIC + "\"");
   client.connect(&sketch.arduino, sketch.getStatus());

   sketch.completeInitialization();
}

void loop()
{
   sketch.loop();

   client.loop();

   if (publishTimer.ready())
   {
      float value = sensor.get();
      client.publish(TOPIC, value);

      if (!needsInitialDisplay)
      {
         valueSeries->add(value);
         plotNeedsDraw = true;
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
      sketch.arduino.clearDisplay();
      sketch.arduino.setCursor(0, 0);
      sketch.arduino.setTextSize(3);
      sketch.arduino.println("Publisher", Color::HEADING);
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

   // Drawing the plot is by far the most expensive step in loop(), so only redraw it when a
   // new point has been added. Holding buttonA suspends drawing entirely to show the maximum
   // publish rate.
   if (!sketch.arduino.buttonA.isPressed() && plotNeedsDraw)
   {
      plotNeedsDraw = false;
      valuePlot.draw();
   }
}
