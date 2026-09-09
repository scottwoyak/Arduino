//
// Wind Subscriber Display
//
// Subscribes to live wind speed telemetry over a WebSocket connection and renders it on
// the display using one of three selectable views: a rolling min/max/average multi-bar,
// a moving bar chart, or a windowed histogram with a current-value slider.
//
// Behavior:
// - Connects to WiFi, then opens a WebSocket connection to the telemetry server and
//   receives live wind speed readings as they arrive.
// - Tracks a rolling 10-minute average/min/max and a windowed histogram of readings.
// - Pressing button A cycles between the MultiBar, Rolling, and Histogram views.
// - Resets the device on telemetry disconnect or error.
// - Checks for a firmware update periodically.
//

// Uncomment to use local telemetry server instead of remote
//#define TELEMETRY_LOCAL

constexpr auto TELEMETRY_TOPIC = "Wind/Lake";

constexpr auto VERSION =
#include "version.txt"
;
constexpr auto SKETCH_NAME = "Wind_Subscriber_Display";


#include "ArduinoBoard.h"

#ifndef ARDUINO_BUTTON_SUPPORTED
#error "This sketch requires a board with button support (e.g. Feather ESP32-S3 or Feather M0)."
#endif
#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif
#ifndef ARDUINO_BUILTIN_LED_SUPPORTED
#error "This sketch requires a board with a separate built-in LED (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include "BarChart.h"
#include "ColorRange.h"
#include "EnumSelector.h"
#include "MovingBarChart.h"
#include "MultiBar.h"
#include "SerialX.h"
#include "Slider.h"
#include "Status.h"
#include "TelemetryClient.h"
#include "TimedHistogramChart.h"
#include "Timer.h"
#include "WiFiSettings.h"

// ----------- Telemetry
Arduino arduino;
NeoPixelStatus status(&arduino.neoPixel);

// ----------- Built-in LED (flashes on each received telemetry value)
constexpr uint16_t RECEIVE_LED_FLASH_MS = 20;

Format speedFormat("##.# mph", Format::Alignment::RIGHT);

// ----------- Display layout
constexpr uint16_t DISPLAY_HEIGHT = 135;
constexpr uint16_t DISPLAY_WIDTH = 240;
constexpr uint16_t HEADER_HEIGHT = 3 * 8 + 4; // one line of text size 3 plus padding
constexpr uint16_t SUBHEADING_HEIGHT = 2 * 8 + 2; // one line of text size 2 plus padding
constexpr Rect16 WORKSPACE_RECT(0, HEADER_HEIGHT + SUBHEADING_HEIGHT, DISPLAY_WIDTH, DISPLAY_HEIGHT - HEADER_HEIGHT - SUBHEADING_HEIGHT);

// ----------- MultiBar view (min/max/average)
Color c1 = Color565::fromRGB(0, 128, 0);
Color c2 = Color::YELLOW;
constexpr uint16_t BAR_HEIGHT = 34;
constexpr Rect16 BAR_RECT = { 0, DISPLAY_HEIGHT - BAR_HEIGHT - 1, DISPLAY_WIDTH, BAR_HEIGHT };
constexpr RangeF MULTIBAR_RANGE = { 0, 40 };
constexpr uint8_t NUM_BARS = 4;
MultiHorizontalBar multiBar(BAR_RECT, MULTIBAR_RANGE, NUM_BARS, c1, c2, Color::BLACK);

// ----------- Rolling bar chart view
Color Green2 = Color565::fromRGB(0, 200, 0);
constexpr RangeF GRAPH_RANGE = { 0, 30 };
constexpr Rect16 GRAPH_RECT = WORKSPACE_RECT;
MovingBarChart rollingChart(GRAPH_RECT, GRAPH_RANGE, Green2, Color::BLACK);

// ----------- Histogram view
constexpr uint16_t HISTOGRAM_DURATION_S = 10 * 60;
constexpr uint8_t HISTOGRAM_NUM_BINS = 80;
constexpr RangeF CHART_RANGE = { 0, 30 };
constexpr uint8_t VALUES_AXIS_HEIGHT = 16 + 6;
constexpr Rect16 CHART_RECT(WORKSPACE_RECT.x, WORKSPACE_RECT.y, WORKSPACE_RECT.width, WORKSPACE_RECT.height - VALUES_AXIS_HEIGHT);
TimedHistogramChart histogramChart(CHART_RECT, CHART_RANGE, HISTOGRAM_NUM_BINS, HISTOGRAM_DURATION_S * 1000, Green2, Color::BLACK);

// Colors bars from lime green (low speed) through yellow, orange, and red (high speed).
ColorRange speedColorRange;

// Samples the histogram and rolling chart at a fixed cadence (rather than once per
// telemetry message) so that a steady value accumulates counts/bars proportional to
// elapsed time instead of being under-represented relative to rapidly changing values,
// which arrive as more messages.
constexpr uint16_t SAMPLE_INTERVAL_MS = 100;
Timer sampleTimer(SAMPLE_INTERVAL_MS);

constexpr Rect16 SLIDER_RECT(0, DISPLAY_HEIGHT - VALUES_AXIS_HEIGHT + 2, DISPLAY_WIDTH, 3);
HorizontalSlider slider(SLIDER_RECT, CHART_RANGE, Color::WHITE, Color::BLACK);

enum class Mode
{
   MultiBar,
   Rolling,
   Histogram,
};
EnumSelector<Mode> modeSelector(arduino.buttonA, Mode::Histogram, Mode::Histogram);

///
/// <summary>
/// Draws the sketch's title header (the telemetry topic) at the top of the display.
/// </summary>
///
void displayHeader()
{
   arduino.setCursor(0, 0);
   arduino.setTextSize(3);
   arduino.println(TELEMETRY_TOPIC, Color::HEADING);
}

///
/// <summary>
/// Draws a subheading below the header indicating whether the telemetry server is
/// local or remote.
/// </summary>
///
void displaySubheading()
{
   arduino.setTextSize(2);
#ifdef TELEMETRY_LOCAL
   arduino.println("Local", Color::SUB_HEADING);
#else
   arduino.println("Remote", Color::SUB_HEADING);
#endif
}

///
/// <summary>
/// Draws the server mode (Local/Remote) and telemetry topic as footer text at the
/// bottom of the display, left and right aligned respectively. Only shown on the
/// setup screen; it's cleared when the main display is drawn on telemetry start.
/// </summary>
///
void displayFooter()
{
   Point16 savedCursor = arduino.getCursor();
   uint8_t savedTextSize = arduino.getTextSize();

   arduino.setTextSize(2);

   arduino.setCursor(0, -arduino.charH());
#ifdef TELEMETRY_LOCAL
   arduino.print("Local", Color::GRAY);
#else
   arduino.print("Remote", Color::GRAY);
#endif

   arduino.setCursor(arduino.width(), -arduino.charH());
   arduino.printR(TELEMETRY_TOPIC, Color::GRAY);

   arduino.setTextSize(savedTextSize);
   arduino.setCursor(savedCursor);
}

TelemetrySubscriber client(TELEMETRY_TOPIC, &status);

///
/// <summary>
/// Handles telemetry lifecycle events for this sketch: draws the header once started
/// and feeds the charts/stats/LED flash on each received value. Disconnect and error
/// handling use the base class's default behavior.
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

      arduino.clearDisplay();
      displayHeader();
      displaySubheading();
   }

   void onReceiveText(const std::string& text) override
   {
      (void)text;

      // briefly flash the built-in LED to indicate a new value was received
      arduino.led.flash(RECEIVE_LED_FLASH_MS);

      // feed the charts/stats only when a new value has actually arrived, rather than
      // every loop() iteration, so stale values aren't repeatedly re-sampled
      float speed = client.getValue();
      multiBar.set(speed);
      slider.set(speed);
   }
};

WindTelemetryHandler telemetryHandler(&status);

void setup()
{
   SerialX::begin();
   arduino.begin();
   status.begin();

   speedColorRange.addStop(0, Color::LIME);
   speedColorRange.addStop(5, Color::LIME);
   speedColorRange.addStop(10, Color::YELLOW);
   speedColorRange.addStop(20, Color::ORANGE);
   speedColorRange.addStop(30, Color::RED);
   histogramChart.setColorRange(&speedColorRange);

   client.setHandler(&telemetryHandler);

   arduino.beginInit();
   displayFooter();

   arduino.initWifi(WIFI_SSID, WIFI_PASSWORD, &status);

   arduino.enableOTA(VERSION, SKETCH_NAME);

   arduino.initClient("WebSocket", []() { client.beginSSL(TELEMETRY_HOST, TELEMETRY_PORT); }, &status);
   delay(1000); // provide time for the wind meter to get a reading
}

void loop()
{
   arduino.checkForOTA();

   client.loop();

   if (client.isStarted() == false)
   {
      return;
   }

   float speed = client.getValue();

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
   arduino.setTextSize(3);
   arduino.printlnR(speed, speedFormat, Color::VALUE);
   arduino.setCursor(0, WORKSPACE_RECT.y);

   if (modeSelector.hasChanged())
   {
      arduino.clear(WORKSPACE_RECT);
      multiBar.reset();
      rollingChart.reset();
      histogramChart.reset();
   }

   switch (modeSelector.value())
   {
   case Mode::MultiBar:
      displayMultiBar();
      break;
   case Mode::Rolling:
      displayRollingChart();
      break;
   case Mode::Histogram:
      displayHistogram();
      break;

   default:
      break;
   }
}

///
/// <summary>
/// Draws the MultiBar view: rolling 10-minute min/max/average labels and bar.
/// </summary>
///
void displayMultiBar()
{
   arduino.setTextSize(2);
   arduino.println("Last 10 Minutes...", Color::LABEL);
   arduino.moveCursorY(1);

   arduino.println("Min: ", histogramChart.min(), speedFormat);
   arduino.moveCursorY(1);

   arduino.println("Max: ", histogramChart.max(), speedFormat);
   arduino.moveCursorY(1);

   arduino.println("Avg: ", histogramChart.average(), speedFormat);

   // display bar
   multiBar.draw(&arduino.display);
}

///
/// <summary>
/// Draws the Rolling view: a moving bar chart of recent readings.
/// </summary>
///
void displayRollingChart()
{
   rollingChart.draw(&arduino.display);
}

Format AxisValueL("##.#", Format::Alignment::LEFT);
Format AxisValueR("##.#", Format::Alignment::RIGHT);

///
/// <summary>
/// Draws the Histogram view: a windowed histogram of recent readings with a
/// current-value slider, auto-scaling the visible range to the current values.
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

   arduino.setTextSize(2);
   arduino.setCursor(0, -15);

   RangeF displayRange = histogramChart.getVisibleRange();
   arduino.print(displayRange.min, AxisValueL, Color::GRAY);
   arduino.printC((displayRange.min + displayRange.max)/2, AxisValueL, Color::GRAY);
   arduino.printR(displayRange.max, AxisValueR, Color::GRAY);
   uint16_t y = DISPLAY_HEIGHT - VALUES_AXIS_HEIGHT + 1;
   arduino.display.drawLine(0, y, arduino.display.width(), y, (uint16_t)Color::GRAY);
}
