//
// Wind Viewer
//
// Subscribes to live wind speed telemetry over a WebSocket connection and renders it as
// a moving bar chart across the bottom fifth of the display, with a windowed histogram
// (and current-value marker along its axis) filling the space between it and the header.
//
// The layout is computed at runtime from the display's dimensions, so the sketch runs on
// any display size, e.g. the Hosyond ESP32-S3 4" 480x320 display (Viewer board), the
// 240x135 display on the Feather ESP32-S3 TFT, or the Waveshare ESP32-S3-Touch-LCD-4.3B's
// 800x480 display.
//
// Behavior:
// - Connects to WiFi, then opens a WebSocket connection to the telemetry server and
//   receives live wind speed readings as they arrive.
// - Tracks a windowed histogram of readings and a moving bar chart of recent readings.
// - On telemetry disconnect/error, keeps retrying in the background (throttled logging)
//   instead of resetting the device; the display simply stops updating until it reconnects.
// - Checks for a firmware update periodically.
//

// Uncomment to use local telemetry server instead of remote
//#define TELEMETRY_LOCAL

// Uncomment this to build for the Waveshare ESP32-S3-Touch-LCD-4.3B instead of the
// Hosyond ESP32-S3 Viewer board. Also requires selecting the generic "ESP32S3 Dev
// Module" board in Visual Micro (that board has no dedicated board package entry).
//#define ARDUINO_WAVESHARE_ESP32S3_TOUCH_LCD_43

// Default: build for the Hosyond ESP32-S3 Viewer board. Also requires selecting the
// generic "ESP32S3 Dev Module" board in Visual Micro (that board has no dedicated
// board package entry).
#define ARDUINO_HOSYOND_ESP32_S3_VIEWER

// Declares which VLW font sizes this sketch actually uses (2 and 4 - see
// MIN_TEXT_SIZE/MAX_HEADER_TEXT_SIZE below - plus 3, this board's
// DEFAULT_HEADING_SIZE used by printInitHeader() during boot), so
// ArduinoWithDisplay.h/Fonts/Roboto*.h only compile in the needed font data instead of
// all 7 sizes, reducing flash usage.
#define TEXT_SIZES_CUSTOM
#define TEXT_SIZE_2
#define TEXT_SIZE_3
#define TEXT_SIZE_4

#include <string>

constexpr const char* TELEMETRY_TOPICS[] = { "Wind/Lake", "Wind/Bragg" };
constexpr auto PREFERENCES_NAMESPACE = "WindViewer";

// Selected at startup via prompt in setup().
std::string telemetryTopic;

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Viewer)."
#endif

#include "BarChart.h"
#include "ColorRange.h"
#include "LibraryVersion.h"
#include "MovingBarChart.h"
#include "SerialX.h"
#include "Status.h"
#include "Stopwatch.h"
#include "TelemetryClient.h"
#include "TimedHistogramChart.h"
#include "Timer.h"
#include "WiFiSettings.h"
#include "ViewerSketch.h"

// This sketch's own version (e.g. "v1.0"); MakeVersion() appends the shared
// LIBRARY_VERSION build number so shared library changes bump every sketch's
// compiled VERSION without manually editing each sketch.
const auto VERSION = MakeVersion("v1.2");
constexpr auto SKETCH_NAME = "Wind_Viewer";

// ----------- Telemetry
Arduino arduino;

SketchConfig SKETCH_CONFIG = {
   .sketchName = SKETCH_NAME,
   .version = VERSION,
   .preferencesNamespace = PREFERENCES_NAMESPACE,
   .enableOTA = true,
};

TelemetryConfig TELEMETRY_CONFIG = {
   .prompts = TELEMETRY_TOPICS,
};

ViewerSketch viewer(&arduino, SKETCH_CONFIG, TELEMETRY_CONFIG);

Format speedFormat("##.# mph", Format::Alignment::RIGHT);

// ----------- Display layout
// The layout is computed at runtime from the display's dimensions (see initLayout) so
// that the sketch runs on displays of different sizes, e.g. the 480x320 Viewer board
// and the 240x135 Feather ESP32-S3 TFT.
constexpr uint8_t MIN_TEXT_SIZE = 2;
constexpr uint8_t MAX_HEADER_TEXT_SIZE = 4;

// ----------- Telemetry Reconnect Logging
// A dropped telemetry connection keeps retrying in the background (see
// WebSocketsClient's built-in auto-reconnect) rather than resetting the device; without
// throttling, a persistent outage would otherwise flood the log with a message per
// retry attempt. RECONNECT_LOG_INTERVAL_S throttles that down to a single "still down"
// summary every 10 minutes.
constexpr float RECONNECT_LOG_INTERVAL_S = 600.0f;
constexpr uint8_t SPEED_NUM_CHARS = 8; // "##.# mph"
constexpr uint8_t HEADER_PADDING = 6;
constexpr uint8_t VALUES_AXIS_PADDING = 8;

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

// Colors bars from lime green (low speed) through yellow, orange, and red (high speed).
ColorRange speedColorRange;

// Samples the histogram and rolling chart at a fixed cadence (rather than once per
// telemetry message) so that a steady value accumulates counts/bars proportional to
// elapsed time instead of being under-represented relative to rapidly changing values,
// which arrive as more messages.
constexpr uint16_t SAMPLE_INTERVAL_MS = 100;
Timer sampleTimer(SAMPLE_INTERVAL_MS);

// A network hiccup can cause the sensor to stop publishing for a few seconds; since
// TelemetrySubscriber::getValue() simply keeps returning the last received value, the
// 100ms sampleTimer above would otherwise keep re-sampling that stale value into the
// histogram/rolling chart as if real data were still arriving. STALE_TIMEOUT_MS detects
// this by tracking how long it's been since the last message actually arrived (reset in
// WindTelemetryHandler::onReceiveText below) and is set well above the publisher's
// publish interval so normal gaps between messages don't false-trigger.
constexpr uint32_t STALE_TIMEOUT_MS = 2000;

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
/// Handles telemetry lifecycle events for this sketch: draws the header once started
/// and feeds the charts/stats on each received value. Unlike the base class's default
/// behavior, disconnects/failures/errors do not reset the device; they're logged
/// (throttled) and the underlying WebSocket keeps retrying the connection in the
/// background, while the main loop simply stops updating (via isStarted()) until it
/// reconnects.
/// </summary>
///
class WindTelemetryHandler : public TelemetryEventHandler
{
private:
   ReconnectLogThrottle _reconnectLog{ telemetryTopic, RECONNECT_LOG_INTERVAL_S };
   Stopwatch _sinceLastReceive{ false };

public:
   // set on every (re)start; the main loop clears and redraws the display, so the
   // "Telemetry... OK" init line printed during setup() is never wiped mid-init
   bool needsInitialDisplay = true;

   explicit WindTelemetryHandler(IStatus* status) : TelemetryEventHandler(status, &arduino)
   {
   }

   void onStarted() override
   {
      TelemetryEventHandler::onStarted();

      _reconnectLog.reportSuccess();
      needsInitialDisplay = true;
      _sinceLastReceive.reset();
      _sinceLastReceive.start();

      Logger.log("Wind telemetry connected: " + String(viewer.getClient()->getUrl().c_str()));
   }

   void onDisconnected(const std::string& reason) override
   {
      _reconnectLog.reportFailure(std::string("Wind telemetry disconnected (") + reason + "); will keep retrying in the background");
   }

   void onConnectionFailed(const std::string& reason) override
   {
      _reconnectLog.reportFailure(std::string("Wind telemetry connection failed (") + reason + "); will keep retrying in the background");
   }

   void onError(const std::string& message) override
   {
      _reconnectLog.reportFailure(std::string("Wind telemetry error: ") + message + "; will keep retrying in the background");
   }

   ///
   /// <summary>
   /// Indicates whether telemetry has gone stale, i.e. no message has actually been
   /// received in the last STALE_TIMEOUT_MS, so the last value returned by getValue() is
   /// no longer trustworthy and should not be fed into the charts.
   /// </summary>
   /// <returns>true if no telemetry message has arrived recently; false otherwise</returns>
   ///
   bool isStale() const
   {
      return _sinceLastReceive.elapsedMillis() >= STALE_TIMEOUT_MS;
   }

   void onReceiveText(const std::string& text) override
   {
      (void)text;

      _sinceLastReceive.reset();

      // feed the charts/stats only when a new value has actually arrived, rather than
      // every loop() iteration, so stale values aren't repeatedly re-sampled. Skip NaN
      // values (e.g. before the topic has actually been published) since the axis
      // marker renders NaN as a solid red bar.
      float speed = viewer.getClient()->getValue();
      if (!isnan(speed))
      {
         histogramChart->setCurrentValue(speed);
      }
   }
};

WindTelemetryHandler telemetryHandler(&arduino.status);

void setup()
{
   SerialX::begin();
   arduino.begin();

   viewer.beginBanner();

   telemetryTopic = viewer.resolveTopic(true);

   viewer.beginConnect();

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

   viewer.beginTelemetry(&telemetryHandler);
   viewer.onStatus([](LoggerStatus& status)
   {
      status.add("Wind Speed", viewer.getClient()->getValue(), 1);
      status.add("Telemetry URL", viewer.getClient()->getUrl());
   });
   delay(1000); // provide time for the wind meter to get a reading

   Logger.logInitializationComplete();
}

void loop()
{
   viewer.loop();

   TelemetrySubscriber* client = viewer.getClient();

   if (client->isStarted() == false)
   {
      return;
   }

   if (telemetryHandler.needsInitialDisplay)
   {
      telemetryHandler.needsInitialDisplay = false;
      arduino.clearDisplay();
      displayHeader();
   }

   float speed = client->getValue();

   // Skip sampling until the first real value has arrived, and stop sampling once
   // telemetry has gone stale (no message received in a while); otherwise the last
   // received value would keep getting re-sampled into the charts as if the sensor were
   // steadily reporting it, when in fact a network hiccup has just stopped updates.
   bool stale = telemetryHandler.isStale();
   if (sampleTimer.ready() && !isnan(speed) && !stale)
   {
      histogramChart->set(speed);
      rollingChart->set(speed);
   }

   // display values
   arduino.setCursor(0, 0);
   arduino.setTextSize(headerTextSize);
   if (stale)
   {
      arduino.printlnR(speedFormat, Color::RED);
   }
   else
   {
      arduino.printlnR(speed, speedFormat, speedColorRange.getColor(speed));
   }

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
   }
   else if (range.max < 10)
   {
      histogramChart->setVisibleRange(RangeF(0, 10));
   }
   else if (range.max < 15)
   {
      histogramChart->setVisibleRange(RangeF(0, 15));
   }
   else if (range.max < 20)
   {
      histogramChart->setVisibleRange(RangeF(0, 20));
   }
   else
   {
      histogramChart->setVisibleRange(RangeF(0, 30));
   }

   histogramChart->draw(&arduino.display);

   arduino.setTextSize(axisTextSize);
   arduino.setCursor(0, chartRect.bottom() + 3);

   RangeF displayRange = histogramChart->getVisibleRange();
   arduino.print(displayRange.min, AxisValueL, Color::GRAY);
   arduino.printC((displayRange.min + displayRange.max) / 2, AxisValueL, Color::GRAY);
   arduino.printR(displayRange.max, AxisValueR, Color::GRAY);
}

