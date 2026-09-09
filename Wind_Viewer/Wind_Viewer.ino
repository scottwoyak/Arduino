//
// Wind Viewer
//
// Subscribes to live wind speed telemetry over a WebSocket connection and renders it on
// the Hosyond ESP32-32E 4" display (Viewer board): a moving bar chart across the bottom
// third of the display, with a windowed histogram (and current-value slider) filling
// the space between it and the header.
//
// Behavior:
// - Connects to WiFi, then opens a WebSocket connection to the telemetry server and
//   receives live wind speed readings as they arrive.
// - Tracks a windowed histogram of readings and a moving bar chart of recent readings.
// - Resets the device on telemetry disconnect or error.
// - Checks for a firmware update periodically.
//

// Uncomment to use local telemetry server instead of remote
//#define TELEMETRY_LOCAL

#include <string>

constexpr const char* TELEMETRY_TOPICS[] = { "Wind/Lake", "Wind/Bragg" };
constexpr uint8_t NUM_TELEMETRY_TOPICS = 2;
constexpr uint32_t TOPIC_PROMPT_TIMEOUT_MS = 10 * 1000;
constexpr auto PREFERENCES_NAMESPACE = "WindViewer";
constexpr auto TOPIC_KEY = "topic";

// Selected at startup via prompt in setup().
std::string telemetryTopic;

constexpr auto VERSION =
#include "version.txt"
;
constexpr auto SKETCH_NAME = "Wind_Viewer";


#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Viewer)."
#endif

#include "BarChart.h"
#include "ColorRange.h"
#include "MovingBarChart.h"
#include "SerialX.h"
#include "Slider.h"
#include "Status.h"
#include "TelemetryClient.h"
#include "TimedHistogramChart.h"
#include "Timer.h"
#include "WiFiSettings.h"

// ----------- Telemetry
Arduino arduino;

Format speedFormat("##.# mph", Format::Alignment::RIGHT);

// ----------- Display layout
constexpr uint16_t DISPLAY_HEIGHT = 320;
constexpr uint16_t DISPLAY_WIDTH = 480;
constexpr uint8_t HEADER_TEXT_SIZE = 4;
constexpr uint16_t HEADER_HEIGHT = 4 * 8 + 6; // one line of text size 4 plus padding
constexpr Rect16 WORKSPACE_RECT(0, HEADER_HEIGHT, DISPLAY_WIDTH, DISPLAY_HEIGHT - HEADER_HEIGHT);

// ----------- Rolling bar chart (bottom third of the display)
constexpr RangeF GRAPH_RANGE = { 0, 30 };
constexpr uint16_t ROLLING_CHART_HEIGHT = DISPLAY_HEIGHT / 4;
constexpr Rect16 GRAPH_RECT(0, DISPLAY_HEIGHT - ROLLING_CHART_HEIGHT, DISPLAY_WIDTH, ROLLING_CHART_HEIGHT);
MovingBarChart rollingChart(GRAPH_RECT, GRAPH_RANGE, Color::LIME, Color::BLACK);

// ----------- Histogram (fills the space between the header and the rolling chart)
constexpr uint16_t HISTOGRAM_DURATION_S = 10 * 60;
constexpr uint16_t HISTOGRAM_NUM_BINS = 300;
constexpr RangeF CHART_RANGE = { 0, 30 };
constexpr uint8_t VALUES_AXIS_HEIGHT = 25 + 8;
constexpr Rect16 CHART_RECT(0, HEADER_HEIGHT, DISPLAY_WIDTH, DISPLAY_HEIGHT - HEADER_HEIGHT - ROLLING_CHART_HEIGHT - VALUES_AXIS_HEIGHT);
TimedHistogramChart histogramChart(CHART_RECT, CHART_RANGE, HISTOGRAM_NUM_BINS, HISTOGRAM_DURATION_S * 1000, Color::LIME, Color::BLACK);

// Colors bars from lime green (low speed) through yellow, orange, and red (high speed).
ColorRange speedColorRange;

// Samples the histogram and rolling chart at a fixed cadence (rather than once per
// telemetry message) so that a steady value accumulates counts/bars proportional to
// elapsed time instead of being under-represented relative to rapidly changing values,
// which arrive as more messages.
constexpr uint16_t SAMPLE_INTERVAL_MS = 100;
Timer sampleTimer(SAMPLE_INTERVAL_MS);

constexpr Rect16 SLIDER_RECT(0, CHART_RECT.y + CHART_RECT.height + 2, DISPLAY_WIDTH, 3);
HorizontalSlider slider(SLIDER_RECT, CHART_RANGE, Color::WHITE, Color::BLACK);

///
/// <summary>
/// Draws the sketch's title header (the telemetry topic) at the top of the display.
/// </summary>
///
void displayHeader()
{
   arduino.setCursor(0, 0);
   arduino.setTextSize(HEADER_TEXT_SIZE);
   arduino.println(telemetryTopic, Color::HEADING);
}

///
/// <summary>
/// Draws the telemetry topic as footer text at the bottom-right of the display. Only
/// shown on the setup screen; it's cleared when the main display is drawn on telemetry
/// start.
/// </summary>
///
void displayFooter()
{
   Point16 savedCursor = arduino.getCursor();
   uint8_t savedTextSize = arduino.getTextSize();

   arduino.setTextSize(3);

   arduino.setCursor(arduino.width(), -arduino.charH());
   arduino.printR(telemetryTopic, Color::GRAY);

   arduino.setTextSize(savedTextSize);
   arduino.setCursor(savedCursor);
}

// Constructed in setup() once the telemetry topic has been selected.
TelemetrySubscriber* client = nullptr;

///
/// <summary>
/// Handles telemetry lifecycle events for this sketch: draws the header once started
/// and feeds the charts/stats on each received value. Disconnect and error handling use
/// the base class's default behavior.
/// </summary>
///
class WindTelemetryHandler : public TelemetryEventHandler
{
public:
   explicit WindTelemetryHandler(IStatus* status) : TelemetryEventHandler(status, &arduino)
   {
   }

   void onStarted() override
   {
      TelemetryEventHandler::onStarted();

      // Initialization is complete; hide the virtual NeoPixel so it stops overwriting
      // the regular display content drawn in its corner.
      arduino.hideStatus();

      arduino.clearDisplay();
      displayHeader();
   }

   void onReceiveText(const std::string& text) override
   {
      (void)text;

      // feed the charts/stats only when a new value has actually arrived, rather than
      // every loop() iteration, so stale values aren't repeatedly re-sampled
      float speed = client->getValue();
      slider.set(speed);
   }
};

WindTelemetryHandler telemetryHandler(&arduino.status);

void setup()
{
   SerialX::begin();
   arduino.begin();

   speedColorRange.addStop(0, Color::LIME);
   speedColorRange.addStop(2, Color::LIME);
   speedColorRange.addStop(10, Color::YELLOW);
   speedColorRange.addStop(15, Color::ORANGE);
   speedColorRange.addStop(20, Color::RED);
   histogramChart.setColorRange(&speedColorRange);
   rollingChart.setColorRange(&speedColorRange);

   arduino.preferences.begin(PREFERENCES_NAMESPACE, true);
   String savedTopic = arduino.preferences.getString(TOPIC_KEY, TELEMETRY_TOPICS[0]);
   arduino.preferences.end();

   size_t defaultTopicIndex = 0;
   for (uint8_t i = 0; i < NUM_TELEMETRY_TOPICS; i++)
   {
      if (savedTopic.equals(TELEMETRY_TOPICS[i]))
      {
         defaultTopicIndex = i;
         break;
      }
   }

   Serial.println("Select telemetry topic:");
   for (uint8_t i = 0; i < NUM_TELEMETRY_TOPICS; i++)
   {
      Serial.print("  ");
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.println(TELEMETRY_TOPICS[i]);
   }
   size_t topicIndex = SerialX::readSelectionWithTimeout(NUM_TELEMETRY_TOPICS, defaultTopicIndex, TOPIC_PROMPT_TIMEOUT_MS);
   telemetryTopic = TELEMETRY_TOPICS[topicIndex];

   arduino.preferences.begin(PREFERENCES_NAMESPACE, false);
   arduino.preferences.putString(TOPIC_KEY, telemetryTopic.c_str());
   arduino.preferences.end();

   client = new TelemetrySubscriber(telemetryTopic, &arduino.status);
   client->setHandler(&telemetryHandler);

   arduino.beginInit(telemetryTopic.c_str());
   displayFooter();

   arduino.initWifi(WIFI_SSID, WIFI_PASSWORD, &arduino.status);

   arduino.enableOTA(VERSION, SKETCH_NAME);

   arduino.initClient("WebSocket", []() { client->beginSSL(TELEMETRY_HOST, TELEMETRY_PORT); }, &arduino.status);
   delay(1000); // provide time for the wind meter to get a reading
}

void loop()
{
   arduino.checkForOTA();

   client->loop();

   if (client->isStarted() == false)
   {
      return;
   }

   float speed = client->getValue();

   // Skip sampling until the first real value has arrived; otherwise NAN placeholders
   // get shifted into the rolling/histogram charts and take a full period to clear out,
   // showing as red bars in the meantime.
   if (sampleTimer.ready() && !isnan(speed))
   {
      histogramChart.set(speed);
      rollingChart.set(speed);
   }

   // display values
   arduino.setCursor(0, 0);
   arduino.setTextSize(HEADER_TEXT_SIZE);
   arduino.printlnR(speed, speedFormat, Color::VALUE);

   displayHistogram();
   rollingChart.draw(&arduino.display);
}

Format AxisValueL("##.#", Format::Alignment::LEFT);
Format AxisValueR("##.#", Format::Alignment::RIGHT);

///
/// <summary>
/// Draws the histogram: a windowed histogram of recent readings with a current-value
/// slider, auto-scaling the visible range to the current values.
/// </summary>
///
void displayHistogram()
{
   RangeF range = histogramChart.getCurrentValuesRange();

   if (range.max < 5)
   {
      histogramChart.setVisibleRange(RangeF(0, 5));
      slider.setRange(RangeF(0, 5));
   }
   else if (range.max < 10)
   {
      histogramChart.setVisibleRange(RangeF(0, 10));
      slider.setRange(RangeF(0, 10));
   }
   else if (range.max < 15)
   {
      histogramChart.setVisibleRange(RangeF(0, 15));
      slider.setRange(RangeF(0, 15));
   }
   else if (range.max < 20)
   {
      histogramChart.setVisibleRange(RangeF(0, 20));
      slider.setRange(RangeF(0, 20));
   }
   else
   {
      histogramChart.setVisibleRange(RangeF(0, 30));
      slider.setRange(RangeF(0, 30));
   }

   histogramChart.draw(&arduino.display);
   slider.draw(&arduino.display);

   arduino.setTextSize(3);
   arduino.setCursor(0, CHART_RECT.y + CHART_RECT.height + 3);

   RangeF displayRange = histogramChart.getVisibleRange();
   arduino.print(displayRange.min, AxisValueL, Color::GRAY);
   arduino.printC((displayRange.min + displayRange.max) / 2, AxisValueL, Color::GRAY);
   arduino.printR(displayRange.max, AxisValueR, Color::GRAY);
   uint16_t y = CHART_RECT.y + CHART_RECT.height + 1;
   arduino.display.drawLine(0, y, arduino.display.width(), y, (uint16_t)Color::GRAY);
}

