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
//

// Uncomment to use local telemetry server instead of remote
#define TELEMETRY_LOCAL

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
#include "EnumSelector.h"
#include "MovingBarChart.h"
#include "MultiBar.h"
#include "SerialX.h"
#include "Slider.h"
#include "Status.h"
#include "TelemetryClient.h"
#include "TimedHistogramChart.h"
#include "TimedRate.h"
#include "TimedStats.h"
#include "Timer.h"
#include "WiFiSettings.h"

// ----------- Telemetry
Arduino arduino;
NeoPixelStatus status(&arduino.neoPixel);
TimedRate refreshRate;
TelemetrySubscriber client("Wind/Bragg");

// ----------- Built-in LED (flashes on each received telemetry value)
constexpr uint16_t RECEIVE_LED_FLASH_MS = 20;

// ----------- Rolling wind statistics
constexpr uint16_t WIND_AVERAGE_DURATION_S = 10 * 60;
constexpr uint8_t WIND_AVERAGE_INTERVAL_S = 10;
constexpr uint8_t WIND_AVERAGE_BINS = WIND_AVERAGE_DURATION_S / WIND_AVERAGE_INTERVAL_S;
TimedStats windStats(WIND_AVERAGE_DURATION_S * 1000, WIND_AVERAGE_BINS);

Format speedFormat("##.# mph", Format::Alignment::RIGHT);

// ----------- Display layout
constexpr uint16_t DISPLAY_HEIGHT = 135;
constexpr uint16_t DISPLAY_WIDTH = 240;
constexpr uint16_t HEADER_HEIGHT = 3 * 8 + 4; // one line of text size 3 plus padding
constexpr Rect16 WORKSPACE_RECT(0, HEADER_HEIGHT, DISPLAY_WIDTH, DISPLAY_HEIGHT - HEADER_HEIGHT);

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

constexpr Rect16 SLIDER_RECT(0, DISPLAY_HEIGHT - VALUES_AXIS_HEIGHT + 2, DISPLAY_WIDTH, 3);
HorizontalSlider slider(SLIDER_RECT, CHART_RANGE, Color::WHITE, Color::BLACK);

enum class Mode
{
   MultiBar,
   Rolling,
   Histogram,
};
EnumSelector<Mode> modeSelector(arduino.buttonA, Mode::Histogram, Mode::Histogram);

void setup()
{
   SerialX::begin();
   arduino.begin();
   status.begin();
   status.setStatus(Status::STARTED);

   arduino.printHeader("Initializing");

   arduino.initWifi(WIFI_SSID, WIFI_PASSWORD, &status);

   client.setCallbacks(nullptr, onDisconnected, nullptr, onReceiveText, onError, onStarted);
   arduino.beginClient("WebSocket", []() { client.beginSSL(TELEMETRY_HOST, TELEMETRY_PORT); }, &status);

   delay(1000); // provide time for the wind meter to get a reading
}

void onStarted()
{
   status.setStatus(Status::READY);
   arduino.clearDisplay();
   displayHeader();
}

void displayHeader()
{
   arduino.setCursor(0, 0);
   arduino.setTextSize(3);
   arduino.print("Wind", Color::HEADING);
}

void onError(std::string msg)
{
   arduino.setTextSize(2);
   arduino.clearDisplay();
   arduino.display.setTextWrap(true);
   arduino.println(msg, Color::WHITE, Color::RED);
   Util::reset(10);
}

void onDisconnected(std::string reason)
{
   Serial.println("Disconnected: " + String(reason.c_str()));
   Util::reset();
}

void onReceiveText(std::string text)
{
   // briefly flash the built-in LED to indicate a new value was received
   arduino.led.flash(RECEIVE_LED_FLASH_MS);
}

void loop()
{
   client.loop();

   if (client.isStarted() == false)
   {
      return;
   }

   refreshRate.tick();

   // get values
   float speed = client.getValue();
   windStats.set(speed);
   multiBar.set(speed);
   rollingChart.set(speed);
   histogramChart.set(speed);
   slider.set(speed);

   // display values
   arduino.setCursor(0, 0);
   arduino.setTextSize(3);
   arduino.printlnR(speed, speedFormat, Color::VALUE);
   arduino.moveCursorY(4);

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
      displayMultiBar(speed);
      break;
   case Mode::Rolling:
      displayRollingChart(speed);
      break;
   case Mode::Histogram:
      displayHistogram();
      break;

   default:
      break;
   }
}

void displayMultiBar(float speed)
{
   arduino.setTextSize(2);
   arduino.println("Last 10 Minutes...", Color::LABEL);
   arduino.moveCursorY(1);

   arduino.println("Min: ", windStats.min(), speedFormat);
   arduino.moveCursorY(1);

   arduino.println("Max: ", windStats.max(), speedFormat);
   arduino.moveCursorY(1);

   arduino.println("Avg: ", windStats.average(), speedFormat);

   // display bar
   multiBar.draw(&arduino.display);
}

void displayRollingChart(float speed)
{
   rollingChart.draw(&arduino.display);
}

Format AxisValueL("##.#", Format::Alignment::LEFT);
Format AxisValueR("##.#", Format::Alignment::RIGHT);

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
