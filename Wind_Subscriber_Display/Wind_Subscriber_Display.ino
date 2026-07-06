// undefine to use the remote server
//#define TELEMETRY_LOCAL

#include <WiFi.h>

#include "ArduinoBoard.h"

#ifndef ARDUINO_BUTTON_SUPPORTED
#error "This sketch requires a board with button support (e.g. Feather ESP32-S3 or Feather M0)."
#endif
#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include "SerialX.h"
#include "WiFiSettings.h"
#include "TimedStats.h"
#include "TimedHistogramChart.h"
#include "Slider.h"
#include "MultiBar.h"
#include "BarChart.h"
#include "MovingBarChart.h"
#include "RollingRate.h"
#include "TelemetryClient.h"

Arduino arduino;
RollingRate refreshRate(100);
Stopwatch sw;
TelemetrySubscriber client("Wind/Lake");

constexpr uint16_t WIND_AVERAGE_DURATION_S = 10 * 60;
constexpr uint8_t WIND_AVERAGE_INTERVAL_S = 10;     
constexpr uint8_t WIND_AVERAGE_BINS = WIND_AVERAGE_DURATION_S / WIND_AVERAGE_INTERVAL_S;
TimedStats windStats(WIND_AVERAGE_DURATION_S*1000, WIND_AVERAGE_BINS);

Format speedFormat("##.# mph", Format::Alignment::RIGHT);

constexpr uint16_t DISPLAY_HEIGHT = 135;
constexpr uint16_t DISPLAY_WIDTH = 240;
constexpr uint16_t HEADER_HEIGHT = 3 * 8 + 4; // one line of text size 3 plus padding

Color c1 = Color565::fromRGB(0, 128, 0);
Color c2 = Color::YELLOW;
constexpr uint16_t BAR_HEIGHT = 34;
constexpr Rect16 BAR_RECT = { 0, DISPLAY_HEIGHT - BAR_HEIGHT - 1, DISPLAY_WIDTH, BAR_HEIGHT };
constexpr RangeF MULTIBAR_RANGE = { 0, 40 };
constexpr uint8_t NUM_BARS = 4;
MultiHorizontalBar multiBar(BAR_RECT, MULTIBAR_RANGE, NUM_BARS, c1, c2, Color::BLACK);

Color Green2 = Color565::fromRGB(0, 200, 0);
constexpr RangeF GRAPH_RANGE = { 0, 30 };
constexpr Rect16 GRAPH_RECT(0, HEADER_HEIGHT, DISPLAY_WIDTH, DISPLAY_HEIGHT - HEADER_HEIGHT);
MovingBarChart rollingChart(GRAPH_RECT, GRAPH_RANGE, Green2, Color::BLACK);

constexpr uint16_t HISTOGRAM_DURATION_S = 10*60;
constexpr uint8_t HISTOGRAM_NUM_BINS = 80;
constexpr RangeF CHART_RANGE = { 0, 30 };
constexpr uint8_t VALUES_AXIS_HEIGHT = 16 + 6;
constexpr Rect16 CHART_RECT(0, HEADER_HEIGHT, DISPLAY_WIDTH, DISPLAY_HEIGHT - HEADER_HEIGHT - VALUES_AXIS_HEIGHT);
TimedHistogramChart histogramChart(CHART_RECT, CHART_RANGE, HISTOGRAM_NUM_BINS, HISTOGRAM_DURATION_S * 1000, Green2, Color::BLACK);

constexpr Rect16 SLIDER_RECT(0, DISPLAY_HEIGHT - VALUES_AXIS_HEIGHT+2, DISPLAY_WIDTH, 3);
HorizontalSlider slider(SLIDER_RECT, CHART_RANGE, Color::WHITE, Color::BLACK);

enum class Mode
{
   MultiBar,
   Rolling,
   Histogram,
   Count,
} mode;

Mode operator++(Mode& mode, int)
{
   mode = static_cast<Mode>((static_cast<int>(mode) + 1) % static_cast<int>(Mode::Count));
   return mode;
}

void setup()
{
   mode = Mode::Histogram;

   SerialX::begin();
   arduino.begin();

   // Connect to WiFi
   WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

   arduino.setTextSize(2);
   arduino.setCursorY(-arduino.charH());
   arduino.println("Wind Subscriber", Color::GRAY);

   arduino.println("Initializing", Color::HEADING2);
   arduino.moveCursorY(4);

   arduino.print("WiFi...", Color::LABEL);
   while (WiFi.status() != WL_CONNECTED)
   {
      arduino.print(".", Color::LABEL);
   }
   arduino.printlnR("OK", Color::VALUE);
   arduino.moveCursorY(1);

   arduino.print("WebSocket...", Color::LABEL);

   client.setCallbacks(nullptr, onDisconnected, nullptr, nullptr, onError, onStarted);
   client.beginSSL(TELEMETRY_HOST, TELEMETRY_PORT);


   delay(1000); // provide time for the wind meter to get a reading

   sw.reset();
}

void onStarted()
{
   arduino.clearDisplay();
}

void onError(std::string msg)
{
   arduino.setTextSize(2);
   arduino.clearDisplay();
   arduino.display.setTextWrap(true);
   arduino.println(msg, Color::WHITE, Color::RED);
   Util::reset(10);
}

void onDisconnected()
{
   Util::reset();
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

   arduino.print("Wind", Color::HEADING);
   arduino.printR(speed, speedFormat, Color::VALUE);
   arduino.moveCursorY(4);

   if (arduino.buttonA.wasPressed())
   {
      mode++;

      arduino.display.fillRect(0, arduino.display.getCursorY(), arduino.display.width(), arduino.display.height() - arduino.display.getCursorY(), (uint16_t)Color::BLACK);
      multiBar.reset();
      rollingChart.reset();
      histogramChart.reset();
   }

   switch (mode)
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

   if (sw.elapsedSecs() > 1)
   {
      Serial.println(refreshRate.get());
      Serial.println(speed);
      sw.reset();
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
Format AxisValueC("##.#", Format::Alignment::CENTER);
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
