//
// Telemetry data publisher with display feedback.
//
// Publishes mock sensor test data to a telemetry server via WebSocket connection,
// using a Publisher. This sketch has no enclosure and does not upload any
// sensor/enclosure values to InfluxDB.
// Displays connection status, topic, host, and message rate on a TFT display.
//
// Uncomment TELEMETRY_LOCAL to use a local telemetry server instead of the remote.
// Change TEST_SENSOR_TYPE below to select a different mock sensor (see TestSensor.h for options).
// Hardware: Feather ESP32 with WiFi and TFT display.
//

// Uncomment to use local telemetry server instead of remote
//#define TELEMETRY_LOCAL

#include <Arduino.h>

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif
#ifndef ARDUINO_NEOPIXEL_SUPPORTED
#error "This sketch requires a board with onboard NeoPixel LED support (e.g. Feather ESP32-S3 or Waveshare ESP32-S3-Zero)."
#endif

#include "ScatterPlot.h"
#include "Table.h"
#include "WiFiSettings.h"

// Selects the mock sensor used to generate published test data.
#define TEST_SENSOR_TYPE WaveTestSensor
#include "TestSensor.h"

#include "Publisher.h"

Arduino arduino;
TestSensor sensor;

TelemetryConfig TELEMETRY_CONFIG = {
   .topic = "Test",
   .decimals = 3,
};

SketchConfig PUBLISHER_CONFIG = {
   .sketchName = "Publisher",
   .telemetry = TELEMETRY_CONFIG,
};

Publisher publisher(&arduino, PUBLISHER_CONFIG);

// ----------- Display Items
constexpr unsigned long RATE_UPDATE_INTERVAL_MS = 1000;
Timer rateDisplayTimer(RATE_UPDATE_INTERVAL_MS);
Table table(&arduino, 0, 0);

// ----------- Published Value Scatter Plot (bottom of display, 5 second rolling span)
constexpr unsigned long PLOT_SPAN_MS = 5000UL;
ScatterPlot valuePlot(&arduino, Rect16{}, "##.#s", "###.###");
TimedScatterPlotSeries* valueSeries = valuePlot.createTimedSeries(PLOT_SPAN_MS);
constexpr uint8_t VALUE_SERIES_POINT_SIZE = 1;

// Mirrors PUBLISHER_CONFIG.telemetry.publishIntervalMs so the plot is sampled at the same rate
// the telemetry value is published.
Timer plotSampleTimer(PUBLISHER_CONFIG.telemetry.publishIntervalMs);
bool needsInitialDisplay = true;

void setup()
{
   valueSeries->pointSize = VALUE_SERIES_POINT_SIZE;

   publisher.addSensor("Test Sensor", []() { sensor.begin(); return true; });
   publisher.setValueSource([]() { return sensor.get(); });

   publisher.begin();
}

void loop()
{
   if (plotSampleTimer.ready())
   {
      valueSeries->add(sensor.get());
   }

   publisher.loop();

   TelemetryPublisher* client = publisher.client();

   if (client->isStarted() && needsInitialDisplay)
   {
      arduino.clearDisplay();
      arduino.setCursor(0, 0);
      arduino.setTextSize(3);
      arduino.println("Publisher", Color::HEADING);
      arduino.moveCursorY(4);

      arduino.setTextSize(2);
      table.setPosition(0, arduino.getCursor().y);
      table.addRow("Topic", "                    ");
      table.addRow("Host", "                        ", Color::VALUE2);
      table.addRow("Rate", "###/s");

      #ifdef TELEMETRY_LOCAL
      constexpr auto HOST_LABEL = "local";
#else
      constexpr auto HOST_LABEL = "remote";
#endif
      table.setValue(0, client->getTopic(), Color::VALUE);
      table.setValue(1, HOST_LABEL, Color::VALUE2);
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
      table.setValue(2, client->getRate());
   }

   table.draw();

   valueSeries->updateWindow(millis());
   valuePlot.draw();
}
