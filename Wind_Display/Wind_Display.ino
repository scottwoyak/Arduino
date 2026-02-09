
#include "Feather.h"
#include "SerialX.h"
#include "TimedAverager.h"
#include "TimedHistogramChart.h"
#include "WindMeter.h"
#include "Slider.h"
#include "MultiBar.h"
#include "BarChart.h"

Feather feather;
WindMeter wind(A5);

constexpr uint16_t WIND_AVERAGE_DURATION_S = 10 * 60;
constexpr uint8_t WIND_AVERAGE_INTERVAL_S = 10;     
constexpr uint8_t WIND_AVERAGE_BINS = WIND_AVERAGE_DURATION_S / WIND_AVERAGE_INTERVAL_S;
TimedAverager windAverager(WIND_AVERAGE_DURATION_S*1000, WIND_AVERAGE_BINS);

Format speedFormat("##.# mph");

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
RollingBarChart rollingChart(GRAPH_RECT, GRAPH_RANGE, Green2, Color::BLACK);

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

   speedFormat.alignment = Format::Alignment::RIGHT;

   SerialX::begin();
   feather.begin();
   wind.begin();

   delay(1000); // provide time for the wind meter to get a reading
}

void loop()
{
   // get values
   float speed = wind.getSpeed();
   windAverager.set(speed);
   multiBar.set(speed);
   rollingChart.set(speed);
   histogramChart.set(speed);
   slider.set(speed);

   // display values
   feather.setCursor(0, 0);
   feather.setTextSize(3);

   feather.print("Wind", Color::HEADING);
   feather.setCursorX(-feather.charW() * speedFormat.length); // right align the speed
   feather.println(speed, speedFormat, Color::VALUE);
   feather.moveCursorY(4);

   if (feather.buttonA.wasPressed())
   {
      mode++;

      feather.display.fillRect(0, feather.display.getCursorY(), feather.display.width(), feather.display.height() - feather.display.getCursorY(), (uint16_t)Color::BLACK);
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
}

void displayMultiBar(float speed)
{
   feather.setTextSize(2);
   feather.println("Last 10 Minutes...", Color::LABEL);
   feather.moveCursorY(1);

   feather.print("Min: ", Color::LABEL);
   feather.println(windAverager.getMin(), speedFormat, Color::VALUE);
   feather.moveCursorY(1);

   feather.print("Max: ", Color::LABEL);
   feather.println(windAverager.getMax(), speedFormat, Color::VALUE);
   feather.moveCursorY(1);

   feather.print("Avg: ", Color::LABEL);
   feather.println(windAverager.get(), speedFormat, Color::VALUE);

   // display bar
   multiBar.draw(&feather.display);
}

void displayRollingChart(float speed)
{
   rollingChart.draw(&feather.display);
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

   histogramChart.draw(&feather.display);
   slider.draw(&feather.display);

   feather.setTextSize(2);
   feather.setCursor(0, -15);

   RangeF displayRange = histogramChart.getVisibleRange();
   feather.print(displayRange.min, AxisValueL, Color::GRAY);
   feather.printC((displayRange.min + displayRange.max)/2, AxisValueL, Color::GRAY);
   feather.printR(displayRange.max, AxisValueR, Color::GRAY);
   uint16_t y = DISPLAY_HEIGHT - VALUES_AXIS_HEIGHT + 1;
   feather.display.drawLine(0, y, feather.display.width(), y, (uint16_t)Color::GRAY);
}