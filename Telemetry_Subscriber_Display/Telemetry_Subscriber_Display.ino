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
#include <Preferences.h>
#include <array>
#include <sstream>
#include <string>

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#include "WrongBoard.h"
#endif

#include "DeviceServerClient.h"
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
constexpr std::array<const char*, 4> TOPICS = { "Test", "Gate/Left", "Gate/Right", "Wind/Bragg" };
uint8_t topicIndex = 0;
constexpr auto PREFERENCES_NAMESPACE = "Subscriber";
constexpr auto TOPIC_INDEX_KEY = "topicIndex";
Preferences preferences;

TelemetryConfig TELEMETRY_CONFIG = {
   .topic = TOPICS[0],
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

/// <summary>Commands registered with DeviceServerClient, appended to the "Help" command's listing.</summary>
constexpr DeviceServerClient::CommandInfo SUBSCRIBER_COMMANDS[] = {
   { "GetTopic", "Reports the current telemetry topic and lists all available topics" },
   { "SetTopic <topic>", "Persists and switches to the given telemetry topic immediately" },
   { "GetTelemetryStatus", "Reports whether the telemetry WebSocket is connected/ready, the current topic, and the receive rate" },
};

///
/// <summary>
/// Switches the active subscription topic by index, persists it in Preferences, and
/// triggers a display redraw.
/// </summary>
/// <param name="newIndex">Index into TOPICS.</param>
///
void selectTopicIndex(uint8_t newIndex)
{
   topicIndex = newIndex;
   preferences.putUChar(TOPIC_INDEX_KEY, topicIndex);
   client.setTopic(TOPICS[topicIndex]);
   client.setTopics({ TOPICS[topicIndex] });
   needsInitialDisplay = true;
}

///
/// <summary>
/// Handles custom DeviceServer commands for inspecting and changing the subscribed topic.
/// </summary>
/// <param name="command">Full command text received from the DeviceServer.</param>
///
void onSubscriberCommand(const char* command)
{
   std::istringstream stream(command);
   std::string verb;
   stream >> verb;

   if (strcasecmp(verb.c_str(), "GetTopic") == 0)
   {
      std::ostringstream text;
      for (size_t i = 0; i < TOPICS.size(); i++)
      {
         if (i > 0)
         {
            text << "\n";
         }
         text << TOPICS[i];
         if (i == topicIndex)
         {
            text << " (selected)";
         }
      }
      DeviceServerClient::respond(text.str().c_str());
      return;
   }

   if (strcasecmp(verb.c_str(), "SetTopic") == 0)
   {
      std::string value;
      std::getline(stream >> std::ws, value);
      if (value.empty())
      {
         DeviceServerClient::respond("Error: SetTopic requires a topic name");
         return;
      }

      for (uint8_t i = 0; i < TOPICS.size(); i++)
      {
         if (strcasecmp(TOPICS[i], value.c_str()) == 0)
         {
            selectTopicIndex(i);
            DeviceServerClient::respond((std::string("Switched to topic=\"") + TOPICS[i] + "\"").c_str());
            return;
         }
      }

      DeviceServerClient::respond((std::string("Error: unknown topic \"") + value + "\"").c_str());
      return;
   }

   if (strcasecmp(verb.c_str(), "GetTelemetryStatus") == 0)
   {
      std::string result = std::string("topic=\"") + client.getTopic() + "\"\n" +
         "connected=" + (client.isConnected() ? "true" : "false") + "\n" +
         "ready=" + (client.isReady() ? "true" : "false") + "\n" +
         "rate=" + std::to_string(client.getRate()) + " samples/s";
      DeviceServerClient::respond(result.c_str());
      return;
   }

   DeviceServerClient::respond((std::string("Unknown command: ") + verb).c_str());
}

void setup()
{
   SerialX::begin(BAUD_RATE);

   valueSeries->pointSize = VALUE_SERIES_POINT_SIZE;

   preferences.begin(PREFERENCES_NAMESPACE);
   topicIndex = preferences.getUChar(TOPIC_INDEX_KEY, 0);
   if (topicIndex >= TOPICS.size())
   {
      topicIndex = 0;
   }
   client.setTopic(TOPICS[topicIndex]);

   client.onSample([](const std::string& topic, double value, int64_t dtMicros)
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

   sketch.beginBanner();
   sketch.beginConnect();

   // Other sketch classes (PublisherSketch, ViewerSketch) print this topic line themselves.
   sketch.arduino.printlnInitStatus("Topic... ", std::string("\"") + TOPICS[topicIndex] + "\"");
   client.connect(&sketch.arduino, sketch.getStatus());

   sketch.setOnLocateEndCallback([]() { needsInitialDisplay = true; });
   DeviceServerClient::onCommand(onSubscriberCommand, SUBSCRIBER_COMMANDS, std::size(SUBSCRIBER_COMMANDS));

   sketch.beginLogger();
   sketch.completeInitialization();
}

void loop()
{
   sketch.loop();
   client.loop();

   if (sketch.isLocating())
   {
      return;
   }

   if (sketch.arduino.buttonA.wasPressed())
   {
      selectTopicIndex((topicIndex + 1) % TOPICS.size());
   }

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

      table.setValue(0, TOPICS[topicIndex], Color::VALUE);
      table.setValue(1, client.getHost(), Color::VALUE2);
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
      table.setValue(2, client.getRate());
   }

   table.draw();

   if (!sketch.arduino.buttonA.isPressed())
   {
      valueSeries->updateWindow(millis());
      valuePlot.draw();
   }
}
