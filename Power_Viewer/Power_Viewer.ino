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
//   2. Breakdown: A 24-hour 0-200 kWh X-Y line plot anchored at local midnight showing
//      cumulative daily kWh for House, Garage, and Barn, followed by Today's House,
//      Garage, and Barn kWh totals.
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
#include "DisplayBuffer.h"
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
constexpr auto TODAY_BREAKDOWN_BINS_PREF_KEY = "todaySubBins";
constexpr auto LIVE_VIEW_PREF_KEY = "liveView";
constexpr auto TODAY_VIEW_PREF_KEY = "todayView";
constexpr uint16_t EMPTY_TENTHS_KWH = 0xFFFF;
constexpr uint16_t MAX_TODAY_CHART_COLS = 480;
constexpr uint16_t NUM_TODAY_BREAKDOWN_BINS = 240;
constexpr uint8_t NUM_LIVE_BARS = 4;

// Full-scale power (in kW) and daily energy (in kWh) for the charts and bar gauges.
constexpr float MAX_POWER_KW = 20.0f;
constexpr float MAX_ENERGY_KWH = 200.0f;
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

constexpr uint8_t PLOT_LAYER_AXIS = 1;
constexpr uint8_t PLOT_LAYER_GRID = 2;
constexpr uint8_t PLOT_LAYER_HOUSE = 3;
constexpr uint8_t PLOT_LAYER_GARAGE = 4;
constexpr uint8_t PLOT_LAYER_BARN = 5;
constexpr uint8_t PLOT_MAX_LAYER = PLOT_LAYER_BARN;

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

std::array<DisplayValue, NUM_LIVE_BARS> liveBarValues = {
   DisplayValue(&sketch.arduino, Format("##.# kW"), TABLE_TEXT_SIZE, DisplayValue::Alignment::RIGHT),
   DisplayValue(&sketch.arduino, Format("##.# kW"), TABLE_TEXT_SIZE, DisplayValue::Alignment::RIGHT),
   DisplayValue(&sketch.arduino, Format("##.# kW"), TABLE_TEXT_SIZE, DisplayValue::Alignment::RIGHT),
   DisplayValue(&sketch.arduino, Format("##.# kW"), TABLE_TEXT_SIZE, DisplayValue::Alignment::RIGHT),
};

Field todaySolarField(&sketch.arduino, "Solar", "###.# kWh", TABLE_TEXT_SIZE);
Field todayUsageField(&sketch.arduino, "Usage", "###.# kWh", TABLE_TEXT_SIZE);
Field todayNetField(&sketch.arduino, "Net", "###.# kWh", TABLE_TEXT_SIZE);
FieldRow todaySummaryRow(&todaySolarField, &todayUsageField, &todayNetField, Color::GRAY);

Field todayHouseField(&sketch.arduino, "House", "###.# kWh", TABLE_TEXT_SIZE);
Field todayGarageField(&sketch.arduino, "Garage", "###.# kWh", TABLE_TEXT_SIZE);
Field todayBarnField(&sketch.arduino, "Barn", "###.# kWh", TABLE_TEXT_SIZE);
FieldRow todayBreakdownRow(&todayHouseField, &todayGarageField, &todayBarnField, Color::GRAY);

constexpr std::array<const char*, NUM_LIVE_BARS> LIVE_BAR_LABELS = { "Solar", "House", "Garage", "Barn" };
constexpr std::array<Color, NUM_LIVE_BARS> LIVE_BAR_COLORS = { SOLAR_COLOR, HOUSE_COLOR, GARAGE_COLOR, BARN_COLOR };

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

///
/// <summary>
/// Stores a single time bin of the 24-hour Breakdown Today X-Y plot for House, Garage,
/// and Barn in tenths of a kWh (or EMPTY_TENTHS_KWH when not yet recorded today).
/// </summary>
///
struct TodayBreakdownSample
{
   uint16_t houseTenthsKwh = EMPTY_TENTHS_KWH;
   uint16_t garageTenthsKwh = EMPTY_TENTHS_KWH;
   uint16_t barnTenthsKwh = EMPTY_TENTHS_KWH;
};

MovingBarChart* rollingChart = nullptr;
MovingBarChart* todayChart = nullptr;
std::array<HorizontalBar*, NUM_LIVE_BARS> liveBars = { nullptr, nullptr, nullptr, nullptr };
std::array<int16_t, NUM_LIVE_BARS> liveBarTextY = { 0, 0, 0, 0 };
DisplayBuffer todayBreakdownBuffer;
std::array<TodayChartSample, MAX_TODAY_CHART_COLS> todayChartSamples;
std::array<TodayBreakdownSample, NUM_TODAY_BREAKDOWN_BINS> todayBreakdownSamples;

Rect16 rollingChartRect;
Rect16 todayChartRect;
Rect16 liveSectionRect;
Rect16 todaySectionRect;
Rect16 liveTouchRect;
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
      const std::array<float, NUM_LIVE_BARS> barValues = {
         latestReadings.productionKw,
         latestReadings.houseKw,
         latestReadings.garageKw,
         latestReadings.barnKw,
      };
      for (uint8_t i = 0; i < NUM_LIVE_BARS; i++)
      {
         float kw = barValues[i];
         liveBars[i]->set(std::isnan(kw) ? 0.0f : kw);
         liveBars[i]->draw(&sketch.arduino.display);
         liveBarValues[i].draw(kw, LIVE_BAR_COLORS[i]);
      }
   }
}

///
/// <summary>
/// Updates and renders the dynamic value row for the active "Today" section mode.
/// </summary>
///
void drawTodayFields()
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
      todayBreakdownRow.draw(
         latestReadings.houseTodayKwh,
         HOUSE_COLOR,
         latestReadings.garageTodayKwh,
         GARAGE_COLOR,
         latestReadings.barnTodayKwh,
         BARN_COLOR);
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
   drawTodayFields();
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
   todayBreakdownRow.setBounds(contentLeftX, todayRowY, displayWidth - contentLeftX);

   // Layout for the Live Breakdown 4-row horizontal bar view (Label + 0-20 kW Bar + kW value)
   int16_t barGap = isLargeDisplay ? BAR_ROW_GAP_LARGE_PX : BAR_ROW_GAP_SMALL_PX;
   int16_t bandHeight = max<int16_t>(rowHeight, (liveSectionRect.height - (NUM_LIVE_BARS - 1) * barGap) / NUM_LIVE_BARS);
   int16_t barHeight = min<int16_t>(bandHeight, isLargeDisplay ? BAR_MAX_HEIGHT_LARGE_PX : rowHeight);
   int16_t totalRowsHeight = NUM_LIVE_BARS * bandHeight + (NUM_LIVE_BARS - 1) * barGap;
   int16_t firstBandY = liveSectionRect.y + max<int16_t>(0, (liveSectionRect.height - totalRowsHeight) / 2);

   int16_t barLeftX = contentLeftX + LIVE_BAR_LABEL_CHARS * charWidth + CHART_LABEL_GAP_PX;
   int16_t barRightX = displayWidth - liveBarValues[0].width() - CHART_LABEL_GAP_PX;
   int16_t barWidth = max<int16_t>(16, barRightX - barLeftX);

   RangeF barPowerRange(0.0f, MAX_POWER_KW);

   for (uint8_t i = 0; i < NUM_LIVE_BARS; i++)
   {
      int16_t bandY = firstBandY + i * (bandHeight + barGap);
      int16_t barY = bandY + (bandHeight - barHeight) / 2;
      int16_t textY = bandY + (bandHeight - rowHeight) / 2;

      liveBarTextY[i] = textY;
      liveBarValues[i].setPosition(displayWidth, textY);
      liveBars[i] = new HorizontalBar(
         Rect16(barLeftX, barY, barWidth, barHeight),
         barPowerRange,
         LIVE_BAR_COLORS[i],
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

   todayBreakdownBuffer.bind(
      &sketch.arduino,
      todayChartRect.x,
      todayChartRect.y,
      todayChartRect.width,
      todayChartRect.height,
      PLOT_MAX_LAYER);
   todayBreakdownBuffer.setBackgroundColor(Color::BLACK);
   todayBreakdownBuffer.setPaletteColor(PLOT_LAYER_AXIS, Color::GRAY);
   todayBreakdownBuffer.setPaletteColor(PLOT_LAYER_GRID, TODAY_GRID_COLOR);
   todayBreakdownBuffer.setPaletteColor(PLOT_LAYER_HOUSE, HOUSE_COLOR);
   todayBreakdownBuffer.setPaletteColor(PLOT_LAYER_GARAGE, GARAGE_COLOR);
   todayBreakdownBuffer.setPaletteColor(PLOT_LAYER_BARN, BARN_COLOR);
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
/// Maps a 0-200 kWh daily energy reading to a buffer-relative Y coordinate within
/// todayBreakdownBuffer.
/// </summary>
/// <param name="kwh">Cumulative daily energy in kWh.</param>
/// <returns>Buffer-relative Y pixel row.</returns>
///
int16_t breakdownKwhToPlotY(float kwh)
{
   float clamped = std::clamp(kwh, 0.0f, MAX_ENERGY_KWH);
   int16_t bottomRow = max<int16_t>(0, todayChartRect.height - 2);
   int16_t spanRows = max<int16_t>(1, bottomRow);
   int16_t offset = static_cast<int16_t>(std::lround((clamped / MAX_ENERGY_KWH) * spanRows));
   return bottomRow - offset;
}

///
/// <summary>
/// Stamps a single building's cumulative daily kWh X-Y trace into todayBreakdownBuffer.
/// </summary>
/// <param name="layer">DisplayBuffer palette layer index for the series.</param>
/// <param name="member">Pointer-to-member selecting House, Garage, or Barn tenths of kWh.</param>
///
void stampBreakdownSeries(uint8_t layer, uint16_t TodayBreakdownSample::* member)
{
   int16_t plotRight = max<int16_t>(1, todayChartRect.width - 1);
   int16_t xSpan = max<int16_t>(1, plotRight - 1);

   int16_t prevX = -1;
   int16_t prevY = -1;

   for (uint16_t b = 0; b < NUM_TODAY_BREAKDOWN_BINS; b++)
   {
      uint16_t tenths = todayBreakdownSamples[b].*member;
      if (tenths == EMPTY_TENTHS_KWH)
      {
         continue;
      }

      int16_t x = 1 + static_cast<int16_t>((static_cast<int32_t>(b) * xSpan) / (NUM_TODAY_BREAKDOWN_BINS - 1));
      int16_t y = breakdownKwhToPlotY(tenths * 0.1f);

      if (prevX >= 0)
      {
         todayBreakdownBuffer.drawLine(prevX, prevY, x, y, layer);
         if (isLargeDisplay)
         {
            todayBreakdownBuffer.drawLine(prevX, max<int16_t>(0, prevY - 1), x, max<int16_t>(0, y - 1), layer);
         }
      }

      prevX = x;
      prevY = y;
   }

   if (prevX >= 0)
   {
      todayBreakdownBuffer.drawPoint(prevX, prevY, isLargeDisplay ? 2 : 1, layer);
   }
}

///
/// <summary>
/// Renders the 24-hour 0-200 kWh X-Y plot of House, Garage, and Barn daily energy into
/// todayChartRect using todayBreakdownBuffer.
/// </summary>
/// <param name="mode">DisplayBuffer redraw mode (FULL after clearing the area, DIFF for live updates).</param>
///
void drawTodayBreakdownPlot(DisplayBuffer::RedrawMode mode)
{
   int16_t w = todayChartRect.width;
   int16_t h = todayChartRect.height;
   if (w <= 2 || h <= 2)
   {
      return;
   }

   todayBreakdownBuffer.clear(mode == DisplayBuffer::RedrawMode::FULL);

   // Reference grid: 06:00, 12:00, 18:00 vertical lines + 100 kWh and 200 kWh horizontal lines
   for (uint8_t quarter = 1; quarter <= 3; quarter++)
   {
      int16_t gridX = (w * quarter) / 4;
      todayBreakdownBuffer.drawLine(gridX, 0, gridX, h - 1, PLOT_LAYER_GRID);
   }
   todayBreakdownBuffer.drawLine(1, 0, w - 1, 0, PLOT_LAYER_GRID);
   todayBreakdownBuffer.drawLine(1, (h - 1) / 2, w - 1, (h - 1) / 2, PLOT_LAYER_GRID);

   // Left Y-axis (00:00) and bottom X-axis (0 kWh)
   todayBreakdownBuffer.drawLine(0, 0, 0, h - 1, PLOT_LAYER_AXIS);
   todayBreakdownBuffer.drawLine(0, h - 1, w - 1, h - 1, PLOT_LAYER_AXIS);

   // Series X-Y curves
   stampBreakdownSeries(PLOT_LAYER_HOUSE, &TodayBreakdownSample::houseTenthsKwh);
   stampBreakdownSeries(PLOT_LAYER_GARAGE, &TodayBreakdownSample::garageTenthsKwh);
   stampBreakdownSeries(PLOT_LAYER_BARN, &TodayBreakdownSample::barnTenthsKwh);

   todayBreakdownBuffer.draw(mode);
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
/// Clears all in-memory Today Summary and Breakdown chart samples to their unrecorded state.
/// </summary>
///
void clearTodayChartSamples()
{
   todayChartSamples.fill({ EMPTY_TENTHS_KWH, EMPTY_TENTHS_KWH });
   todayBreakdownSamples.fill({ EMPTY_TENTHS_KWH, EMPTY_TENTHS_KWH, EMPTY_TENTHS_KWH });
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
   sketch.arduino.preferences.remove(TODAY_BREAKDOWN_BINS_PREF_KEY);
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
/// Loads the persisted Today Summary and Breakdown chart samples from Preferences if they
/// match today's local calendar day; otherwise resets the stored samples for the new day.
/// </summary>
///
void loadTodayChartHistory()
{
   clearTodayChartSamples();

   int32_t todayId = currentLocalDayId();
   uint16_t numCols = min(todayChart->getNumBars(), MAX_TODAY_CHART_COLS);
   size_t expectedBytes = static_cast<size_t>(numCols) * sizeof(TodayChartSample);
   size_t expectedBreakdownBytes = todayBreakdownSamples.size() * sizeof(TodayBreakdownSample);

   if (sketch.arduino.preferences.begin(PREFERENCES_NAMESPACE, true))
   {
      int32_t savedDayId = sketch.arduino.preferences.getInt(TODAY_CHART_DAY_PREF_KEY, -1);
      if (todayId >= 0 && savedDayId == todayId)
      {
         bool loadedAny = false;
         if (sketch.arduino.preferences.getBytesLength(TODAY_CHART_BINS_PREF_KEY) == expectedBytes)
         {
            sketch.arduino.preferences.getBytes(TODAY_CHART_BINS_PREF_KEY, todayChartSamples.data(), expectedBytes);
            loadedAny = true;
         }
         if (sketch.arduino.preferences.getBytesLength(TODAY_BREAKDOWN_BINS_PREF_KEY) == expectedBreakdownBytes)
         {
            sketch.arduino.preferences.getBytes(TODAY_BREAKDOWN_BINS_PREF_KEY, todayBreakdownSamples.data(), expectedBreakdownBytes);
            loadedAny = true;
         }
         sketch.arduino.preferences.end();
         if (loadedAny)
         {
            todayChartDayId = todayId;
            return;
         }
      }
      else
      {
         sketch.arduino.preferences.end();
      }
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
/// Converts a floating-point kWh reading to integer tenths of a kWh, or EMPTY_TENTHS_KWH
/// when the reading is NaN.
/// </summary>
/// <param name="kwh">Daily energy in kWh, or NAN if not yet received.</param>
/// <returns>Encoded tenths of a kWh, or EMPTY_TENTHS_KWH.</returns>
///
uint16_t encodeTenthsKwh(float kwh)
{
   if (std::isnan(kwh))
   {
      return EMPTY_TENTHS_KWH;
   }

   return static_cast<uint16_t>(std::clamp(std::lround(kwh * 10.0f), 0L, 65534L));
}

///
/// <summary>
/// Records a Today Breakdown time-bin sample for House, Garage, and Barn in memory and
/// persists the updated array to Preferences whenever any building's 0.1 kWh value changes.
/// </summary>
/// <param name="binIndex">Zero-based time bin index in todayBreakdownSamples.</param>
/// <param name="houseKwh">Cumulative daily House energy in kWh, or NAN.</param>
/// <param name="garageKwh">Cumulative daily Garage energy in kWh, or NAN.</param>
/// <param name="barnKwh">Cumulative daily Barn energy in kWh, or NAN.</param>
/// <returns>True if any recorded tenths-of-kWh value changed; otherwise false.</returns>
///
bool recordTodayBreakdownSample(uint16_t binIndex, float houseKwh, float garageKwh, float barnKwh)
{
   if (binIndex >= NUM_TODAY_BREAKDOWN_BINS)
   {
      return false;
   }

   uint16_t houseTenths = encodeTenthsKwh(houseKwh);
   uint16_t garageTenths = encodeTenthsKwh(garageKwh);
   uint16_t barnTenths = encodeTenthsKwh(barnKwh);

   TodayBreakdownSample& existing = todayBreakdownSamples[binIndex];
   bool changed = false;

   if (houseTenths != EMPTY_TENTHS_KWH && existing.houseTenthsKwh != houseTenths)
   {
      existing.houseTenthsKwh = houseTenths;
      changed = true;
   }
   if (garageTenths != EMPTY_TENTHS_KWH && existing.garageTenthsKwh != garageTenths)
   {
      existing.garageTenthsKwh = garageTenths;
      changed = true;
   }
   if (barnTenths != EMPTY_TENTHS_KWH && existing.barnTenthsKwh != barnTenths)
   {
      existing.barnTenthsKwh = barnTenths;
      changed = true;
   }

   if (!changed)
   {
      return false;
   }

   size_t numBytes = todayBreakdownSamples.size() * sizeof(TodayBreakdownSample);
   sketch.arduino.preferences.begin(PREFERENCES_NAMESPACE, false);
   sketch.arduino.preferences.putInt(TODAY_CHART_DAY_PREF_KEY, todayChartDayId);
   sketch.arduino.preferences.putBytes(TODAY_BREAKDOWN_BINS_PREF_KEY, todayBreakdownSamples.data(), numBytes);
   sketch.arduino.preferences.end();
   return true;
}

///
/// <summary>
/// Updates both the Summary and Breakdown 24-hour midnight-anchored Today datasets for the
/// current local time of day, handles local midnight rollover, and redraws whichever Today
/// chart mode is active when its plotted values change.
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
         drawTodayBreakdownPlot(DisplayBuffer::RedrawMode::DIFF);
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

   // Breakdown 24-hour House / Garage / Barn X-Y plot
   bool hasAnyBreakdownDaily =
      !std::isnan(latestReadings.houseTodayKwh) ||
      !std::isnan(latestReadings.garageTodayKwh) ||
      !std::isnan(latestReadings.barnTodayKwh);
   if (hasAnyBreakdownDaily)
   {
      bool midnightBreakdownSkew = secondsSinceMidnight < 60 &&
         ((!std::isnan(latestReadings.houseTodayKwh) && latestReadings.houseTodayKwh > 1.0f) ||
          (!std::isnan(latestReadings.garageTodayKwh) && latestReadings.garageTodayKwh > 1.0f) ||
          (!std::isnan(latestReadings.barnTodayKwh) && latestReadings.barnTodayKwh > 1.0f));
      if (!midnightBreakdownSkew)
      {
         uint16_t binIndex = static_cast<uint16_t>(
            min(static_cast<uint32_t>(NUM_TODAY_BREAKDOWN_BINS - 1),
                (secondsSinceMidnight * NUM_TODAY_BREAKDOWN_BINS) / SECONDS_PER_DAY));
         if (recordTodayBreakdownSample(
                binIndex,
                latestReadings.houseTodayKwh,
                latestReadings.garageTodayKwh,
                latestReadings.barnTodayKwh) &&
             todayViewMode == TodayViewMode::BREAKDOWN)
         {
            drawTodayBreakdownPlot(DisplayBuffer::RedrawMode::DIFF);
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
      for (uint8_t i = 0; i < NUM_LIVE_BARS; i++)
      {
         sketch.arduino.setCursor(contentLeftX, liveBarTextY[i]);
         sketch.arduino.print(LIVE_BAR_LABELS[i], Color::GRAY);
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
      drawChartLabels(todayChartRect, "200", "100", "0");
      drawTodayBreakdownPlot(DisplayBuffer::RedrawMode::FULL);

      todayBreakdownRow.invalidate();
   }

   drawTodayFields();
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
