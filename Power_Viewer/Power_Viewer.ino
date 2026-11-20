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
// - "Now" (tap top section to toggle):
//   1. Summary: A 10-minute center-zero rolling bar chart of recent power (production,
//      usage, and signed net delta in kW), followed by a single-row table of live Solar,
//      Usage, and Net kW readings.
//   2. Breakdown: A single center-zero (-20 to +20 kW) horizontal stacked bar with stacked
//      House/Garage/Barn extending left, Solar extending right, and a vertical white Net
//      marker line, followed by a Net, Solar, House, Garage, and Barn kW table.
// - "Today" (tap bottom section to toggle):
//   1. Summary: A 24-hour center-zero chart anchored at local midnight showing per-column
//      average power (Solar green on top, Consumption blue on bottom, 20 kW full scale),
//      followed by Today's Solar, Usage, and Net kWh totals.
//   2. Breakdown: A single center-zero (-200 to +200 kWh) horizontal stacked bar with stacked
//      House/Garage/Barn extending left, Solar extending right, and a vertical white Net
//      marker line, followed by a Net, Solar, House, Garage, and Barn kWh table.
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
constexpr uint16_t TODAY_CHART_PERSIST_S = 60;
constexpr auto TODAY_CHART_DAY_PREF_KEY = "todayDay";
constexpr auto TODAY_CHART_BINS_PREF_KEY = "todayKwBins";
constexpr auto NOW_VIEW_PREF_KEY = "nowView";
constexpr auto TODAY_VIEW_PREF_KEY = "todayView";
constexpr uint16_t EMPTY_HUNDREDTHS_KW = 0xFFFF;
constexpr uint16_t MAX_TODAY_CHART_COLS = 480;
constexpr uint8_t NUM_BREAKDOWN_COLS = 5;

// Full-scale power (in kW) and daily energy (in kWh) for the charts and bar gauges.
constexpr float MAX_POWER_KW = 20.0f;
constexpr float MAX_ENERGY_KWH = 200.0f;
constexpr float WATTS_PER_KW = 1000.0f;

// ----------- Display Configuration
constexpr auto NOW_SECTION_TEXT = "Now (kW)";
constexpr auto TODAY_SUMMARY_SECTION_TEXT = "Today (kW)";
constexpr auto TODAY_BREAKDOWN_SECTION_TEXT = "Today (kWh)";
constexpr uint8_t HEADING_TEXT_SIZE = DEFAULT_HEADING_SIZE;
constexpr uint8_t TABLE_TEXT_SIZE = DEFAULT_FOOTER_SIZE;
constexpr int16_t OUTER_MARGIN_PX = 4;
constexpr int16_t ROW_GAP_PX = 3;
constexpr int16_t SECTION_GAP_LARGE_PX = 20;
constexpr int16_t SECTION_GAP_SMALL_PX = 6;
constexpr int16_t HEADING_GAP_PX = 4;
constexpr int16_t CHART_LABEL_GAP_PX = 4;
constexpr int16_t BREAKDOWN_BAR_MAX_HEIGHT_PX = 60;
constexpr int16_t BREAKDOWN_TABLE_GAP_LARGE_PX = 10;
constexpr int16_t BREAKDOWN_TABLE_GAP_SMALL_PX = 2;
constexpr int16_t BREAKDOWN_HEADER_GAP_PX = 2;
constexpr uint8_t CHART_LABEL_CHARS = 3;
constexpr float ROTATED_HEADING_ANGLE_DEG = 270.0f;
constexpr uint16_t ROLLING_CHART_SPAN_M = 10;
constexpr uint32_t SECONDS_PER_DAY = 24UL * 60UL * 60UL;

constexpr Color PRODUCTION_COLOR = Color565::fromRGB(0, 72, 0);
constexpr Color CONSUMPTION_COLOR = Color565::fromRGB(0, 48, 128);
constexpr Color TODAY_GRID_COLOR = Color565::fromRGB(48, 48, 48);
constexpr Color BAR_TRACK_COLOR = Color565::fromRGB(32, 32, 32);
constexpr Color DELTA_POSITIVE_COLOR = Color::LIME;
constexpr Color DELTA_NEGATIVE_COLOR = Color::DODGERBLUE;
constexpr Color NET_MARKER_COLOR = Color::WHITE;
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
/// Display modes for the top ("Now") section.
/// </summary>
///
enum class NowViewMode : uint8_t
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
NowViewMode nowViewMode = NowViewMode::SUMMARY;
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

std::array<DisplayValue, NUM_BREAKDOWN_COLS> nowBreakdownValues = {
   DisplayValue(&sketch.arduino, Format("+##.# kW"), TABLE_TEXT_SIZE, DisplayValue::Alignment::CENTER),
   DisplayValue(&sketch.arduino, Format("##.# kW"), TABLE_TEXT_SIZE, DisplayValue::Alignment::CENTER),
   DisplayValue(&sketch.arduino, Format("##.# kW"), TABLE_TEXT_SIZE, DisplayValue::Alignment::CENTER),
   DisplayValue(&sketch.arduino, Format("##.# kW"), TABLE_TEXT_SIZE, DisplayValue::Alignment::CENTER),
   DisplayValue(&sketch.arduino, Format("##.# kW"), TABLE_TEXT_SIZE, DisplayValue::Alignment::CENTER),
};

Field todaySolarField(&sketch.arduino, "Solar", "###.# kWh", TABLE_TEXT_SIZE);
Field todayUsageField(&sketch.arduino, "Usage", "###.# kWh", TABLE_TEXT_SIZE);
Field todayNetField(&sketch.arduino, "Net", "###.# kWh", TABLE_TEXT_SIZE);
FieldRow todaySummaryRow(&todaySolarField, &todayUsageField, &todayNetField, Color::GRAY);

std::array<DisplayValue, NUM_BREAKDOWN_COLS> todayBreakdownValues = {
   DisplayValue(&sketch.arduino, Format("+###.# kWh"), TABLE_TEXT_SIZE, DisplayValue::Alignment::CENTER),
   DisplayValue(&sketch.arduino, Format("###.# kWh"), TABLE_TEXT_SIZE, DisplayValue::Alignment::CENTER),
   DisplayValue(&sketch.arduino, Format("###.# kWh"), TABLE_TEXT_SIZE, DisplayValue::Alignment::CENTER),
   DisplayValue(&sketch.arduino, Format("###.# kWh"), TABLE_TEXT_SIZE, DisplayValue::Alignment::CENTER),
   DisplayValue(&sketch.arduino, Format("###.# kWh"), TABLE_TEXT_SIZE, DisplayValue::Alignment::CENTER),
};

constexpr std::array<const char*, NUM_BREAKDOWN_COLS> BREAKDOWN_COL_LABELS = { "Net", "Solar", "House", "Garage", "Barn" };
constexpr std::array<Color, NUM_BREAKDOWN_COLS> BREAKDOWN_COL_COLORS = { NET_MARKER_COLOR, SOLAR_COLOR, HOUSE_COLOR, GARAGE_COLOR, BARN_COLOR };

///
/// <summary>
/// Holds the laid-out widget and coordinates for a Breakdown section's center-zero
/// stacked horizontal bar and 5-column value table.
/// </summary>
///
struct BreakdownLayout
{
   StackedCenterZeroHorizontalBar* bar = nullptr;
   Rect16 barRect;
   int16_t axisLabelY = 0;
   int16_t tableHeaderY = 0;
   int16_t tableValueY = 0;
   bool showTableHeaders = false;
   std::array<int16_t, NUM_BREAKDOWN_COLS> colCenterX = {};
};

///
/// <summary>
/// Stores a single column of the 24-hour Summary Today chart as average power in
/// hundredths of a kW (or EMPTY_HUNDREDTHS_KW when the column has not yet been recorded
/// today).
/// </summary>
///
struct TodayChartSample
{
   uint16_t prodHundredthsKw = EMPTY_HUNDREDTHS_KW;
   uint16_t consHundredthsKw = EMPTY_HUNDREDTHS_KW;
};

MovingBarChart* rollingChart = nullptr;
MovingBarChart* todayChart = nullptr;
BreakdownLayout nowBreakdown;
BreakdownLayout todayBreakdown;
std::array<TodayChartSample, MAX_TODAY_CHART_COLS> todayChartSamples;

Rect16 rollingChartRect;
Rect16 todayChartRect;
Rect16 nowSectionRect;
Rect16 todaySectionRect;
Rect16 nowTouchRect;
Rect16 todayTouchRect;
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
Timer todayPersistTimer(TODAY_CHART_PERSIST_S * 1000UL);
int32_t todayActiveCol = -1;
float todayColProdSum = 0.0f;
float todayColConsSum = 0.0f;
uint32_t todayColSamples = 0;
bool todayChartPrefsDirty = false;

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
/// Updates and renders a Breakdown section's center-zero stacked horizontal bar (stacked
/// House/Garage/Barn on the left, Solar on the right, and a vertical white Net marker line)
/// and its 5-column Net/Solar/House/Garage/Barn value table.
/// </summary>
/// <param name="layout">Laid-out breakdown section geometry and bar widget.</param>
/// <param name="values">Five centered DisplayValue widgets for Net, Solar, House, Garage, and Barn.</param>
/// <param name="netValue">Signed net value (positive when Solar exceeds Usage).</param>
/// <param name="solarValue">Solar production magnitude.</param>
/// <param name="houseValue">House consumption magnitude.</param>
/// <param name="garageValue">Garage consumption magnitude.</param>
/// <param name="barnValue">Barn consumption magnitude.</param>
///
void drawBreakdownDynamic(
   const BreakdownLayout& layout,
   std::array<DisplayValue, NUM_BREAKDOWN_COLS>* values,
   float netValue,
   float solarValue,
   float houseValue,
   float garageValue,
   float barnValue)
{
   ASSERT(layout.bar != nullptr);
   ASSERT(values != nullptr);

   // Derive the marker and Net column from the exact segment values drawn on this frame
   // so the white Net marker stays in lockstep with the stacked bars instead of lagging
   // behind asynchronously published Power/Total updates.
   float breakdownNet = (!std::isnan(solarValue) &&
      !std::isnan(houseValue) &&
      !std::isnan(garageValue) &&
      !std::isnan(barnValue))
      ? (solarValue - (houseValue + garageValue + barnValue))
      : netValue;

   layout.bar->setLeftSegments({
      { houseValue, HOUSE_COLOR },
      { garageValue, GARAGE_COLOR },
      { barnValue, BARN_COLOR },
   });
   layout.bar->setRight(solarValue, SOLAR_COLOR);
   // Positive net (Solar > Usage) places the marker right over Solar; negative net places it left over Usage.
   layout.bar->set(breakdownNet);
   layout.bar->draw(&sketch.arduino.display);

   const std::array<float, NUM_BREAKDOWN_COLS> colValues = {
      breakdownNet,
      solarValue,
      houseValue,
      garageValue,
      barnValue,
   };
   for (uint8_t i = 0; i < NUM_BREAKDOWN_COLS; i++)
   {
      (*values)[i].draw(colValues[i], BREAKDOWN_COL_COLORS[i]);
   }
}

///
/// <summary>
/// Updates and renders the dynamic widgets for the active "Now" section mode.
/// </summary>
///
void drawNowDynamic()
{
   if (nowViewMode == NowViewMode::SUMMARY)
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
      drawBreakdownDynamic(
         nowBreakdown,
         &nowBreakdownValues,
         netPowerKw,
         latestReadings.productionKw,
         latestReadings.houseKw,
         latestReadings.garageKw,
         latestReadings.barnKw);
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
      drawBreakdownDynamic(
         todayBreakdown,
         &todayBreakdownValues,
         netEnergyKwh,
         latestReadings.productionTodayKwh,
         latestReadings.houseTodayKwh,
         latestReadings.garageTodayKwh,
         latestReadings.barnTodayKwh);
   }
}

///
/// <summary>
/// Updates and renders the dynamic value widgets for both the Now and Today sections.
/// </summary>
///
void drawFields()
{
   drawNowDynamic();
   drawTodayDynamic();
}

///
/// <summary>
/// Computes the layout and constructs the center-zero stacked horizontal bar and 5-column
/// value table for a Breakdown section.
/// </summary>
/// <param name="layout">Target BreakdownLayout structure to populate.</param>
/// <param name="sectionRect">Bounding rectangle of the section's content area.</param>
/// <param name="range">Symmetric value range mapped across each half of the horizontal bar.</param>
/// <param name="values">Five centered DisplayValue widgets for Net, Solar, House, Garage, and Barn.</param>
/// <param name="rowHeight">Character pixel height at TABLE_TEXT_SIZE.</param>
/// <param name="itemGap">Vertical gap in pixels between the axis labels and the bar.</param>
///
void initBreakdownLayout(
   BreakdownLayout* layout,
   const Rect16& sectionRect,
   RangeF range,
   std::array<DisplayValue, NUM_BREAKDOWN_COLS>* values,
   int16_t rowHeight,
   int16_t itemGap)
{
   ASSERT(layout != nullptr);
   ASSERT(values != nullptr);

   layout->showTableHeaders = isLargeDisplay;
   layout->tableValueY = sectionRect.y + sectionRect.height - rowHeight;
   layout->tableHeaderY = layout->showTableHeaders
      ? (layout->tableValueY - BREAKDOWN_HEADER_GAP_PX - rowHeight)
      : layout->tableValueY;

   int16_t tableTopY = layout->showTableHeaders ? layout->tableHeaderY : layout->tableValueY;
   int16_t tableGap = isLargeDisplay ? BREAKDOWN_TABLE_GAP_LARGE_PX : BREAKDOWN_TABLE_GAP_SMALL_PX;
   int16_t topAreaHeight = max<int16_t>(rowHeight + itemGap + 8, tableTopY - tableGap - sectionRect.y);
   int16_t maxBarHeight = max<int16_t>(8, topAreaHeight - rowHeight - itemGap);
   int16_t barHeight = isLargeDisplay ? min<int16_t>(maxBarHeight, BREAKDOWN_BAR_MAX_HEIGHT_PX) : maxBarHeight;
   int16_t groupHeight = rowHeight + itemGap + barHeight;

   layout->axisLabelY = sectionRect.y + max<int16_t>(0, (topAreaHeight - groupHeight) / 2);
   int16_t barY = layout->axisLabelY + rowHeight + itemGap;
   layout->barRect = Rect16(sectionRect.x, barY, sectionRect.width, barHeight);

   layout->bar = new StackedCenterZeroHorizontalBar(
      layout->barRect,
      range,
      NET_MARKER_COLOR,
      BAR_TRACK_COLOR);
   layout->bar->setCenterLineWidth(StackedCenterZeroHorizontalBar::DEFAULT_MARKER_WIDTH_PX * 2);

   int32_t totalColWidth = 0;
   for (uint8_t i = 0; i < NUM_BREAKDOWN_COLS; i++)
   {
      totalColWidth += (*values)[i].width();
   }

   int32_t totalGap = static_cast<int32_t>(sectionRect.width) - totalColWidth;
   if (totalGap >= 0)
   {
      int32_t cursorX = sectionRect.x;
      constexpr int32_t numGaps = static_cast<int32_t>(NUM_BREAKDOWN_COLS - 1);
      for (uint8_t i = 0; i < NUM_BREAKDOWN_COLS; i++)
      {
         int16_t colWidth = (*values)[i].width();
         layout->colCenterX[i] = static_cast<int16_t>(cursorX + colWidth / 2);
         (*values)[i].setPosition(layout->colCenterX[i], layout->tableValueY);
         if (i + 1 < NUM_BREAKDOWN_COLS)
         {
            int32_t gap = (totalGap * static_cast<int32_t>(i + 1)) / numGaps -
                          (totalGap * static_cast<int32_t>(i)) / numGaps;
            cursorX += colWidth + gap;
         }
      }
   }
   else
   {
      for (uint8_t i = 0; i < NUM_BREAKDOWN_COLS; i++)
      {
         int16_t slotLeft = sectionRect.x + (sectionRect.width * i) / NUM_BREAKDOWN_COLS;
         int16_t slotRight = sectionRect.x + (sectionRect.width * (i + 1)) / NUM_BREAKDOWN_COLS;
         layout->colCenterX[i] = (slotLeft + slotRight) / 2;
         (*values)[i].setPosition(layout->colCenterX[i], layout->tableValueY);
      }
   }
}

///
/// <summary>
/// Computes the responsive display layout for both modes of the "Now" section and both
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

   int16_t topGap = isLargeDisplay ? OUTER_MARGIN_PX : 1;
   int16_t itemGap = isLargeDisplay ? ROW_GAP_PX : 1;
   int16_t sectionGap = isLargeDisplay ? SECTION_GAP_LARGE_PX : SECTION_GAP_SMALL_PX;

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
   int16_t nowBottomY = nowRowY + rowHeight;

   int16_t todayChartY = nowBottomY + sectionGap;
   int16_t todayRowY = todayChartY + todayChartHeight + itemGap;
   int16_t todayBottomY = todayRowY + rowHeight;

   int16_t minSectionHeight = min(
      static_cast<int16_t>(nowBottomY - rollingChartY),
      static_cast<int16_t>(todayBottomY - todayChartY));
   int16_t maxSectionLabelChars = static_cast<int16_t>(strlen(TODAY_BREAKDOWN_SECTION_TEXT));
   sectionTextSize = (isLargeDisplay &&
      maxSectionLabelChars * sketch.arduino.charW(TABLE_TEXT_SIZE + 1) <= minSectionHeight)
      ? static_cast<uint8_t>(TABLE_TEXT_SIZE + 1)
      : TABLE_TEXT_SIZE;
   headingColWidth = sketch.arduino.charH(sectionTextSize);

   // Place content (Y-axis labels + chart, and the one-row value tables) to the right of
   // the rotated section heading column.
   contentLeftX = headingColWidth + HEADING_GAP_PX;
   chartLabelRightX = contentLeftX + charWidth * CHART_LABEL_CHARS;
   int16_t chartX = chartLabelRightX + CHART_LABEL_GAP_PX;
   int16_t chartWidth = max(static_cast<int16_t>(16), static_cast<int16_t>(displayWidth - chartX));

   rollingChartRect = Rect16(chartX, rollingChartY, chartWidth, nowChartHeight);
   todayChartRect = Rect16(chartX, todayChartY, chartWidth, todayChartHeight);

   nowSectionRect = Rect16(contentLeftX, rollingChartY, displayWidth - contentLeftX, nowBottomY - rollingChartY);
   todaySectionRect = Rect16(contentLeftX, todayChartY, displayWidth - contentLeftX, todayBottomY - todayChartY);

   int16_t splitY = (nowBottomY + todayChartY) / 2;
   nowTouchRect = Rect16(0, rollingChartY, displayWidth, splitY - rollingChartY);
   todayTouchRect = Rect16(0, splitY, displayWidth, displayHeight - splitY);

   nowSummaryRow.setBounds(chartX, nowRowY, chartWidth);
   todaySummaryRow.setBounds(chartX, todayRowY, chartWidth);

   RangeF powerRange(-MAX_POWER_KW, MAX_POWER_KW);
   RangeF energyRange(-MAX_ENERGY_KWH, MAX_ENERGY_KWH);

   initBreakdownLayout(&nowBreakdown, nowSectionRect, powerRange, &nowBreakdownValues, rowHeight, itemGap);
   initBreakdownLayout(&todayBreakdown, todaySectionRect, energyRange, &todayBreakdownValues, rowHeight, itemGap);

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
      powerRange,
      SOLAR_COLOR,
      DELTA_NEGATIVE_COLOR,
      Color::BLACK,
      Color::GRAY);
   todayChart->setSecondaryColors(SOLAR_COLOR, DELTA_NEGATIVE_COLOR);
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
   todayChartSamples.fill({ EMPTY_HUNDREDTHS_KW, EMPTY_HUNDREDTHS_KW });
}

///
/// <summary>
/// Resets the running average accumulator for the active Today Summary chart column.
/// </summary>
///
void resetTodayColumnAccumulator()
{
   todayActiveCol = -1;
   todayColProdSum = 0.0f;
   todayColConsSum = 0.0f;
   todayColSamples = 0;
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
      if (sample.prodHundredthsKw != EMPTY_HUNDREDTHS_KW &&
          sample.consHundredthsKw != EMPTY_HUNDREDTHS_KW)
      {
         float prodKw = sample.prodHundredthsKw * 0.01f;
         float consKw = sample.consHundredthsKw * 0.01f;
         todayChart->setAt(i, 0.0f, prodKw, consKw);
      }
   }
}

///
/// <summary>
/// Persists the current day's Today Summary chart average kW samples to Preferences.
/// </summary>
///
void saveTodayChartHistory()
{
   uint16_t numCols = min(todayChart->getNumBars(), MAX_TODAY_CHART_COLS);
   size_t numBytes = static_cast<size_t>(numCols) * sizeof(TodayChartSample);
   sketch.arduino.preferences.begin(PREFERENCES_NAMESPACE, false);
   sketch.arduino.preferences.putInt(TODAY_CHART_DAY_PREF_KEY, todayChartDayId);
   sketch.arduino.preferences.putBytes(TODAY_CHART_BINS_PREF_KEY, todayChartSamples.data(), numBytes);
   sketch.arduino.preferences.end();
   todayChartPrefsDirty = false;
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
   resetTodayColumnAccumulator();
   todayChartPrefsDirty = false;
   todayChartDayId = dayId;

   sketch.arduino.preferences.begin(PREFERENCES_NAMESPACE, false);
   sketch.arduino.preferences.putInt(TODAY_CHART_DAY_PREF_KEY, todayChartDayId);
   sketch.arduino.preferences.remove(TODAY_CHART_BINS_PREF_KEY);
   sketch.arduino.preferences.end();
}

///
/// <summary>
/// Loads the persisted Now and Today view mode selections from Preferences.
/// </summary>
///
void loadViewPreferences()
{
   if (sketch.arduino.preferences.begin(PREFERENCES_NAMESPACE, true))
   {
      uint8_t savedNow = sketch.arduino.preferences.getUChar(NOW_VIEW_PREF_KEY, 0);
      uint8_t savedToday = sketch.arduino.preferences.getUChar(TODAY_VIEW_PREF_KEY, 0);
      sketch.arduino.preferences.end();

      nowViewMode = (savedNow == static_cast<uint8_t>(NowViewMode::BREAKDOWN))
         ? NowViewMode::BREAKDOWN
         : NowViewMode::SUMMARY;
      todayViewMode = (savedToday == static_cast<uint8_t>(TodayViewMode::BREAKDOWN))
         ? TodayViewMode::BREAKDOWN
         : TodayViewMode::SUMMARY;
   }
}

///
/// <summary>
/// Persists the current Now and Today view mode selections to Preferences.
/// </summary>
///
void saveViewPreferences()
{
   sketch.arduino.preferences.begin(PREFERENCES_NAMESPACE, false);
   sketch.arduino.preferences.putUChar(NOW_VIEW_PREF_KEY, static_cast<uint8_t>(nowViewMode));
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
   resetTodayColumnAccumulator();
   todayChartPrefsDirty = false;

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

   if (todayId >= 0)
   {
      resetTodayChartHistory(todayId);
   }
}

///
/// <summary>
/// Updates a Today Summary chart column sample in memory whenever its 0.01 kW rounded
/// average values change, flagging Preferences for persistence.
/// </summary>
/// <param name="colIndex">Zero-based column index in todayChart.</param>
/// <param name="prodKw">Average solar production over the column in kW.</param>
/// <param name="consKw">Average home consumption over the column in kW.</param>
/// <returns>True if the recorded hundredths-of-kW sample changed; otherwise false.</returns>
///
bool updateTodayChartSample(uint16_t colIndex, float prodKw, float consKw)
{
   uint16_t numCols = min(todayChart->getNumBars(), MAX_TODAY_CHART_COLS);
   if (colIndex >= numCols)
   {
      return false;
   }

   uint16_t prodHundredths = static_cast<uint16_t>(std::clamp(std::lround(prodKw * 100.0f), 0L, 65534L));
   uint16_t consHundredths = static_cast<uint16_t>(std::clamp(std::lround(consKw * 100.0f), 0L, 65534L));

   TodayChartSample& existing = todayChartSamples[colIndex];
   if (existing.prodHundredthsKw == prodHundredths && existing.consHundredthsKw == consHundredths)
   {
      return false;
   }

   existing.prodHundredthsKw = prodHundredths;
   existing.consHundredthsKw = consHundredths;
   todayChartPrefsDirty = true;
   return true;
}

///
/// <summary>
/// Samples live solar production and home consumption into the active 24-hour Today Summary
/// column average, handles local midnight rollover, persists history to Preferences on
/// column transitions and periodically, and redraws the Today Summary chart when active.
/// </summary>
///
void sampleTodayChart()
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
      todayPersistTimer.reset();
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

   uint16_t numCols = todayChart->getNumBars();
   if (numCols == 0 ||
       std::isnan(latestReadings.productionKw) ||
       std::isnan(latestReadings.consumptionKw))
   {
      return;
   }

   time_t now = time(nullptr);
   struct tm timeInfo;
   localtime_r(&now, &timeInfo);
   uint32_t secondsSinceMidnight =
      static_cast<uint32_t>(timeInfo.tm_hour) * 3600UL +
      static_cast<uint32_t>(timeInfo.tm_min) * 60UL +
      static_cast<uint32_t>(timeInfo.tm_sec);

   uint16_t colIndex = static_cast<uint16_t>(
      min(static_cast<uint32_t>(numCols - 1), (secondsSinceMidnight * numCols) / SECONDS_PER_DAY));

   bool colAdvanced = (todayActiveCol >= 0 && colIndex != static_cast<uint16_t>(todayActiveCol));
   if (colAdvanced && todayChartPrefsDirty)
   {
      saveTodayChartHistory();
      todayPersistTimer.reset();
   }

   if (todayActiveCol != static_cast<int32_t>(colIndex))
   {
      todayActiveCol = colIndex;
      todayColProdSum = 0.0f;
      todayColConsSum = 0.0f;
      todayColSamples = 0;
   }

   todayColProdSum += latestReadings.productionKw;
   todayColConsSum += latestReadings.consumptionKw;
   todayColSamples++;

   float invSamples = 1.0f / static_cast<float>(todayColSamples);
   float avgProdKw = todayColProdSum * invSamples;
   float avgConsKw = todayColConsSum * invSamples;

   if (updateTodayChartSample(colIndex, avgProdKw, avgConsKw))
   {
      const TodayChartSample& sample = todayChartSamples[colIndex];
      todayChart->setAt(
         colIndex,
         0.0f,
         sample.prodHundredthsKw * 0.01f,
         sample.consHundredthsKw * 0.01f);
      if (todayViewMode == TodayViewMode::SUMMARY)
      {
         todayChart->draw(&sketch.arduino.display);
         drawTodayGridLines();
      }
   }

   if (todayChartPrefsDirty && todayPersistTimer.ready())
   {
      saveTodayChartHistory();
   }
}

///
/// <summary>
/// Samples the current live solar production and home consumption and advances the
/// 10-minute rolling power chart whenever a column interval completes (continuing in the
/// background even while the Now Breakdown bar view is active).
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
      if (nowViewMode == NowViewMode::SUMMARY)
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
/// Draws the static axis scale labels (max left, 0 center, max right) and optional table
/// column headers for a Breakdown section, and resets its bar and value widgets.
/// </summary>
/// <param name="layout">Laid-out breakdown section geometry and bar widget.</param>
/// <param name="values">Five centered DisplayValue widgets for Net, Solar, House, Garage, and Barn.</param>
/// <param name="maxLabel">Full-scale axis label text displayed at both left and right ends.</param>
///
void renderBreakdownStatic(
   const BreakdownLayout& layout,
   std::array<DisplayValue, NUM_BREAKDOWN_COLS>* values,
   const char* maxLabel)
{
   ASSERT(layout.bar != nullptr);
   ASSERT(values != nullptr);
   ASSERT(maxLabel != nullptr);

   int16_t charWidth = sketch.arduino.charW(TABLE_TEXT_SIZE);
   int16_t maxLabelWidth = static_cast<int16_t>(strlen(maxLabel)) * charWidth;
   int16_t barCenterX = layout.barRect.x + layout.barRect.width / 2;

   sketch.arduino.setTextSize(TABLE_TEXT_SIZE);

   // Left max, centered 0, and right max scale labels above the horizontal bar
   sketch.arduino.setCursor(layout.barRect.x, layout.axisLabelY);
   sketch.arduino.print(maxLabel, Color::GRAY);

   sketch.arduino.setCursor(barCenterX - charWidth / 2, layout.axisLabelY);
   sketch.arduino.print("0", Color::GRAY);

   sketch.arduino.setCursor(layout.barRect.x + layout.barRect.width - maxLabelWidth, layout.axisLabelY);
   sketch.arduino.print(maxLabel, Color::GRAY);

   for (uint8_t i = 0; i < NUM_BREAKDOWN_COLS; i++)
   {
      if (layout.showTableHeaders)
      {
         int16_t labelWidth = static_cast<int16_t>(strlen(BREAKDOWN_COL_LABELS[i])) * charWidth;
         sketch.arduino.setCursor(layout.colCenterX[i] - labelWidth / 2, layout.tableHeaderY);
         sketch.arduino.print(BREAKDOWN_COL_LABELS[i], Color::GRAY);
      }

      (*values)[i].invalidate();
   }

   layout.bar->reset();
}

///
/// <summary>
/// Draws the static and initial dynamic content of the "Now" section content area for the
/// current nowViewMode.
/// </summary>
/// <param name="clearArea">True to clear nowSectionRect before drawing.</param>
///
void renderNowSection(bool clearArea)
{
   if (clearArea)
   {
      sketch.arduino.display.fillRect(
         nowSectionRect.x,
         nowSectionRect.y,
         nowSectionRect.width,
         nowSectionRect.height,
         static_cast<uint16_t>(Color::BLACK));
   }

   if (nowViewMode == NowViewMode::SUMMARY)
   {
      drawChartLabels(rollingChartRect, "20", "0", "-20");
      rollingChart->reset();
      rollingChart->draw(&sketch.arduino.display);

      nowSummaryRow.invalidate();
   }
   else
   {
      renderBreakdownStatic(nowBreakdown, &nowBreakdownValues, "20");
   }

   drawNowDynamic();
}

///
/// <summary>
/// Draws the static and initial dynamic content of the "Today" section (including its
/// mode-specific rotated heading) for the current todayViewMode.
/// </summary>
/// <param name="clearArea">True to clear the Today section and its rotated heading before drawing.</param>
///
void renderTodaySection(bool clearArea)
{
   if (clearArea)
   {
      sketch.arduino.display.fillRect(
         0,
         todaySectionRect.y,
         sketch.arduino.width(),
         todaySectionRect.height,
         static_cast<uint16_t>(Color::BLACK));
   }

   const char* todayHeading = (todayViewMode == TodayViewMode::SUMMARY)
      ? TODAY_SUMMARY_SECTION_TEXT
      : TODAY_BREAKDOWN_SECTION_TEXT;
   sketch.arduino.drawRotatedText(
      todayHeading,
      headingColWidth / 2.0f,
      todaySectionRect.y + todaySectionRect.height / 2.0f,
      ROTATED_HEADING_ANGLE_DEG,
      sectionTextSize);

   if (todayViewMode == TodayViewMode::SUMMARY)
   {
      drawChartLabels(todayChartRect, "20", "0", "20");
      todayChart->reset();
      todayChart->draw(&sketch.arduino.display);
      drawTodayGridLines();

      todaySummaryRow.invalidate();
   }
   else
   {
      renderBreakdownStatic(todayBreakdown, &todayBreakdownValues, "200");
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

   // Rotated "Now" section heading on the left ("Today" heading is drawn by renderTodaySection)
   sketch.arduino.drawRotatedText(
      NOW_SECTION_TEXT,
      headingColWidth / 2.0f,
      nowSectionRect.y + nowSectionRect.height / 2.0f,
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
      todayPersistTimer.reset();
   }

   renderNowSection(false);
   renderTodaySection(false);
}

///
/// <summary>
/// Polls the touch panel for debounced taps in the top ("Now") or bottom ("Today")
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

   if (nowTouchRect.contains(tap))
   {
      nowViewMode = (nowViewMode == NowViewMode::SUMMARY)
         ? NowViewMode::BREAKDOWN
         : NowViewMode::SUMMARY;
      saveViewPreferences();
      renderNowSection(true);
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
   }

   if (chartSampleTimer.ready())
   {
      sampleRollingChart();
      sampleTodayChart();
   }
}
