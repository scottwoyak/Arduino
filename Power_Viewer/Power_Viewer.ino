//
// Power Viewer
//
// Subscribes to live solar production and home power consumption telemetry published by
// the Power Hub (Power/Total, Power/TotalDaily, Power/Solar, Power/SolarDaily, and the
// per-building Power/House*, Power/Garage*, and Power/Barn* topics) over a WebSocket
// connection and renders a dual-section live and daily view.
//
// The screen is divided vertically into two independently tappable sections under the
// "Power" title, with the selected mode for each section persisted in Preferences:
// - "Live" (tap top section to toggle):
//   1. Summary: A 10-minute center-zero rolling bar chart of recent power (production,
//      usage, and signed net delta in kW), followed by a single-row table of live Solar,
//      Usage, and Net kW readings.
//   2. Breakdown: Four 0-20 kW horizontal bar rows showing live power for Solar, House,
//      Garage, and Barn with right-aligned kW readouts.
// - "Today" (tap bottom section to toggle):
//   1. Summary: A 24-hour center-zero chart anchored at local midnight showing cumulative
//      daily energy (production, usage, and signed net delta in kWh), followed by Today's
//      Solar, Usage, and Net kWh totals.
//   2. Breakdown: Four 0-100 kWh vertical bars showing cumulative daily energy for Solar,
//      House, Garage, and Barn with centered kWh readouts above and labels below each bar.
//
// The layout adapts at runtime to the display's dimensions, supporting both large
// displays (e.g. the 480x320 Hosyond ESP32-S3 Viewer board) and compact displays
// (e.g. the 240x135 Feather ESP32-S3 TFT).
//

#include <Arduino.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>
#include <sstream>
#include <string>

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Hosyond ESP32-S3 Viewer or Feather ESP32-S3)."
#include "WrongBoard.h"
#endif

#include "Bar.h"
#include "DisplayValue.h"
#include "Field.h"
#include "FieldRow.h"
#include "LibraryVersion.h"
#include "MovingBarChart.h"
#include "SerialX.h"
#include "Status.h"
#include "TelemetryClient.h"
#include "TimeSync.h"
#include "Timer.h"
#include "WiFiSettings.h"
#include "ViewerSketch.h"

const auto VERSION = MakeVersion("1.05");
constexpr auto SKETCH_NAME = "Power_Viewer";
constexpr auto PREFERENCES_NAMESPACE = SKETCH_NAME;
constexpr auto LEGACY_PREFERENCES_NAMESPACE = "SolarViewer";

// ----------- Telemetry Configuration
constexpr auto POWER_TOTAL_TOPIC = "Power/Total";
constexpr auto POWER_DAILY_TOPIC = "Power/TotalDaily";
constexpr auto POWER_SOLAR_TOPIC = "Power/Solar";
constexpr auto POWER_SOLAR_DAILY_TOPIC = "Power/SolarDaily";
constexpr auto POWER_HOUSE_TOPIC = "Power/House";
constexpr auto POWER_HOUSE_DAILY_TOPIC = "Power/HouseDaily";
constexpr auto POWER_GARAGE_TOPIC = "Power/Garage";
constexpr auto POWER_GARAGE_DAILY_TOPIC = "Power/GarageDaily";
constexpr auto POWER_BARN_TOPIC = "Power/Barn";
constexpr auto POWER_BARN_DAILY_TOPIC = "Power/BarnDaily";

constexpr uint16_t DISPLAY_UPDATE_MS = 200;
constexpr uint16_t CHART_SAMPLE_MS = 200;
constexpr auto TODAY_CHART_DAY_PREF_KEY = "todayDay";
constexpr auto TODAY_CHART_BINS_PREF_KEY = "todayBins";
constexpr auto LIVE_VIEW_PREF_KEY = "liveView";
constexpr auto TODAY_VIEW_PREF_KEY = "todayView";
constexpr uint16_t EMPTY_TENTHS_KWH = 0xFFFF;
constexpr uint16_t MAX_TODAY_CHART_COLS = 480;
constexpr uint8_t NUM_BREAKDOWN_BARS = 4;

// Full-scale power (in kW) and daily energy (in kWh) for the charts and bar gauges.
constexpr float MAX_POWER_KW = 20.0f;
constexpr float MAX_ENERGY_KWH = 200.0f;
constexpr float MAX_TODAY_BAR_KWH = 100.0f;
constexpr float WATTS_PER_KW = 1000.0f;

// ----------- Display Configuration
constexpr auto LIVE_SECTION_TEXT = "Live";
constexpr auto TODAY_SECTION_TEXT = "Today";
constexpr uint8_t HEADING_TEXT_SIZE = DEFAULT_HEADING_SIZE;
constexpr uint8_t TABLE_TEXT_SIZE = DEFAULT_FOOTER_SIZE;
constexpr int16_t OUTER_MARGIN_PX = 4;
constexpr int16_t ROW_GAP_PX = 3;
constexpr int16_t SECTION_GAP_LARGE_PX = 20;
constexpr int16_t SECTION_GAP_SMALL_PX = 6;
constexpr int16_t HEADING_GAP_PX = 4;
constexpr int16_t CHART_LABEL_GAP_PX = 4;
constexpr int16_t BAR_ROW_GAP_LARGE_PX = 10;
constexpr int16_t BAR_ROW_GAP_SMALL_PX = 3;
constexpr int16_t BAR_MAX_HEIGHT_LARGE_PX = 22;
constexpr int16_t TODAY_BAR_MIN_WIDTH_PX = 12;
constexpr int16_t TODAY_BAR_MAX_WIDTH_PX = 80;
constexpr uint8_t LIVE_BAR_LABEL_CHARS = 6;
constexpr uint8_t TODAY_LABEL_CHARS = 4;
constexpr float ROTATED_HEADING_ANGLE_DEG = 270.0f;
constexpr uint16_t ROLLING_CHART_SPAN_M = 10;
constexpr uint32_t SECONDS_PER_DAY = 24UL * 60UL * 60UL;

constexpr Color PRODUCTION_COLOR = Color565::fromRGB(0, 72, 0);
constexpr Color CONSUMPTION_COLOR = Color565::fromRGB(0, 48, 128);
constexpr Color TODAY_GRID_COLOR = Color565::fromRGB(48, 48, 48);
constexpr Color BAR_TRACK_COLOR = Color565::fromRGB(32, 32, 32);
constexpr Color DELTA_POSITIVE_COLOR = Color::LIME;
constexpr Color DELTA_NEGATIVE_COLOR = Color::DODGERBLUE;
constexpr Color SOLAR_COLOR = Color::LIME;
constexpr Color HOUSE_COLOR = Color::DODGERBLUE;
constexpr Color GARAGE_COLOR = Color::ORANGE;
constexpr Color BARN_COLOR = Color::VIOLET;

// ----------- Sketch & State
SketchConfig SKETCH_CONFIG = {
   .sketchName = SKETCH_NAME,
   .version = VERSION,
   .preferencesNamespace = PREFERENCES_NAMESPACE,
   .enableOTA = true,
   .enableRebooter = true,
};

ViewerSketch sketch(SKETCH_CONFIG);
Timer displayTimer(DISPLAY_UPDATE_MS);
Timer chartSampleTimer(CHART_SAMPLE_MS);

///
/// <summary>
/// Display modes for the top ("Live") section.
/// </summary>
///
enum class LiveViewMode : uint8_t
{
   SUMMARY = 0,
   BREAKDOWN = 1,
};

///
/// <summary>
/// Display modes for the bottom ("Today") section.
/// </summary>
///
enum class TodayViewMode : uint8_t
{
   SUMMARY = 0,
   BREAKDOWN = 1,
};

///
/// <summary>
/// Holds the latest solar production, whole-home consumption, and per-building (House,
/// Garage, and Barn) power and daily energy readings received from telemetry.
/// </summary>
///
struct SolarReadings
{
   float productionKw = NAN;
   float productionTodayKwh = NAN;
   float consumptionKw = NAN;
   float consumptionTodayKwh = NAN;
   float houseKw = NAN;
   float houseTodayKwh = NAN;
   float garageKw = NAN;
   float garageTodayKwh = NAN;
   float barnKw = NAN;
   float barnTodayKwh = NAN;

   ///
   /// <summary>
   /// Returns whether at least one telemetry metric has been received.
   /// </summary>
   /// <returns>True if any power or daily energy field is non-NaN; otherwise false.</returns>
   ///
   bool hasAnyReading() const
   {
      return !std::isnan(productionKw) ||
         !std::isnan(productionTodayKwh) ||
         !std::isnan(consumptionKw) ||
         !std::isnan(consumptionTodayKwh) ||
         !std::isnan(houseKw) ||
         !std::isnan(houseTodayKwh) ||
         !std::isnan(garageKw) ||
         !std::isnan(garageTodayKwh) ||
         !std::isnan(barnKw) ||
         !std::isnan(barnTodayKwh);
   }
};

SolarReadings latestReadings;
float netPowerKw = NAN;
float netEnergyKwh = NAN;
bool readingsDirty = false;
LiveViewMode liveViewMode = LiveViewMode::SUMMARY;
TodayViewMode todayViewMode = TodayViewMode::SUMMARY;

void onTelemetrySample(const std::string& topic, double value, int64_t dtMicros);

///
/// <summary>
/// Handles telemetry WebSocket lifecycle events: configures the sample callback and
/// subscribes to all solar, total, and per-building power topics on the connection, and
/// flags a full screen redraw if an error message overwrote the display.
/// </summary>
///
class SolarTelemetryHandler : public TelemetryViewerEventHandler
{
private:
   bool _configured = false;

public:
   ///
   /// <summary>
   /// Initializes a new instance of the SolarTelemetryHandler class.
   /// </summary>
   /// <param name="status">Status indicator updated by telemetry lifecycle events.</param>
   ///
   explicit SolarTelemetryHandler(IStatus* status) : TelemetryViewerEventHandler(status, &sketch.arduino) {}

   ///
   /// <summary>
   /// Registers the sample handler and subscribes the client to all solar, total, and
   /// per-building power telemetry topics once.
   /// </summary>
   /// <param name="client">Active telemetry subscriber client.</param>
   ///
   void configure(TelemetrySubscriber* client)
   {
      ASSERT(client != nullptr);

      if (_configured)
      {
         return;
      }

      _configured = true;
      client->onSample(onTelemetrySample);
      client->setTopics({
         POWER_SOLAR_TOPIC,
         POWER_SOLAR_DAILY_TOPIC,
         POWER_TOTAL_TOPIC,
         POWER_DAILY_TOPIC,
         POWER_HOUSE_TOPIC,
         POWER_HOUSE_DAILY_TOPIC,
         POWER_GARAGE_TOPIC,
         POWER_GARAGE_DAILY_TOPIC,
         POWER_BARN_TOPIC,
         POWER_BARN_DAILY_TOPIC,
      });
   }

   void onConnected() override
   {
      TelemetryViewerEventHandler::onConnected();
      configure(sketch.getClient());
   }

   void onError(const std::string& message) override
   {
      TelemetryViewerEventHandler::onError(message);
      needsFullRedraw = true;
   }
};

SolarTelemetryHandler telemetryHandler(sketch.getStatus());

Field nowSolarField(&sketch.arduino, "Solar", "##.# kW", TABLE_TEXT_SIZE);
Field nowUsageField(&sketch.arduino, "Usage", "##.# kW", TABLE_TEXT_SIZE);
Field nowNetField(&sketch.arduino, "Net", "##.# kW", TABLE_TEXT_SIZE);
FieldRow nowSummaryRow(&nowSolarField, &nowUsageField, &nowNetField, Color::GRAY);

std::array<DisplayValue, NUM_BREAKDOWN_BARS> liveBarValues = {
   DisplayValue(&sketch.arduino, Format("##.# kW"), TABLE_TEXT_SIZE, DisplayValue::Alignment::RIGHT),
   DisplayValue(&sketch.arduino, Format("##.# kW"), TABLE_TEXT_SIZE, DisplayValue::Alignment::RIGHT),
   DisplayValue(&sketch.arduino, Format("##.# kW"), TABLE_TEXT_SIZE, DisplayValue::Alignment::RIGHT),
   DisplayValue(&sketch.arduino, Format("##.# kW"), TABLE_TEXT_SIZE, DisplayValue::Alignment::RIGHT),
};

Field todaySolarField(&sketch.arduino, "Solar", "###.# kWh", TABLE_TEXT_SIZE);
Field todayUsageField(&sketch.arduino, "Usage", "###.# kWh", TABLE_TEXT_SIZE);
Field todayNetField(&sketch.arduino, "Net", "###.# kWh", TABLE_TEXT_SIZE);
FieldRow todaySummaryRow(&todaySolarField, &todayUsageField, &todayNetField, Color::GRAY);

std::array<DisplayValue, NUM_BREAKDOWN_BARS> todayBarValues = {
   DisplayValue(&sketch.arduino, Format("###.#"), TABLE_TEXT_SIZE, DisplayValue::Alignment::CENTER),
   DisplayValue(&sketch.arduino, Format("###.#"), TABLE_TEXT_SIZE, DisplayValue::Alignment::CENTER),
   DisplayValue(&sketch.arduino, Format("###.#"), TABLE_TEXT_SIZE, DisplayValue::Alignment::CENTER),
   DisplayValue(&sketch.arduino, Format("###.#"), TABLE_TEXT_SIZE, DisplayValue::Alignment::CENTER),
};

constexpr std::array<const char*, NUM_BREAKDOWN_BARS> BREAKDOWN_BAR_LABELS = { "Solar", "House", "Garage", "Barn" };
constexpr std::array<Color, NUM_BREAKDOWN_BARS> BREAKDOWN_BAR_COLORS = { SOLAR_COLOR, HOUSE_COLOR, GARAGE_COLOR, BARN_COLOR };

///
/// <summary>
/// Stores a single column of the 24-hour Summary Today chart in tenths of a kWh (or
/// EMPTY_TENTHS_KWH when the column has not yet been recorded today).
/// </summary>
///
struct TodayChartSample
{
   uint16_t prodTenthsKwh = EMPTY_TENTHS_KWH;
   uint16_t consTenthsKwh = EMPTY_TENTHS_KWH;
};

MovingBarChart* rollingChart = nullptr;
MovingBarChart* todayChart = nullptr;
std::array<HorizontalBar*, NUM_BREAKDOWN_BARS> liveBars = { nullptr, nullptr, nullptr, nullptr };
std::array<int16_t, NUM_BREAKDOWN_BARS> liveBarTextY = { 0, 0, 0, 0 };
std::array<VerticalBar*, NUM_BREAKDOWN_BARS> todayBars = { nullptr, nullptr, nullptr, nullptr };
std::array<int16_t, NUM_BREAKDOWN_BARS> todayBarCenterX = { 0, 0, 0, 0 };
std::array<TodayChartSample, MAX_TODAY_CHART_COLS> todayChartSamples;

Rect16 rollingChartRect;
Rect16 todayChartRect;
Rect16 todayBarAreaRect;
Rect16 liveSectionRect;
Rect16 todaySectionRect;
Rect16 liveTouchRect;
Rect16 todayTouchRect;
int16_t todayBarValueY = 0;
int16_t todayBarLabelY = 0;
int16_t headingColWidth = 0;
int16_t contentLeftX = 0;
uint8_t sectionTextSize = TABLE_TEXT_SIZE;
int16_t chartLabelRightX = 0;
int32_t todayChartDayId = -1;
bool isLargeDisplay = false;
Timer rollingColumnTimer(CHART_SAMPLE_MS);
float rollingColumnSum = 0.0f;
float rollingProdSum = 0.0f;
float rollingConsSum = 0.0f;
uint16_t rollingColumnSamples = 0;

///
/// <summary>
/// Returns the display color for signed net delta values.
/// </summary>
/// <param name="value">The signed net delta value.</param>
/// <returns>DELTA_POSITIVE_COLOR when non-negative, DELTA_NEGATIVE_COLOR when negative.</returns>
///
Color deltaValueColor(float value)
{
   return value >= 0.0f ? DELTA_POSITIVE_COLOR : DELTA_NEGATIVE_COLOR;
}

///
/// <summary>
/// Updates and renders the dynamic widgets for the active "Live" section mode.
/// </summary>
///
void drawLiveDynamic()
{
   if (liveViewMode == LiveViewMode::SUMMARY)
   {
      nowSummaryRow.draw(
         latestReadings.productionKw,
         DELTA_POSITIVE_COLOR,
         latestReadings.consumptionKw,
         DELTA_NEGATIVE_COLOR,
         std::isnan(netPowerKw) ? NAN : fabsf(netPowerKw),
         deltaValueColor(netPowerKw));
   }
   else
   {
      const std::array<float, NUM_BREAKDOWN_BARS> barValues = {
         latestReadings.productionKw,
         latestReadings.houseKw,
         latestReadings.garageKw,
         latestReadings.barnKw,
      };
      for (uint8_t i = 0; i < NUM_BREAKDOWN_BARS; i++)
      {
         float kw = barValues[i];
         liveBars[i]->set(std::isnan(kw) ? 0.0f : kw);
         liveBars[i]->draw(&sketch.arduino.display);
         liveBarValues[i].draw(kw, BREAKDOWN_BAR_COLORS[i]);
      }
   }
}

///
/// <summary>
/// Updates and renders the dynamic widgets for the active "Today" section mode.
/// </summary>
///
void drawTodayDynamic()
{
   if (todayViewMode == TodayViewMode::SUMMARY)
   {
      todaySummaryRow.draw(
         latestReadings.productionTodayKwh,
         DELTA_POSITIVE_COLOR,
         latestReadings.consumptionTodayKwh,
         DELTA_NEGATIVE_COLOR,
         std::isnan(netEnergyKwh) ? NAN : fabsf(netEnergyKwh),
         deltaValueColor(netEnergyKwh));
   }
   else
   {
      const std::array<float, NUM_BREAKDOWN_BARS> barValues = {
         latestReadings.productionTodayKwh,
         latestReadings.houseTodayKwh,
         latestReadings.garageTodayKwh,
         latestReadings.barnTodayKwh,
      };
      for (uint8_t i = 0; i < NUM_BREAKDOWN_BARS; i++)
      {
         float kwh = barValues[i];
         todayBars[i]->set(std::isnan(kwh) ? 0.0f : kwh);
         todayBars[i]->draw(&sketch.arduino.display);
         todayBarValues[i].draw(kwh, BREAKDOWN_BAR_COLORS[i]);
      }
   }
}

///
/// <summary>
/// Updates and renders the dynamic value widgets for both the Live and Today sections.
/// </summary>
///
void drawFields()
{
   drawLiveDynamic();
   drawTodayDynamic();
}

///
/// <summary>
/// Computes the responsive display layout for both modes of the "Live" section and both
/// modes of the "Today" section, along with touch hit-test rectangles.
/// </summary>
///
void initLayout()
{
   int16_t displayWidth = sketch.arduino.width();
   int16_t displayHeight = sketch.arduino.height();
   isLargeDisplay = displayHeight >= LARGE_DISPLAY_MIN_HEIGHT;

   int16_t titleHeight = sketch.arduino.charH(HEADING_TEXT_SIZE);
   int16_t rowHeight = sketch.arduino.charH(TABLE_TEXT_SIZE);
   int16_t charWidth = sketch.arduino.charW(TABLE_TEXT_SIZE);

   sectionTextSize = isLargeDisplay ? static_cast<uint8_t>(TABLE_TEXT_SIZE + 1) : TABLE_TEXT_SIZE;
   headingColWidth = sketch.arduino.charH(sectionTextSize);
   int16_t topGap = isLargeDisplay ? OUTER_MARGIN_PX : 1;
   int16_t itemGap = isLargeDisplay ? ROW_GAP_PX : 1;
   int16_t sectionGap = isLargeDisplay ? SECTION_GAP_LARGE_PX : SECTION_GAP_SMALL_PX;

   // Place content (Y-axis labels + chart, and the one-row value tables) to the right of
   // the rotated section heading column.
   contentLeftX = headingColWidth + HEADING_GAP_PX;
   chartLabelRightX = contentLeftX + charWidth * TODAY_LABEL_CHARS;
   int16_t chartX = chartLabelRightX + CHART_LABEL_GAP_PX;
   int16_t chartWidth = max(static_cast<int16_t>(16), static_cast<int16_t>(displayWidth - chartX));

   // With the headings rotated on the left, all remaining vertical space goes to the two
   // charts and two single-row tables.
   int16_t fixedHeight = titleHeight + topGap +
      itemGap + rowHeight + sectionGap +
      itemGap + rowHeight;
   int16_t totalChartHeight = max(static_cast<int16_t>(32), static_cast<int16_t>(displayHeight - fixedHeight));
   int16_t nowChartHeight = totalChartHeight / 2;
   int16_t todayChartHeight = totalChartHeight - nowChartHeight;

   int16_t rollingChartY = titleHeight + topGap;
   int16_t nowRowY = rollingChartY + nowChartHeight + itemGap;
   int16_t liveBottomY = nowRowY + rowHeight;

   int16_t todayChartY = liveBottomY + sectionGap;
   int16_t todayRowY = todayChartY + todayChartHeight + itemGap;
   int16_t todayBottomY = todayRowY + rowHeight;

   rollingChartRect = Rect16(chartX, rollingChartY, chartWidth, nowChartHeight);
   todayChartRect = Rect16(chartX, todayChartY, chartWidth, todayChartHeight);

   liveSectionRect = Rect16(contentLeftX, rollingChartY, displayWidth - contentLeftX, liveBottomY - rollingChartY);
   todaySectionRect = Rect16(contentLeftX, todayChartY, displayWidth - contentLeftX, todayBottomY - todayChartY);

   int16_t splitY = (liveBottomY + todayChartY) / 2;
   liveTouchRect = Rect16(0, rollingChartY, displayWidth, splitY - rollingChartY);
   todayTouchRect = Rect16(0, splitY, displayWidth, displayHeight - splitY);

   nowSummaryRow.setBounds(chartX, nowRowY, chartWidth);
   todaySummaryRow.setBounds(chartX, todayRowY, chartWidth);

   // Layout for the Live Breakdown 4-row horizontal bar view (Label + 0-20 kW Bar + kW value)
   int16_t barGap = isLargeDisplay ? BAR_ROW_GAP_LARGE_PX : BAR_ROW_GAP_SMALL_PX;
   int16_t bandHeight = max<int16_t>(rowHeight, (liveSectionRect.height - (NUM_BREAKDOWN_BARS - 1) * barGap) / NUM_BREAKDOWN_BARS);
   int16_t barHeight = min<int16_t>(bandHeight, isLargeDisplay ? BAR_MAX_HEIGHT_LARGE_PX : rowHeight);
   int16_t totalRowsHeight = NUM_BREAKDOWN_BARS * bandHeight + (NUM_BREAKDOWN_BARS - 1) * barGap;
   int16_t firstBandY = liveSectionRect.y + max<int16_t>(0, (liveSectionRect.height - totalRowsHeight) / 2);

   int16_t barLeftX = contentLeftX + LIVE_BAR_LABEL_CHARS * charWidth + CHART_LABEL_GAP_PX;
   int16_t barRightX = displayWidth - liveBarValues[0].width() - CHART_LABEL_GAP_PX;
   int16_t barWidth = max<int16_t>(16, barRightX - barLeftX);

   RangeF barPowerRange(0.0f, MAX_POWER_KW);

   for (uint8_t i = 0; i < NUM_BREAKDOWN_BARS; i++)
   {
      int16_t bandY = firstBandY + i * (bandHeight + barGap);
      int16_t barY = bandY + (bandHeight - barHeight) / 2;
      int16_t textY = bandY + (bandHeight - rowHeight) / 2;

      liveBarTextY[i] = textY;
      liveBarValues[i].setPosition(displayWidth, textY);
      liveBars[i] = new HorizontalBar(
         Rect16(barLeftX, barY, barWidth, barHeight),
         barPowerRange,
         BREAKDOWN_BAR_COLORS[i],
         BAR_TRACK_COLOR);
   }

   // Layout for the Today Breakdown 4-column vertical bar view (0-100 kWh for Solar, House,
   // Garage, and Barn, with numeric kWh readouts above and labels below each bar).
   todayBarValueY = todayChartRect.y;
   todayBarLabelY = todayRowY;
   int16_t todayBarTopY = todayBarValueY + rowHeight + itemGap;
   int16_t todayBarBaselineY = todayChartRect.y + todayChartRect.height - 1;
   int16_t todayBarHeight = max<int16_t>(8, todayBarBaselineY - todayBarTopY);
   todayBarAreaRect = Rect16(chartX, todayBarTopY, chartWidth, todayBarHeight);

   int16_t slotWidth = chartWidth / NUM_BREAKDOWN_BARS;
   int16_t todayBarWidth = std::clamp<int16_t>(slotWidth / 2, TODAY_BAR_MIN_WIDTH_PX, TODAY_BAR_MAX_WIDTH_PX);
   RangeF todayBarEnergyRange(0.0f, MAX_TODAY_BAR_KWH);

   for (uint8_t i = 0; i < NUM_BREAKDOWN_BARS; i++)
   {
      int16_t slotLeft = chartX + (chartWidth * i) / NUM_BREAKDOWN_BARS;
      int16_t slotRight = chartX + (chartWidth * (i + 1)) / NUM_BREAKDOWN_BARS;
      int16_t centerX = (slotLeft + slotRight) / 2;
      int16_t barX = centerX - todayBarWidth / 2;

      todayBarCenterX[i] = centerX;
      todayBarValues[i].setPosition(centerX, todayBarValueY);
      todayBars[i] = new VerticalBar(
         Rect16(barX, todayBarAreaRect.y, todayBarWidth, todayBarAreaRect.height),
         todayBarEnergyRange,
         BREAKDOWN_BAR_COLORS[i],
         BAR_TRACK_COLOR);
   }

   RangeF powerRange(-MAX_POWER_KW, MAX_POWER_KW);
   RangeF energyRange(-MAX_ENERGY_KWH, MAX_ENERGY_KWH);

   uint32_t columnPeriodMs = (ROLLING_CHART_SPAN_M * 60UL * 1000UL) / static_cast<uint32_t>(chartWidth);
   rollingColumnTimer.setDurationMs(max(static_cast<uint32_t>(CHART_SAMPLE_MS), columnPeriodMs));
   rollingChart = new MovingBarChart(
      rollingChartRect,
      powerRange,
      DELTA_POSITIVE_COLOR,
      DELTA_NEGATIVE_COLOR,
      Color::BLACK,
      Color::GRAY);
   rollingChart->setSecondaryColors(PRODUCTION_COLOR, CONSUMPTION_COLOR);

   todayChart = new MovingBarChart(
      todayChartRect,
      energyRange,
      DELTA_POSITIVE_COLOR,
      DELTA_NEGATIVE_COLOR,
      Color::BLACK,
      Color::GRAY);
   todayChart->setSecondaryColors(PRODUCTION_COLOR, CONSUMPTION_COLOR);
}

///
/// <summary>
/// Draws the right-aligned Y-axis labels (top, middle, and bottom) in gray to the left of
/// a chart.
/// </summary>
/// <param name="chartRect">Bounding rectangle of the chart.</param>
/// <param name="topLabel">Top full-scale label text.</param>
/// <param name="middleLabel">Midpoint label text.</param>
/// <param name="bottomLabel">Bottom label text.</param>
///
void drawChartLabels(
   const Rect16& chartRect,
   const char* topLabel,
   const char* middleLabel,
   const char* bottomLabel)
{
   int16_t charWidth = sketch.arduino.charW(TABLE_TEXT_SIZE);
   int16_t charHeight = sketch.arduino.charH(TABLE_TEXT_SIZE);

   int16_t topY = chartRect.y;
   int16_t centerY = chartRect.y + (chartRect.height - charHeight) / 2;
   int16_t bottomY = chartRect.y + chartRect.height - charHeight;

   sketch.arduino.setTextSize(TABLE_TEXT_SIZE);

   int16_t topChars = static_cast<int16_t>(strlen(topLabel));
   sketch.arduino.setCursor(chartLabelRightX - topChars * charWidth, topY);
   sketch.arduino.print(topLabel, Color::GRAY);

   int16_t middleChars = static_cast<int16_t>(strlen(middleLabel));
   sketch.arduino.setCursor(chartLabelRightX - middleChars * charWidth, centerY);
   sketch.arduino.print(middleLabel, Color::GRAY);

   int16_t bottomChars = static_cast<int16_t>(strlen(bottomLabel));
   sketch.arduino.setCursor(chartLabelRightX - bottomChars * charWidth, bottomY);
   sketch.arduino.print(bottomLabel, Color::GRAY);
}

///
/// <summary>
/// Draws faint vertical reference lines across the Today 24-hour chart at 06:00 (25%),
/// 12:00 (50%), and 18:00 (75%).
/// </summary>
///
void drawTodayGridLines()
{
   uint16_t gridColor = static_cast<uint16_t>(TODAY_GRID_COLOR);
   for (uint8_t quarter = 1; quarter <= 3; quarter++)
   {
      int16_t lineX = todayChartRect.x + (todayChartRect.width * quarter) / 4;
      sketch.arduino.display.drawFastVLine(lineX, todayChartRect.y, todayChartRect.height, gridColor);
   }
}

///
/// <summary>
/// Returns the current local calendar day encoded as (year * 1000 + yday), or -1 if the
/// system clock has not yet been synchronized via NTP.
/// </summary>
/// <returns>Encoded local calendar day, or -1 if time is not synced.</returns>
///
int32_t currentLocalDayId()
{
   if (!TimeSync::isSynced())
   {
      return -1;
   }

   time_t now = time(nullptr);
   struct tm timeInfo;
   localtime_r(&now, &timeInfo);
   return static_cast<int32_t>(timeInfo.tm_year) * 1000 + static_cast<int32_t>(timeInfo.tm_yday);
}

///
/// <summary>
/// Clears all in-memory Today Summary chart samples to their unrecorded state.
/// </summary>
///
void clearTodayChartSamples()
{
   todayChartSamples.fill({ EMPTY_TENTHS_KWH, EMPTY_TENTHS_KWH });
}

///
/// <summary>
/// Applies any persisted Today Summary chart column samples for the current day into todayChart.
/// </summary>
///
void populateTodayChartFromSamples()
{
   uint16_t numCols = min(todayChart->getNumBars(), MAX_TODAY_CHART_COLS);
   for (uint16_t i = 0; i < numCols; i++)
   {
      const TodayChartSample& sample = todayChartSamples[i];
      if (sample.prodTenthsKwh != EMPTY_TENTHS_KWH && sample.consTenthsKwh != EMPTY_TENTHS_KWH)
      {
         float prodKwh = sample.prodTenthsKwh * 0.1f;
         float consKwh = sample.consTenthsKwh * 0.1f;
         float netKwh = prodKwh - consKwh;
         todayChart->setAt(i, netKwh, prodKwh, consKwh);
      }
   }
}

///
/// <summary>
/// Clears the in-memory and Preferences-persisted Today chart samples for a new calendar day.
/// </summary>
/// <param name="dayId">The new encoded local calendar day ID.</param>
///
void resetTodayChartHistory(int32_t dayId)
{
   clearTodayChartSamples();
   todayChartDayId = dayId;

   sketch.arduino.preferences.begin(PREFERENCES_NAMESPACE, false);
   sketch.arduino.preferences.putInt(TODAY_CHART_DAY_PREF_KEY, todayChartDayId);
   sketch.arduino.preferences.remove(TODAY_CHART_BINS_PREF_KEY);
   sketch.arduino.preferences.end();
}

///
/// <summary>
/// Loads the persisted Live and Today view mode selections from Preferences.
/// </summary>
///
void loadViewPreferences()
{
   if (sketch.arduino.preferences.begin(PREFERENCES_NAMESPACE, true))
   {
      uint8_t savedLive = sketch.arduino.preferences.getUChar(LIVE_VIEW_PREF_KEY, 0);
      uint8_t savedToday = sketch.arduino.preferences.getUChar(TODAY_VIEW_PREF_KEY, 0);
      sketch.arduino.preferences.end();

      liveViewMode = (savedLive == static_cast<uint8_t>(LiveViewMode::BREAKDOWN))
         ? LiveViewMode::BREAKDOWN
         : LiveViewMode::SUMMARY;
      todayViewMode = (savedToday == static_cast<uint8_t>(TodayViewMode::BREAKDOWN))
         ? TodayViewMode::BREAKDOWN
         : TodayViewMode::SUMMARY;
   }
}

///
/// <summary>
/// Persists the current Live and Today view mode selections to Preferences.
/// </summary>
///
void saveViewPreferences()
{
   sketch.arduino.preferences.begin(PREFERENCES_NAMESPACE, false);
   sketch.arduino.preferences.putUChar(LIVE_VIEW_PREF_KEY, static_cast<uint8_t>(liveViewMode));
   sketch.arduino.preferences.putUChar(TODAY_VIEW_PREF_KEY, static_cast<uint8_t>(todayViewMode));
   sketch.arduino.preferences.end();
}

///
/// <summary>
/// Loads the persisted Today Summary chart samples from Preferences if they match today's
/// local calendar day; otherwise resets the stored samples for the new day.
/// </summary>
///
void loadTodayChartHistory()
{
   clearTodayChartSamples();

   int32_t todayId = currentLocalDayId();
   uint16_t numCols = min(todayChart->getNumBars(), MAX_TODAY_CHART_COLS);
   size_t expectedBytes = static_cast<size_t>(numCols) * sizeof(TodayChartSample);

   if (sketch.arduino.preferences.begin(PREFERENCES_NAMESPACE, true))
   {
      int32_t savedDayId = sketch.arduino.preferences.getInt(TODAY_CHART_DAY_PREF_KEY, -1);
      if (todayId >= 0 && savedDayId == todayId &&
          sketch.arduino.preferences.getBytesLength(TODAY_CHART_BINS_PREF_KEY) == expectedBytes)
      {
         sketch.arduino.preferences.getBytes(TODAY_CHART_BINS_PREF_KEY, todayChartSamples.data(), expectedBytes);
         sketch.arduino.preferences.end();
         todayChartDayId = todayId;
         return;
      }
      sketch.arduino.preferences.end();
   }

   Preferences legacyPrefs;
   if (todayId >= 0 && legacyPrefs.begin(LEGACY_PREFERENCES_NAMESPACE, true))
   {
      int32_t legacyDayId = legacyPrefs.getInt(TODAY_CHART_DAY_PREF_KEY, -1);
      size_t legacyBytes = legacyPrefs.getBytesLength(TODAY_CHART_BINS_PREF_KEY);
      if (legacyDayId == todayId && legacyBytes == expectedBytes)
      {
         legacyPrefs.getBytes(TODAY_CHART_BINS_PREF_KEY, todayChartSamples.data(), expectedBytes);
         legacyPrefs.end();

         todayChartDayId = todayId;
         sketch.arduino.preferences.begin(PREFERENCES_NAMESPACE, false);
         sketch.arduino.preferences.putInt(TODAY_CHART_DAY_PREF_KEY, todayChartDayId);
         sketch.arduino.preferences.putBytes(TODAY_CHART_BINS_PREF_KEY, todayChartSamples.data(), expectedBytes);
         sketch.arduino.preferences.end();
         return;
      }
      legacyPrefs.end();
   }

   if (todayId >= 0)
   {
      resetTodayChartHistory(todayId);
   }
}

///
/// <summary>
/// Records a Today Summary chart column sample in memory and persists the updated array to
/// Preferences whenever the column's 0.1 kWh rounded values change.
/// </summary>
/// <param name="colIndex">Zero-based column index in todayChart.</param>
/// <param name="prodKwh">Cumulative daily solar production in kWh.</param>
/// <param name="consKwh">Cumulative daily home consumption in kWh.</param>
/// <returns>True if the recorded tenths-of-kWh sample changed; otherwise false.</returns>
///
bool recordTodayChartSample(uint16_t colIndex, float prodKwh, float consKwh)
{
   uint16_t numCols = min(todayChart->getNumBars(), MAX_TODAY_CHART_COLS);
   if (colIndex >= numCols)
   {
      return false;
   }

   uint16_t prodTenths = static_cast<uint16_t>(std::clamp(std::lround(prodKwh * 10.0f), 0L, 65534L));
   uint16_t consTenths = static_cast<uint16_t>(std::clamp(std::lround(consKwh * 10.0f), 0L, 65534L));

   TodayChartSample& existing = todayChartSamples[colIndex];
   if (existing.prodTenthsKwh == prodTenths && existing.consTenthsKwh == consTenths)
   {
      return false;
   }

   existing.prodTenthsKwh = prodTenths;
   existing.consTenthsKwh = consTenths;

   size_t numBytes = static_cast<size_t>(numCols) * sizeof(TodayChartSample);
   sketch.arduino.preferences.begin(PREFERENCES_NAMESPACE, false);
   sketch.arduino.preferences.putInt(TODAY_CHART_DAY_PREF_KEY, todayChartDayId);
   sketch.arduino.preferences.putBytes(TODAY_CHART_BINS_PREF_KEY, todayChartSamples.data(), numBytes);
   sketch.arduino.preferences.end();
   return true;
}

///
/// <summary>
/// Updates the 24-hour midnight-anchored Today Summary dataset for the current local time
/// of day, handles local midnight rollover, and redraws the Today Summary chart when active.
/// </summary>
///
void updateTodayChart()
{
   if (!TimeSync::isSynced())
   {
      return;
   }

   int32_t dayId = currentLocalDayId();
   if (todayChartDayId != dayId)
   {
      todayChart->clear();
      resetTodayChartHistory(dayId);
      if (todayViewMode == TodayViewMode::SUMMARY)
      {
         todayChart->reset();
         todayChart->draw(&sketch.arduino.display);
         drawTodayGridLines();
      }
      else
      {
         drawTodayDynamic();
      }
   }

   time_t now = time(nullptr);
   struct tm timeInfo;
   localtime_r(&now, &timeInfo);
   uint32_t secondsSinceMidnight =
      static_cast<uint32_t>(timeInfo.tm_hour) * 3600UL +
      static_cast<uint32_t>(timeInfo.tm_min) * 60UL +
      static_cast<uint32_t>(timeInfo.tm_sec);

   // Summary 24-hour center-zero chart
   uint16_t numCols = todayChart->getNumBars();
   if (numCols > 0 &&
       !std::isnan(latestReadings.productionTodayKwh) &&
       !std::isnan(latestReadings.consumptionTodayKwh))
   {
      // Guard against brief clock skew at local midnight so yesterday's final daily totals
      // are not recorded into column 0 before Power Hub publishes its midnight reset.
      bool midnightSkew = secondsSinceMidnight < 60 &&
         (latestReadings.productionTodayKwh > 0.5f || latestReadings.consumptionTodayKwh > 1.0f);
      if (!midnightSkew)
      {
         uint16_t colIndex = static_cast<uint16_t>(
            min(static_cast<uint32_t>(numCols - 1), (secondsSinceMidnight * numCols) / SECONDS_PER_DAY));
         todayChart->setAt(
            colIndex,
            netEnergyKwh,
            latestReadings.productionTodayKwh,
            latestReadings.consumptionTodayKwh);
         if (recordTodayChartSample(colIndex, latestReadings.productionTodayKwh, latestReadings.consumptionTodayKwh) &&
             todayViewMode == TodayViewMode::SUMMARY)
         {
            todayChart->draw(&sketch.arduino.display);
            drawTodayGridLines();
         }
      }
   }
}

///
/// <summary>
/// Samples the current live solar production and home consumption and advances the
/// 10-minute rolling power chart whenever a column interval completes (continuing in the
/// background even while the Live Breakdown bar view is active).
/// </summary>
///
void sampleRollingChart()
{
   if (std::isnan(latestReadings.productionKw) || std::isnan(latestReadings.consumptionKw))
   {
      return;
   }

   rollingColumnSum += netPowerKw;
   rollingProdSum += latestReadings.productionKw;
   rollingConsSum += latestReadings.consumptionKw;
   rollingColumnSamples++;

   if (rollingColumnTimer.ready())
   {
      float invSamples = 1.0f / static_cast<float>(rollingColumnSamples);
      rollingChart->set(
         rollingColumnSum * invSamples,
         rollingProdSum * invSamples,
         rollingConsSum * invSamples);
      if (liveViewMode == LiveViewMode::SUMMARY)
      {
         rollingChart->draw(&sketch.arduino.display);
      }
      rollingColumnSum = 0.0f;
      rollingProdSum = 0.0f;
      rollingConsSum = 0.0f;
      rollingColumnSamples = 0;
   }
}

///
/// <summary>
/// Draws the static and initial dynamic content of the "Live" section content area for the
/// current liveViewMode.
/// </summary>
/// <param name="clearArea">True to clear liveSectionRect before drawing.</param>
///
void renderLiveSection(bool clearArea)
{
   if (clearArea)
   {
      sketch.arduino.display.fillRect(
         liveSectionRect.x,
         liveSectionRect.y,
         liveSectionRect.width,
         liveSectionRect.height,
         static_cast<uint16_t>(Color::BLACK));
   }

   if (liveViewMode == LiveViewMode::SUMMARY)
   {
      drawChartLabels(rollingChartRect, "20", "0", "-20");
      rollingChart->reset();
      rollingChart->draw(&sketch.arduino.display);

      nowSummaryRow.invalidate();
   }
   else
   {
      sketch.arduino.setTextSize(TABLE_TEXT_SIZE);
      for (uint8_t i = 0; i < NUM_BREAKDOWN_BARS; i++)
      {
         sketch.arduino.setCursor(contentLeftX, liveBarTextY[i]);
         sketch.arduino.print(BREAKDOWN_BAR_LABELS[i], Color::GRAY);
         liveBars[i]->reset();
         liveBarValues[i].invalidate();
      }
   }

   drawLiveDynamic();
}

///
/// <summary>
/// Draws the static and initial dynamic content of the "Today" section content area for
/// the current todayViewMode.
/// </summary>
/// <param name="clearArea">True to clear todaySectionRect before drawing.</param>
///
void renderTodaySection(bool clearArea)
{
   if (clearArea)
   {
      sketch.arduino.display.fillRect(
         todaySectionRect.x,
         todaySectionRect.y,
         todaySectionRect.width,
         todaySectionRect.height,
         static_cast<uint16_t>(Color::BLACK));
   }

   if (todayViewMode == TodayViewMode::SUMMARY)
   {
      drawChartLabels(todayChartRect, "200", "0", "-200");
      todayChart->reset();
      todayChart->draw(&sketch.arduino.display);
      drawTodayGridLines();

      todaySummaryRow.invalidate();
   }
   else
   {
      int16_t charWidth = sketch.arduino.charW(TABLE_TEXT_SIZE);
      sketch.arduino.setTextSize(TABLE_TEXT_SIZE);

      // Units header aligned above the Y-axis scale and inline with the numeric bar readouts
      sketch.arduino.setCursor(chartLabelRightX - 3 * charWidth, todayBarValueY);
      sketch.arduino.print("kWh", Color::GRAY);

      drawChartLabels(todayBarAreaRect, "100", "50", "0");

      // Faint horizontal reference lines at 100 and 50 kWh (visible in the gaps between bars)
      // and a gray baseline immediately beneath the vertical bars at 0 kWh.
      uint16_t gridColor = static_cast<uint16_t>(TODAY_GRID_COLOR);
      sketch.arduino.display.drawFastHLine(
         todayBarAreaRect.x,
         todayBarAreaRect.y,
         todayBarAreaRect.width,
         gridColor);
      sketch.arduino.display.drawFastHLine(
         todayBarAreaRect.x,
         todayBarAreaRect.y + todayBarAreaRect.height / 2,
         todayBarAreaRect.width,
         gridColor);
      sketch.arduino.display.drawFastHLine(
         todayBarAreaRect.x,
         todayBarAreaRect.y + todayBarAreaRect.height,
         todayBarAreaRect.width,
         static_cast<uint16_t>(Color::GRAY));

      for (uint8_t i = 0; i < NUM_BREAKDOWN_BARS; i++)
      {
         int16_t labelWidth = static_cast<int16_t>(strlen(BREAKDOWN_BAR_LABELS[i])) * charWidth;
         sketch.arduino.setCursor(todayBarCenterX[i] - labelWidth / 2, todayBarLabelY);
         sketch.arduino.print(BREAKDOWN_BAR_LABELS[i], Color::GRAY);

         todayBars[i]->reset();
         todayBarValues[i].invalidate();
      }
   }

   drawTodayDynamic();
}

///
/// <summary>
/// Draws the static screen elements (title, section headings, and active section views)
/// and refreshes all dynamic fields and charts for a full redraw.
/// </summary>
///
void drawStatic()
{
   static bool firstDraw = true;

   telemetryHandler.needsFullRedraw = false;
   sketch.arduino.clearDisplay();

   // Top screen title
   sketch.arduino.setTextSize(HEADING_TEXT_SIZE);
   sketch.arduino.setCursor(0, 0);
   sketch.arduino.println("Power", Color::HEADING);

   // Rotated section headings on the left
   float headingCenterX = headingColWidth / 2.0f;
   sketch.arduino.drawRotatedText(
      LIVE_SECTION_TEXT,
      headingCenterX,
      rollingChartRect.y + rollingChartRect.height / 2.0f,
      ROTATED_HEADING_ANGLE_DEG,
      sectionTextSize);
   sketch.arduino.drawRotatedText(
      TODAY_SECTION_TEXT,
      headingCenterX,
      todayChartRect.y + todayChartRect.height / 2.0f,
      ROTATED_HEADING_ANGLE_DEG,
      sectionTextSize);

   if (firstDraw)
   {
      firstDraw = false;
      rollingChart->clear();
      todayChart->clear();
      populateTodayChartFromSamples();
      rollingColumnSum = 0.0f;
      rollingProdSum = 0.0f;
      rollingConsSum = 0.0f;
      rollingColumnSamples = 0;
      rollingColumnTimer.reset();
      chartSampleTimer.reset();
   }

   renderLiveSection(false);
   renderTodaySection(false);
   updateTodayChart();
}

///
/// <summary>
/// Polls the touch panel for debounced taps in the top ("Live") or bottom ("Today")
/// sections, toggling the tapped section's view mode, persisting the updated selection,
/// and redrawing only that section.
/// </summary>
///
void handleTouch()
{
   Point16 tap;
   if (!sketch.arduino.pollTap(&tap))
   {
      return;
   }

   if (liveTouchRect.contains(tap))
   {
      liveViewMode = (liveViewMode == LiveViewMode::SUMMARY)
         ? LiveViewMode::BREAKDOWN
         : LiveViewMode::SUMMARY;
      saveViewPreferences();
      renderLiveSection(true);
   }
   else if (todayTouchRect.contains(tap))
   {
      todayViewMode = (todayViewMode == TodayViewMode::SUMMARY)
         ? TodayViewMode::BREAKDOWN
         : TodayViewMode::SUMMARY;
      saveViewPreferences();
      renderTodaySection(true);
   }
}

///
/// <summary>
/// Receives incoming telemetry samples for solar, whole-home, and per-building (House,
/// Garage, and Barn) power and daily energy topics, converting live Watts topics to kW
/// and keeping daily kWh topics in kWh.
/// </summary>
/// <param name="topic">Telemetry topic name.</param>
/// <param name="value">Sample value (Watts for live power topics, kWh for *Daily topics).</param>
/// <param name="dtMicros">Elapsed microseconds since the publisher's previous sample on this topic.</param>
///
void onTelemetrySample(const std::string& topic, double value, int64_t dtMicros)
{
   (void)dtMicros;

   if (!std::isfinite(value))
   {
      return;
   }

   if (topic == POWER_SOLAR_TOPIC)
   {
      latestReadings.productionKw = max(0.0f, static_cast<float>(value) / WATTS_PER_KW);
   }
   else if (topic == POWER_SOLAR_DAILY_TOPIC)
   {
      latestReadings.productionTodayKwh = max(0.0f, static_cast<float>(value));
   }
   else if (topic == POWER_TOTAL_TOPIC)
   {
      latestReadings.consumptionKw = max(0.0f, static_cast<float>(value) / WATTS_PER_KW);
   }
   else if (topic == POWER_DAILY_TOPIC)
   {
      latestReadings.consumptionTodayKwh = max(0.0f, static_cast<float>(value));
   }
   else if (topic == POWER_HOUSE_TOPIC)
   {
      latestReadings.houseKw = max(0.0f, static_cast<float>(value) / WATTS_PER_KW);
   }
   else if (topic == POWER_HOUSE_DAILY_TOPIC)
   {
      latestReadings.houseTodayKwh = max(0.0f, static_cast<float>(value));
   }
   else if (topic == POWER_GARAGE_TOPIC)
   {
      latestReadings.garageKw = max(0.0f, static_cast<float>(value) / WATTS_PER_KW);
   }
   else if (topic == POWER_GARAGE_DAILY_TOPIC)
   {
      latestReadings.garageTodayKwh = max(0.0f, static_cast<float>(value));
   }
   else if (topic == POWER_BARN_TOPIC)
   {
      latestReadings.barnKw = max(0.0f, static_cast<float>(value) / WATTS_PER_KW);
   }
   else if (topic == POWER_BARN_DAILY_TOPIC)
   {
      latestReadings.barnTodayKwh = max(0.0f, static_cast<float>(value));
   }
   else
   {
      return;
   }

   if (!std::isnan(latestReadings.productionKw) && !std::isnan(latestReadings.consumptionKw))
   {
      float roundedProdKw = std::round(latestReadings.productionKw * 10.0f) / 10.0f;
      float roundedConsKw = std::round(latestReadings.consumptionKw * 10.0f) / 10.0f;
      netPowerKw = roundedProdKw - roundedConsKw;
   }

   if (!std::isnan(latestReadings.productionTodayKwh) && !std::isnan(latestReadings.consumptionTodayKwh))
   {
      float roundedProdKwh = std::round(latestReadings.productionTodayKwh * 10.0f) / 10.0f;
      float roundedConsKwh = std::round(latestReadings.consumptionTodayKwh * 10.0f) / 10.0f;
      netEnergyKwh = roundedProdKwh - roundedConsKwh;
   }

   readingsDirty = true;
}

///
/// <summary>
/// Formats the latest solar and power telemetry readings as a human-readable string for
/// the DeviceServer "GetReadings" command.
/// </summary>
/// <returns>Formatted summary of live solar production, home consumption, per-building usage, and daily energy.</returns>
///
std::string formatReadings()
{
   if (!latestReadings.hasAnyReading())
   {
      return "No telemetry readings received yet";
   }

   auto formatMetric = [](float value, const char* unit) -> std::string
   {
      if (std::isnan(value))
      {
         return std::string("--- ") + unit;
      }

      std::ostringstream out;
      out.setf(std::ios::fixed);
      out.precision(2);
      out << value << " " << unit;
      return out.str();
   };

   std::ostringstream stream;
   stream << "Solar: " << formatMetric(latestReadings.productionKw, "kW")
          << " (" << formatMetric(latestReadings.productionTodayKwh, "kWh today") << "), "
          << "Usage: " << formatMetric(latestReadings.consumptionKw, "kW")
          << " (" << formatMetric(latestReadings.consumptionTodayKwh, "kWh today") << "), "
          << "Net: " << formatMetric(netPowerKw, "kW")
          << " (" << formatMetric(netEnergyKwh, "kWh today") << "), "
          << "House: " << formatMetric(latestReadings.houseKw, "kW")
          << " (" << formatMetric(latestReadings.houseTodayKwh, "kWh today") << "), "
          << "Garage: " << formatMetric(latestReadings.garageKw, "kW")
          << " (" << formatMetric(latestReadings.garageTodayKwh, "kWh today") << "), "
          << "Barn: " << formatMetric(latestReadings.barnKw, "kW")
          << " (" << formatMetric(latestReadings.barnTodayKwh, "kWh today") << ")";
   return stream.str();
}

void setup()
{
   SerialX::begin();

   sketch.onReadings(formatReadings, "Reports the latest solar production and power consumption telemetry readings");
   sketch.begin();

   initLayout();
   loadViewPreferences();
   loadTodayChartHistory();

   TelemetrySubscriber* client = sketch.beginTelemetry(POWER_SOLAR_TOPIC, &telemetryHandler);
   telemetryHandler.configure(client);

   sketch.completeInitialization();
   drawStatic();
}

void loop()
{
   sketch.loop();

   if (!telemetryHandler.needsFullRedraw)
   {
      handleTouch();
   }

   TelemetrySubscriber* client = sketch.getClient();
   if (!client->isStarted())
   {
      return;
   }

   if (telemetryHandler.needsFullRedraw)
   {
      drawStatic();
   }

   if (readingsDirty && displayTimer.ready())
   {
      readingsDirty = false;
      drawFields();
      updateTodayChart();
   }

   if (chartSampleTimer.ready())
   {
      sampleRollingChart();
      updateTodayChart();
   }
}
