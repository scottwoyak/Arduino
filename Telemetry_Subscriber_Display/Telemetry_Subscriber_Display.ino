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

// Comment out to stop printing each received sample (timestamp in ms, value) to Serial.
#define PRINT_SAMPLES

// Declares which VLW font sizes this sketch actually uses, so only the needed font data
// is compiled in. TEXT_SIZE_2 and TEXT_SIZE_3 are always included regardless (see
// ArduinoWithDisplay.h), and this sketch only ever calls setTextSize(2) and setTextSize(3).
#define TEXT_SIZES_CUSTOM

#include <Arduino.h>
#include <array>
#include <string>

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#include "WrongBoard.h"
#endif

#include "LibraryVersion.h"
#include "ScatterPlot.h"
#include "SerialX.h"
#include "Table.h"
#include "TelemetryClient.h"
#include "Timer.h"
#include "WiFiSettings.h"
#include "ViewerSketch.h"

const auto VERSION = MakeVersion("1.0");
constexpr auto SKETCH_NAME = "Telemetry_Subscriber_Display";
constexpr uint32_t BAUD_RATE = 115200;
constexpr std::array<const char*, 4> TOPICS = { "Test", "Gate/Left", "Gate/Right", "Wind/Bragg" };
constexpr auto PREFERENCES_NAMESPACE = "Subscriber";

// Selected at startup via prompt in setup().
std::string telemetryTopic;

TelemetryConfig TELEMETRY_CONFIG = {
   .prompts = TOPICS,
};
constexpr uint32_t RATE_UPDATE_INTERVAL_MS = 1000;

SketchConfig SKETCH_CONFIG = {
   .sketchName = SKETCH_NAME,
   .version = VERSION,
   .preferencesNamespace = PREFERENCES_NAMESPACE,
   .cpuFrequencyMhz = 240,
   .enableOTA = true,
};

ViewerSketch sketch(SKETCH_CONFIG, TELEMETRY_CONFIG);

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

   sketch.beginBanner();

   telemetryTopic = sketch.resolveTopic(true);

   sketch.beginConnect();

   TelemetrySubscriber* client = sketch.beginTelemetry();
   client->onSample([](const std::string& topic, double value, int64_t dtMicros)
   {
#ifdef PRINT_SAMPLES
      Serial.print(millis());
      Serial.print(", ");
      Serial.println(value, 3);
#endif

      if (!needsInitialDisplay)
      {
         valueSeries->add(value);
      }
   });

   sketch.setOnLocateEndCallback([]() { needsInitialDisplay = true; });
   sketch.completeInitialization();
}

void loop()
{
   sketch.loop();

   if (sketch.isLocating())
   {
      return;
   }

   TelemetrySubscriber* client = sketch.getClient();

   if (!client->isReady())
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

      table.setValue(0, telemetryTopic, Color::VALUE);
      table.setValue(1, client->getHost(), Color::VALUE2);
      table.setValueNone(2);
      table.draw();

      constexpr int16_t PLOT_TOP_PADDING_PX = 5;
      int16_t plotTop = table.getRect().bottom() + PLOT_TOP_PADDING_PX;
      valuePlot.setRect(0, plotTop, sketch.arduino.width(), sketch.arduino.height() - plotTop);
      valuePlot.setYAxisFormat(VALUE_FORMAT);
      valuePlot.setColors(Color::BLACK, Color::BLACK, Color::GRAY, Color::GRAY);
      valuePlot.setShowXMinMaxValue(false);
      valuePlot.setShowXRangeValue(true);
      valuePlot.setShowYRangeValue(false);
      valuePlot.setYAxisMode(ScatterPlot::AxisMode::GROW_ONLY);
      valueSeries->showPoints = true;
      valueSeries->showLines = false;
      valuePlot.clear();
      valuePlot.invalidate();

      rateDisplayTimer.reset();
      needsInitialDisplay = false;
   }

   if (rateDisplayTimer.ready())
   {
      table.setValue(2, client->getRate());
   }

   table.draw();

   if (!sketch.arduino.buttonA.isPressed())
   {
      valueSeries->updateWindow(millis());
      valuePlot.draw();
   }
}
