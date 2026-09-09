//
// Wind Viewer
//
// Subscribes to live wind speed telemetry over a WebSocket connection and renders it as
// a moving bar chart across the bottom fifth of the display, with a windowed histogram
// (and current-value slider) filling the space between it and the header.
//
// The layout is computed at runtime from the display's dimensions, so the sketch runs on
// any display size, e.g. the Hosyond ESP32-32E 4" 480x320 display (Viewer board) or the
// 240x135 display on the Feather ESP32-S3 TFT.
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
// The layout is computed at runtime from the display's dimensions (see initLayout) so
// that the sketch runs on displays of different sizes, e.g. the 480x320 Viewer board
// and the 240x135 Feather ESP32-S3 TFT.
constexpr uint8_t MIN_TEXT_SIZE = 2;
constexpr uint8_t MAX_HEADER_TEXT_SIZE = 4;
constexpr uint8_t SPEED_NUM_CHARS = 8; // "##.# mph"
constexpr uint8_t HEADER_PADDING = 6;
constexpr uint8_t VALUES_AXIS_PADDING = 8;
constexpr uint8_t SLIDER_HEIGHT = 3;

// ----------- Rolling bar chart (bottom fifth of the display)
constexpr RangeF GRAPH_RANGE = { 0, 30 };
constexpr float ROLLING_CHART_HEIGHT_FRACTION = 0.20f;

// ----------- Histogram (fills the space between the header and the rolling chart)
constexpr uint16_t HISTOGRAM_DURATION_S = 10 * 60;
constexpr RangeF CHART_RANGE = { 0, 30 };

// Histogram bins per pixel of chart width: 300 bins across the 480 pixel wide Viewer
// display, with narrower displays getting proportionally fewer bins so that bar width
// stays consistent across display sizes.
constexpr float HISTOGRAM_BINS_PER_PIXEL = 300.0f / 480.0f;

// Set by initLayout() once the display dimensions are known.
uint8_t headerTextSize;
uint8_t axisTextSize;
Rect16 chartRect;
MovingBarChart* rollingChart = nullptr;
TimedHistogramChart* histogramChart = nullptr;
HorizontalSlider* slider = nullptr;

// Colors bars from lime green (low speed) through yellow, orange, and red (high speed).
ColorRange speedColorRange;

// Samples the histogram and rolling chart at a fixed cadence (rather than once per
// telemetry message) so that a steady value accumulates counts/bars proportional to
// elapsed time instead of being under-represented relative to rapidly changing values,
// which arrive as more messages.
constexpr uint16_t SAMPLE_INTERVAL_MS = 100;
Timer sampleTimer(SAMPLE_INTERVAL_MS);

///
/// <summary>
/// Computes the display layout and constructs the charts and slider from the display's
/// actual dimensions, scaling text sizes, chart heights and histogram bin count so the
/// sketch renders correctly on displays of different sizes. Must be called after
/// arduino.begin(), once the display has been initialized.
/// </summary>
///
void initLayout()
{
   uint16_t displayWidth = arduino.width();
   uint16_t displayHeight = arduino.height();

   // use the largest header size where the topic and the right-aligned speed still fit
   // on a single line
   headerTextSize = MIN_TEXT_SIZE;
   for (uint8_t size = MAX_HEADER_TEXT_SIZE; size > MIN_TEXT_SIZE; size--)
   {
      uint16_t numChars = telemetryTopic.length() + SPEED_NUM_CHARS;
      if (arduino.charW(size) * numChars <= displayWidth)
      {
         headerTextSize = size;
         break;
      }
   }
   axisTextSize = headerTextSize > MIN_TEXT_SIZE ? headerTextSize - 1 : MIN_TEXT_SIZE;

   uint16_t headerHeight = arduino.charH(headerTextSize) + HEADER_PADDING;
   uint16_t valuesAxisHeight = arduino.charH(axisTextSize) + VALUES_AXIS_PADDING;
   uint16_t rollingChartHeight = displayHeight * ROLLING_CHART_HEIGHT_FRACTION;

   Rect16 graphRect = { 0, (uint16_t)(displayHeight - rollingChartHeight), displayWidth, rollingChartHeight };
   rollingChart = new MovingBarChart(graphRect, GRAPH_RANGE, Color::LIME, Color::BLACK);

   chartRect = { 0, headerHeight, displayWidth, (uint16_t)(displayHeight - headerHeight - rollingChartHeight - valuesAxisHeight) };
   uint16_t numBins = chartRect.width * HISTOGRAM_BINS_PER_PIXEL;
   histogramChart = new TimedHistogramChart(chartRect, CHART_RANGE, numBins, HISTOGRAM_DURATION_S * 1000, Color::LIME, Color::BLACK);

   Rect16 sliderRect = { 0, (uint16_t)(chartRect.bottom() + 2), displayWidth, SLIDER_HEIGHT };
   slider = new HorizontalSlider(sliderRect, CHART_RANGE, Color::WHITE, Color::BLACK);
}

///
/// <summary>
/// Draws the sketch's title header (the telemetry topic) at the top of the display.
/// </summary>
///
void displayHeader()
{
   arduino.setCursor(0, 0);
   arduino.setTextSize(headerTextSize);
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

   arduino.setTextSize(axisTextSize);

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

      // Initialization is complete; turn the NeoPixel off so it stops overwriting the
      // regular display content drawn in its corner on boards where it's drawn on the
      // display.
      arduino.neoPixel.turnOff();

      arduino.clearDisplay();
      displayHeader();
   }

   void onReceiveText(const std::string& text) override
   {
      (void)text;

      // feed the charts/stats only when a new value has actually arrived, rather than
      // every loop() iteration, so stale values aren't repeatedly re-sampled
      float speed = client->getValue();
      slider->set(speed);
   }
};

WindTelemetryHandler telemetryHandler(&arduino.status);

void setup()
{
   SerialX::begin();
   arduino.begin();

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

   // the layout depends on the selected topic's length, so build it only once the
   // topic is known
   initLayout();

   speedColorRange.addStop(0, Color::LIME);
   speedColorRange.addStop(2, Color::LIME);
   speedColorRange.addStop(10, Color::YELLOW);
   speedColorRange.addStop(15, Color::ORANGE);
   speedColorRange.addStop(20, Color::RED);
   histogramChart->setColorRange(&speedColorRange);
   rollingChart->setColorRange(&speedColorRange);

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
      histogramChart->set(speed);
      rollingChart->set(speed);
   }

   // display values
   arduino.setCursor(0, 0);
   arduino.setTextSize(headerTextSize);
   arduino.printlnR(speed, speedFormat, Color::VALUE);

   displayHistogram();
   rollingChart->draw(&arduino.display);
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
   RangeF range = histogramChart->getCurrentValuesRange();

   if (range.max < 5)
   {
      histogramChart->setVisibleRange(RangeF(0, 5));
      slider->setRange(RangeF(0, 5));
   }
   else if (range.max < 10)
   {
      histogramChart->setVisibleRange(RangeF(0, 10));
      slider->setRange(RangeF(0, 10));
   }
   else if (range.max < 15)
   {
      histogramChart->setVisibleRange(RangeF(0, 15));
      slider->setRange(RangeF(0, 15));
   }
   else if (range.max < 20)
   {
      histogramChart->setVisibleRange(RangeF(0, 20));
      slider->setRange(RangeF(0, 20));
   }
   else
   {
      histogramChart->setVisibleRange(RangeF(0, 30));
      slider->setRange(RangeF(0, 30));
   }

   histogramChart->draw(&arduino.display);
   slider->draw(&arduino.display);

   arduino.setTextSize(axisTextSize);
   arduino.setCursor(0, chartRect.bottom() + 3);

   RangeF displayRange = histogramChart->getVisibleRange();
   arduino.print(displayRange.min, AxisValueL, Color::GRAY);
   arduino.printC((displayRange.min + displayRange.max) / 2, AxisValueL, Color::GRAY);
   arduino.printR(displayRange.max, AxisValueR, Color::GRAY);
   uint16_t y = chartRect.bottom() + 1;
   arduino.display.drawLine(0, y, arduino.display.width(), y, (uint16_t)Color::GRAY);
}

