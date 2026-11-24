//
// Power Viewer
//
// Subscribes to live solar production and home power consumption telemetry published by
// the Power Hub (Power/Total, Power/TotalDaily, Power/Solar, Power/SolarDaily, and the
// per-building Power/House*, Power/Garage*, and Power/Barn* topics) over a WebSocket
// connection and renders a dual-section live and daily view.
//
// The screen is divided vertically into two independently tappable sections under the
// "Power" title (plus an upper-right Settings gear icon on touch displays to switch
// display units between kW/kWh and $/hr/$ and configure the $/kWh electricity rate),
// with the selected mode, units, and rate persisted in Preferences:
// - Top (kW or $/hr, tap to cycle through 3 views):
//   1. Now Bars: Five 0-20 kW horizontal bars showing live power for Solar, Total,
//      House, Garage, and Barn, followed by a center-zero (-20 to +20 kW) horizontal Net
//      bar (orange left, green right, white zero line) with right-aligned readouts.
//   2. Now Breakdown: A single center-zero (-20 to +20 kW) horizontal stacked bar
//      with stacked House/Garage/Barn extending left, Solar extending right, and a vertical
//      white Net marker line, followed by a Barn, Garage, House, Solar, and Net table.
//   3. Today History: A 24-hour center-zero chart anchored at local midnight showing
//      per-column average power (Solar green on top, Consumption blue on bottom, 20 kW full
//      scale), followed by live Usage, Solar, and Net totals.
// - Bottom (kWh or $, tap to toggle between 2 views):
//   1. Today Bars: Five 0-200 kWh horizontal bars showing daily energy for Solar,
//      Total, House, Garage, and Barn, followed by a center-zero (-200 to +200 kWh)
//      horizontal Net bar (orange left, green right, white zero line) with right-aligned
//      readouts.
//   2. Today Breakdown: A single center-zero (-200 to +200 kWh) horizontal stacked bar
//      with stacked House/Garage/Barn extending left, Solar extending right, and a vertical
//      white Net marker line, followed by a Barn, Garage, House, Solar, and Net table.
//
// The layout adapts at runtime to the display's dimensions, supporting both large
// displays (e.g. the 480x320 Hosyond ESP32-S3 Viewer board) and compact displays
// (e.g. the 240x135 Feather ESP32-S3 TFT).
//

#include <Arduino.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
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

const auto VERSION = MakeVersion("1.06");
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
constexpr auto LEGACY_TODAY_CHART_BINS_PREF_KEY = "todayBins";
constexpr auto NOW_VIEW_PREF_KEY = "nowView";
constexpr auto TODAY_VIEW_PREF_KEY = "todayView";
constexpr auto UNIT_PREF_KEY = "unit";
constexpr auto RATE_PREF_KEY = "rate";
constexpr uint16_t EMPTY_HUNDREDTHS_KW = 0xFFFF;
constexpr uint16_t MAX_TODAY_CHART_COLS = 480;
constexpr uint8_t NUM_MULTI_BARS = 5;
constexpr uint8_t NUM_BREAKDOWN_COLS = 5;

// Full-scale power (in kW), daily energy (in kWh), and electricity rate ($/kWh).
constexpr float MAX_POWER_KW = 20.0f;
constexpr float MAX_ENERGY_KWH = 200.0f;
constexpr float WATTS_PER_KW = 1000.0f;
constexpr float DEFAULT_RATE_PER_KWH = 0.24f;
constexpr float MAX_RATE_PER_KWH = 99.99f;

// ----------- Display Configuration
constexpr auto NOW_POWER_SECTION_TEXT = "Now (kW)";
constexpr auto TODAY_POWER_SECTION_TEXT = "Today (kW)";
constexpr auto TODAY_ENERGY_SECTION_TEXT = "Today (kWh)";
constexpr auto NOW_COST_SECTION_TEXT = "Now ($/hr)";
constexpr auto TODAY_COST_RATE_SECTION_TEXT = "Today ($/hr)";
constexpr auto TODAY_COST_SECTION_TEXT = "Today ($)";
constexpr uint8_t HEADING_TEXT_SIZE = DEFAULT_HEADING_SIZE;
constexpr uint8_t TABLE_TEXT_SIZE = DEFAULT_FOOTER_SIZE;
constexpr int16_t OUTER_MARGIN_PX = 4;
constexpr int16_t ROW_GAP_PX = 3;
constexpr int16_t SECTION_GAP_LARGE_PX = 20;
constexpr int16_t SECTION_GAP_SMALL_PX = 6;
constexpr int16_t HEADING_GAP_PX = 4;
constexpr int16_t CHART_LABEL_GAP_PX = 4;
constexpr int16_t MULTI_BAR_ROW_GAP_LARGE_PX = 6;
constexpr int16_t MULTI_BAR_ROW_GAP_SMALL_PX = 3;
constexpr int16_t MULTI_BAR_MAX_HEIGHT_LARGE_PX = 22;
constexpr int16_t BREAKDOWN_BAR_MAX_HEIGHT_PX = 60;
constexpr int16_t BREAKDOWN_TABLE_GAP_LARGE_PX = 10;
constexpr int16_t BREAKDOWN_TABLE_GAP_SMALL_PX = 2;
constexpr int16_t BREAKDOWN_HEADER_GAP_PX = 2;
constexpr uint8_t MULTI_BAR_LABEL_CHARS = 6;
constexpr uint8_t CHART_LABEL_CHARS = 3;
constexpr float ROTATED_HEADING_ANGLE_DEG = 270.0f;
constexpr uint32_t SECONDS_PER_DAY = 24UL * 60UL * 60UL;
#ifdef ARDUINO_TOUCH_SUPPORTED
constexpr int16_t GEAR_MARGIN_PX = 4;
constexpr int16_t GEAR_TAP_WIDTH_PX = 56;
constexpr int16_t SETTINGS_MARGIN_X = 16;
constexpr int16_t SETTINGS_GAP_PX = 10;
constexpr int16_t SETTINGS_SECTION_GAP_PX = 16;
constexpr int16_t SETTINGS_CORNER_RADIUS_PX = 8;
constexpr uint8_t SETTINGS_PROMPT_TIMEOUT_M = 5;
constexpr uint8_t NUM_UNIT_OPTIONS = 2;
#endif

constexpr Color TODAY_GRID_COLOR = Color565::fromRGB(48, 48, 48);
constexpr Color BAR_TRACK_COLOR = Color565::fromRGB(32, 32, 32);
constexpr Color DELTA_POSITIVE_COLOR = Color::LIME;
constexpr Color DELTA_NEGATIVE_COLOR = Color::DODGERBLUE;
constexpr Color NET_POSITIVE_COLOR = Color::LIME;
constexpr Color NET_NEGATIVE_COLOR = Color::ORANGE;
constexpr Color NET_MARKER_COLOR = Color::WHITE;
constexpr Color SOLAR_COLOR = Color::LIME;
constexpr Color TOTAL_COLOR = Color::ORANGE;
constexpr Color HOUSE_COLOR = Color::ROYALBLUE;
constexpr Color GARAGE_COLOR = Color::DEEPSKYBLUE;
constexpr Color BARN_COLOR = Color::LIGHTSKYBLUE;

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
/// Display modes for the top (kW) section.
/// </summary>
///
enum class TopViewMode : uint8_t
{
   BARS = 0,
   BREAKDOWN = 1,
   TODAY_HISTORY = 2,
};

constexpr uint8_t NUM_TOP_VIEW_MODES = 3;

///
/// <summary>
/// Display modes for the bottom (kWh) section.
/// </summary>
///
enum class BottomViewMode : uint8_t
{
   BARS = 0,
   BREAKDOWN = 1,
};

///
/// <summary>
/// Display unit modes for power and daily energy readouts.
/// </summary>
///
enum class DisplayUnit : uint8_t
{
   KW = 0,
   COST = 1,
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
TopViewMode topViewMode = TopViewMode::BARS;
BottomViewMode bottomViewMode = BottomViewMode::BARS;
DisplayUnit displayUnit = DisplayUnit::KW;
float ratePerKwh = DEFAULT_RATE_PER_KWH;
bool showingSettings = false;

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

std::array<DisplayValue, NUM_MULTI_BARS> nowBarValues = {
   DisplayValue(&sketch.arduino, Format("##.# kW"), TABLE_TEXT_SIZE, DisplayValue::Alignment::RIGHT),
   DisplayValue(&sketch.arduino, Format("##.# kW"), TABLE_TEXT_SIZE, DisplayValue::Alignment::RIGHT),
   DisplayValue(&sketch.arduino, Format("##.# kW"), TABLE_TEXT_SIZE, DisplayValue::Alignment::RIGHT),
   DisplayValue(&sketch.arduino, Format("##.# kW"), TABLE_TEXT_SIZE, DisplayValue::Alignment::RIGHT),
   DisplayValue(&sketch.arduino, Format("##.# kW"), TABLE_TEXT_SIZE, DisplayValue::Alignment::RIGHT),
};
DisplayValue nowNetBarValue(&sketch.arduino, Format("+##.# kW"), TABLE_TEXT_SIZE, DisplayValue::Alignment::RIGHT);

std::array<DisplayValue, NUM_MULTI_BARS> todayBarValues = {
   DisplayValue(&sketch.arduino, Format("###.# kWh"), TABLE_TEXT_SIZE, DisplayValue::Alignment::RIGHT),
   DisplayValue(&sketch.arduino, Format("###.# kWh"), TABLE_TEXT_SIZE, DisplayValue::Alignment::RIGHT),
   DisplayValue(&sketch.arduino, Format("###.# kWh"), TABLE_TEXT_SIZE, DisplayValue::Alignment::RIGHT),
   DisplayValue(&sketch.arduino, Format("###.# kWh"), TABLE_TEXT_SIZE, DisplayValue::Alignment::RIGHT),
   DisplayValue(&sketch.arduino, Format("###.# kWh"), TABLE_TEXT_SIZE, DisplayValue::Alignment::RIGHT),
};
DisplayValue todayNetBarValue(&sketch.arduino, Format("+###.# kWh"), TABLE_TEXT_SIZE, DisplayValue::Alignment::RIGHT);

constexpr std::array<const char*, NUM_MULTI_BARS> MULTI_BAR_LABELS = { "Solar", "Total", "House", "Garage", "Barn" };
constexpr std::array<Color, NUM_MULTI_BARS> MULTI_BAR_COLORS = { SOLAR_COLOR, TOTAL_COLOR, HOUSE_COLOR, GARAGE_COLOR, BARN_COLOR };

Field nowUsageField(&sketch.arduino, "Usage", "##.# kW", TABLE_TEXT_SIZE);
Field nowSolarField(&sketch.arduino, "Solar", "##.# kW", TABLE_TEXT_SIZE);
Field nowNetField(&sketch.arduino, "Net", "+##.# kW", TABLE_TEXT_SIZE);
FieldRow nowSummaryRow(&nowUsageField, &nowSolarField, &nowNetField, Color::GRAY);

std::array<DisplayValue, NUM_BREAKDOWN_COLS> nowBreakdownValues = {
   DisplayValue(&sketch.arduino, Format("##.# kW"), TABLE_TEXT_SIZE, DisplayValue::Alignment::CENTER),
   DisplayValue(&sketch.arduino, Format("##.# kW"), TABLE_TEXT_SIZE, DisplayValue::Alignment::CENTER),
   DisplayValue(&sketch.arduino, Format("##.# kW"), TABLE_TEXT_SIZE, DisplayValue::Alignment::CENTER),
   DisplayValue(&sketch.arduino, Format("##.# kW"), TABLE_TEXT_SIZE, DisplayValue::Alignment::CENTER),
   DisplayValue(&sketch.arduino, Format("+##.# kW"), TABLE_TEXT_SIZE, DisplayValue::Alignment::CENTER),
};

std::array<DisplayValue, NUM_BREAKDOWN_COLS> todayBreakdownValues = {
   DisplayValue(&sketch.arduino, Format("###.# kWh"), TABLE_TEXT_SIZE, DisplayValue::Alignment::CENTER),
   DisplayValue(&sketch.arduino, Format("###.# kWh"), TABLE_TEXT_SIZE, DisplayValue::Alignment::CENTER),
   DisplayValue(&sketch.arduino, Format("###.# kWh"), TABLE_TEXT_SIZE, DisplayValue::Alignment::CENTER),
   DisplayValue(&sketch.arduino, Format("###.# kWh"), TABLE_TEXT_SIZE, DisplayValue::Alignment::CENTER),
   DisplayValue(&sketch.arduino, Format("+###.# kWh"), TABLE_TEXT_SIZE, DisplayValue::Alignment::CENTER),
};

constexpr std::array<const char*, NUM_BREAKDOWN_COLS> BREAKDOWN_COL_LABELS = { "Barn", "Garage", "House", "Solar", "Net" };
constexpr std::array<Color, NUM_BREAKDOWN_COLS> BREAKDOWN_COL_COLORS = { BARN_COLOR, GARAGE_COLOR, HOUSE_COLOR, SOLAR_COLOR, NET_MARKER_COLOR };

///
/// <summary>
/// Holds the laid-out bar widgets and row Y coordinates for a 6-row horizontal bar
/// section (five component bars for Solar, Total, House, Garage, and Barn plus a
/// center-zero Net bar).
/// </summary>
///
struct MultiBarLayout
{
   std::array<HorizontalBar*, NUM_MULTI_BARS> bars = {};
   std::array<int16_t, NUM_MULTI_BARS> barTextY = {};
   CenterZeroHorizontalBar* netBar = nullptr;
   int16_t netBarTextY = 0;
};

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
/// Stores a single column of the 24-hour Today (kW) chart as average power in hundredths
/// of a kW (or EMPTY_HUNDREDTHS_KW when the column has not yet been recorded today).
/// </summary>
///
struct TodayChartSample
{
   uint16_t prodHundredthsKw = EMPTY_HUNDREDTHS_KW;
   uint16_t consHundredthsKw = EMPTY_HUNDREDTHS_KW;
};

MultiBarLayout nowMultiBar;
MultiBarLayout todayMultiBar;
MovingBarChart* todayChart = nullptr;
BreakdownLayout nowBreakdown;
BreakdownLayout todayBreakdown;
std::array<TodayChartSample, MAX_TODAY_CHART_COLS> todayChartSamples;

Rect16 todayChartRect;
Rect16 nowSectionRect;
Rect16 todaySectionRect;
Rect16 nowTouchRect;
Rect16 todayTouchRect;
int16_t nowSummaryRowY = 0;
int16_t headingColWidth = 0;
int16_t contentLeftX = 0;
uint8_t sectionTextSize = TABLE_TEXT_SIZE;
int16_t chartLabelRightX = 0;
int32_t todayChartDayId = -1;
bool isLargeDisplay = false;
#ifdef ARDUINO_TOUCH_SUPPORTED
Rect16 gearTouchRect;
int16_t gearCenterX = 0;
int16_t gearCenterY = 0;
int16_t gearRadius = 12;
std::array<Rect16, NUM_UNIT_OPTIONS> unitBoxRects = {};
Rect16 rateBoxRect;
Rect16 okBoxRect;
uint8_t settingsButtonTextSize = TABLE_TEXT_SIZE;
#endif
Timer todayPersistTimer(TODAY_CHART_PERSIST_S * 1000UL);
int32_t todayActiveCol = -1;
float todayColProdSum = 0.0f;
float todayColConsSum = 0.0f;
uint32_t todayColSamples = 0;
bool todayChartPrefsDirty = false;

///
/// <summary>
/// Converts a kW or kWh value into the currently selected display unit ($/hr or $ when
/// DisplayUnit::COST is active, or unchanged when DisplayUnit::KW is active).
/// </summary>
/// <param name="value">Input power in kW or energy in kWh (or NAN).</param>
/// <returns>Scaled display value in the active unit (preserving NAN).</returns>
///
float toDisplayValue(float value)
{
   return (displayUnit == DisplayUnit::COST) ? (value * ratePerKwh) : value;
}

///
/// <summary>
/// Computes the signed Net display value from solar production and consumption so that
/// displayed Solar minus displayed Total matches displayed Net without rounding drift.
/// </summary>
/// <param name="prodRaw">Solar production in kW or kWh.</param>
/// <param name="consRaw">Total consumption in kW or kWh.</param>
/// <param name="fallbackNetRaw">Fallback signed net value in kW or kWh if either input is NAN.</param>
/// <returns>Signed net value in the active display unit.</returns>
///
float computeDisplayNet(float prodRaw, float consRaw, float fallbackNetRaw)
{
   if (!std::isnan(prodRaw) && !std::isnan(consRaw))
   {
      if (displayUnit == DisplayUnit::COST)
      {
         float roundedProd = std::round(prodRaw * ratePerKwh * 100.0f) / 100.0f;
         float roundedCons = std::round(consRaw * ratePerKwh * 100.0f) / 100.0f;
         return roundedProd - roundedCons;
      }

      float roundedProd = std::round(prodRaw * 10.0f) / 10.0f;
      float roundedCons = std::round(consRaw * 10.0f) / 10.0f;
      return roundedProd - roundedCons;
   }

   return toDisplayValue(fallbackNetRaw);
}

///
/// <summary>
/// Formats a full-scale chart or breakdown axis maximum concisely (integer when >= 10 or
/// effectively whole, otherwise one decimal place) so labels stay within 3 characters.
/// </summary>
/// <param name="maxValue">Full-scale magnitude in the active display unit.</param>
/// <returns>Formatted axis label string.</returns>
///
std::string formatAxisMaxLabel(float maxValue)
{
   std::ostringstream out;
   out << std::fixed;
   float roundedInt = std::round(maxValue);
   if (roundedInt >= 10.0f || std::fabs(maxValue - roundedInt) < 0.05f)
   {
      out.precision(0);
      out << roundedInt;
   }
   else
   {
      out.precision(1);
      out << maxValue;
   }
   return out.str();
}

///
/// <summary>
/// Formats an electricity rate in $/kWh with at least 2 decimal places while preserving up
/// to 4 decimal places for sub-cent tariffs (e.g. 0.24 -> "0.24", 0.245 -> "0.245").
/// </summary>
/// <param name="rate">Electricity rate in dollars per kWh.</param>
/// <returns>Formatted numeric rate string without currency symbol or unit suffix.</returns>
///
std::string formatRateValue(float rate)
{
   std::ostringstream out;
   out << std::fixed;
   out.precision(4);
   out << rate;
   std::string s = out.str();
   size_t dotPos = s.find('.');
   if (dotPos != std::string::npos)
   {
      while (s.length() > dotPos + 3 && s.back() == '0')
      {
         s.pop_back();
      }
   }
   return s;
}

///
/// <summary>
/// Updates and renders a Breakdown section's center-zero stacked horizontal bar (stacked
/// House/Garage/Barn on the left, Solar on the right, and a vertical white Net marker line)
/// and its 5-column Barn/Garage/House/Solar/Net value table.
/// </summary>
/// <param name="layout">Laid-out breakdown section geometry and bar widget.</param>
/// <param name="values">Five centered DisplayValue widgets for Barn, Garage, House, Solar, and Net.</param>
/// <param name="netValue">Signed net value in kW or kWh (positive when Solar exceeds Usage).</param>
/// <param name="solarValue">Solar production magnitude in kW or kWh.</param>
/// <param name="houseValue">House consumption magnitude in kW or kWh.</param>
/// <param name="garageValue">Garage consumption magnitude in kW or kWh.</param>
/// <param name="barnValue">Barn consumption magnitude in kW or kWh.</param>
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
      toDisplayValue(barnValue),
      toDisplayValue(garageValue),
      toDisplayValue(houseValue),
      toDisplayValue(solarValue),
      toDisplayValue(breakdownNet),
   };
   for (uint8_t i = 0; i < NUM_BREAKDOWN_COLS; i++)
   {
      (*values)[i].draw(colValues[i], BREAKDOWN_COL_COLORS[i]);
   }
}

///
/// <summary>
/// Updates and renders a 6-row horizontal bar section (Solar, Total, House, Garage, and
/// Barn component bars plus a center-zero Net bar) and its right-aligned numeric readouts.
/// </summary>
/// <param name="layout">Laid-out multi-bar widgets and row coordinates.</param>
/// <param name="values">Five right-aligned DisplayValue widgets for Solar, Total, House, Garage, and Barn.</param>
/// <param name="netValueWidget">Right-aligned DisplayValue widget for the signed Net readout.</param>
/// <param name="netValue">Signed net value in kW or kWh (positive when Solar exceeds Usage).</param>
/// <param name="solarValue">Solar production magnitude in kW or kWh.</param>
/// <param name="totalValue">Total consumption magnitude in kW or kWh.</param>
/// <param name="houseValue">House consumption magnitude in kW or kWh.</param>
/// <param name="garageValue">Garage consumption magnitude in kW or kWh.</param>
/// <param name="barnValue">Barn consumption magnitude in kW or kWh.</param>
///
void drawMultiBarDynamic(
   const MultiBarLayout& layout,
   std::array<DisplayValue, NUM_MULTI_BARS>* values,
   DisplayValue* netValueWidget,
   float netValue,
   float solarValue,
   float totalValue,
   float houseValue,
   float garageValue,
   float barnValue)
{
   ASSERT(values != nullptr);
   ASSERT(netValueWidget != nullptr);
   ASSERT(layout.netBar != nullptr);

   const std::array<float, NUM_MULTI_BARS> barValues = {
      solarValue,
      totalValue,
      houseValue,
      garageValue,
      barnValue,
   };
   for (uint8_t i = 0; i < NUM_MULTI_BARS; i++)
   {
      ASSERT(layout.bars[i] != nullptr);

      float v = barValues[i];
      layout.bars[i]->set(std::isnan(v) ? 0.0f : v);
      layout.bars[i]->draw(&sketch.arduino.display);
      (*values)[i].draw(toDisplayValue(v), MULTI_BAR_COLORS[i]);
   }

   layout.netBar->set(std::isnan(netValue) ? 0.0f : netValue);
   layout.netBar->draw(&sketch.arduino.display);
   netValueWidget->draw(computeDisplayNet(solarValue, totalValue, netValue), NET_MARKER_COLOR);
}

///
/// <summary>
/// Updates and renders the dynamic widgets for the active top (kW or $/hr) section mode.
/// </summary>
///
void drawNowDynamic()
{
   if (topViewMode == TopViewMode::BARS)
   {
      drawMultiBarDynamic(
         nowMultiBar,
         &nowBarValues,
         &nowNetBarValue,
         netPowerKw,
         latestReadings.productionKw,
         latestReadings.consumptionKw,
         latestReadings.houseKw,
         latestReadings.garageKw,
         latestReadings.barnKw);
   }
   else if (topViewMode == TopViewMode::BREAKDOWN)
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
   else
   {
      nowSummaryRow.draw(
         toDisplayValue(latestReadings.consumptionKw),
         DELTA_NEGATIVE_COLOR,
         toDisplayValue(latestReadings.productionKw),
         DELTA_POSITIVE_COLOR,
         computeDisplayNet(latestReadings.productionKw, latestReadings.consumptionKw, netPowerKw),
         NET_MARKER_COLOR);
   }
}

///
/// <summary>
/// Updates and renders the dynamic widgets for the active bottom (kWh) section mode.
/// </summary>
///
void drawTodayDynamic()
{
   if (bottomViewMode == BottomViewMode::BARS)
   {
      drawMultiBarDynamic(
         todayMultiBar,
         &todayBarValues,
         &todayNetBarValue,
         netEnergyKwh,
         latestReadings.productionTodayKwh,
         latestReadings.consumptionTodayKwh,
         latestReadings.houseTodayKwh,
         latestReadings.garageTodayKwh,
         latestReadings.barnTodayKwh);
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
/// Computes column center X coordinates and positions the 5 Breakdown DisplayValue widgets
/// across sectionRect for their current format widths.
/// </summary>
/// <param name="layout">Target BreakdownLayout whose colCenterX array is updated.</param>
/// <param name="sectionRect">Bounding rectangle of the section's content area.</param>
/// <param name="values">Five centered DisplayValue widgets for Barn, Garage, House, Solar, and Net.</param>
///
void layoutBreakdownColumns(
   BreakdownLayout* layout,
   const Rect16& sectionRect,
   std::array<DisplayValue, NUM_BREAKDOWN_COLS>* values)
{
   ASSERT(layout != nullptr);
   ASSERT(values != nullptr);

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
/// Computes the layout and constructs the center-zero stacked horizontal bar and 5-column
/// value table for a Breakdown section.
/// </summary>
/// <param name="layout">Target BreakdownLayout structure to populate.</param>
/// <param name="sectionRect">Bounding rectangle of the section's content area.</param>
/// <param name="range">Symmetric value range mapped across each half of the horizontal bar.</param>
/// <param name="values">Five centered DisplayValue widgets for Barn, Garage, House, Solar, and Net.</param>
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

   layoutBreakdownColumns(layout, sectionRect, values);
}

///
/// <summary>
/// Computes the layout and constructs the 6 horizontal bars (Solar, Total, House, Garage,
/// Barn, and center-zero Net) for a MultiBar section.
/// </summary>
/// <param name="layout">Target MultiBarLayout structure to populate.</param>
/// <param name="sectionRect">Bounding rectangle of the section's content area.</param>
/// <param name="maxScale">Full-scale positive magnitude (0..maxScale for components, -maxScale..+maxScale for Net).</param>
/// <param name="values">Five right-aligned DisplayValue widgets for Solar, Total, House, Garage, and Barn.</param>
/// <param name="netValueWidget">Right-aligned DisplayValue widget for the signed Net readout.</param>
/// <param name="barLeftX">Shared left X pixel coordinate for all bars.</param>
/// <param name="barWidth">Shared pixel width for all bars.</param>
/// <param name="displayWidth">Display pixel width used to right-align numeric readouts.</param>
/// <param name="rowHeight">Character pixel height at TABLE_TEXT_SIZE.</param>
///
void initMultiBarLayout(
   MultiBarLayout* layout,
   const Rect16& sectionRect,
   float maxScale,
   std::array<DisplayValue, NUM_MULTI_BARS>* values,
   DisplayValue* netValueWidget,
   int16_t barLeftX,
   int16_t barWidth,
   int16_t displayWidth,
   int16_t rowHeight)
{
   ASSERT(layout != nullptr);
   ASSERT(values != nullptr);
   ASSERT(netValueWidget != nullptr);

   constexpr uint8_t NUM_MULTI_BAR_ROWS = NUM_MULTI_BARS + 1;
   int16_t maxBarGap = isLargeDisplay ? MULTI_BAR_ROW_GAP_LARGE_PX : MULTI_BAR_ROW_GAP_SMALL_PX;
   int16_t barGap = max<int16_t>(1, min<int16_t>(maxBarGap, static_cast<int16_t>((sectionRect.height - NUM_MULTI_BAR_ROWS * rowHeight) / NUM_MULTI_BARS)));
   int16_t totalGapHeight = NUM_MULTI_BARS * barGap;
   int16_t bandHeight = max<int16_t>(rowHeight, (sectionRect.height - totalGapHeight) / NUM_MULTI_BAR_ROWS);
   int16_t barHeight = min<int16_t>(bandHeight, isLargeDisplay ? MULTI_BAR_MAX_HEIGHT_LARGE_PX : rowHeight);
   int16_t totalRowsHeight = NUM_MULTI_BAR_ROWS * bandHeight + totalGapHeight;
   int16_t firstBandY = sectionRect.y + max<int16_t>(0, (sectionRect.height - totalRowsHeight) / 2);

   RangeF componentRange(0.0f, maxScale);
   RangeF netRange(-maxScale, maxScale);

   for (uint8_t i = 0; i < NUM_MULTI_BARS; i++)
   {
      int16_t bandY = firstBandY + i * (bandHeight + barGap);
      int16_t barY = bandY + (bandHeight - barHeight) / 2;
      int16_t textY = bandY + (bandHeight - rowHeight) / 2;

      layout->barTextY[i] = textY;
      (*values)[i].setPosition(displayWidth, textY);
      layout->bars[i] = new HorizontalBar(
         Rect16(barLeftX, barY, barWidth, barHeight),
         componentRange,
         MULTI_BAR_COLORS[i],
         BAR_TRACK_COLOR);
   }

   int16_t netBandY = firstBandY + NUM_MULTI_BARS * (bandHeight + barGap);
   int16_t netBarY = netBandY + (bandHeight - barHeight) / 2;
   layout->netBarTextY = netBandY + (bandHeight - rowHeight) / 2;
   netValueWidget->setPosition(displayWidth, layout->netBarTextY);
   layout->netBar = new CenterZeroHorizontalBar(
      Rect16(barLeftX, netBarY, barWidth, barHeight),
      netRange,
      NET_POSITIVE_COLOR,
      NET_NEGATIVE_COLOR,
      BAR_TRACK_COLOR);
}

///
/// <summary>
/// Computes the responsive display layout for all three top (kW) views and both bottom
/// (kWh) views, along with touch hit-test rectangles.
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
   // sections and their optional single-row tables.
   int16_t fixedHeight = titleHeight + topGap +
      itemGap + rowHeight + sectionGap +
      itemGap + rowHeight;
   int16_t totalChartHeight = max(static_cast<int16_t>(32), static_cast<int16_t>(displayHeight - fixedHeight));
   int16_t nowChartHeight = totalChartHeight / 2;
   int16_t todayChartHeight = totalChartHeight - nowChartHeight;

   int16_t nowChartY = titleHeight + topGap;
   int16_t nowRowY = nowChartY + nowChartHeight + itemGap;
   int16_t nowBottomY = nowRowY + rowHeight;

   int16_t todayChartY = nowBottomY + sectionGap;
   int16_t todayRowY = todayChartY + todayChartHeight + itemGap;
   int16_t todayBottomY = todayRowY + rowHeight;

   int16_t minSectionHeight = min(
      static_cast<int16_t>(nowBottomY - nowChartY),
      static_cast<int16_t>(todayBottomY - todayChartY));
   int16_t maxSectionLabelChars = static_cast<int16_t>(max(
      strlen(TODAY_ENERGY_SECTION_TEXT),
      strlen(TODAY_COST_RATE_SECTION_TEXT)));
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

   todayChartRect = Rect16(chartX, nowChartY, chartWidth, nowChartHeight);
   nowSummaryRowY = nowRowY;

   nowSectionRect = Rect16(contentLeftX, nowChartY, displayWidth - contentLeftX, nowBottomY - nowChartY);
   todaySectionRect = Rect16(contentLeftX, todayChartY, displayWidth - contentLeftX, todayBottomY - todayChartY);

   int16_t splitY = (nowBottomY + todayChartY) / 2;
   nowTouchRect = Rect16(0, nowChartY, displayWidth, splitY - nowChartY);
   todayTouchRect = Rect16(0, splitY, displayWidth, displayHeight - splitY);

#ifdef ARDUINO_TOUCH_SUPPORTED
   gearRadius = min<int16_t>(14, max<int16_t>(10, static_cast<int16_t>((titleHeight - 2 * GEAR_MARGIN_PX) / 2)));
   gearCenterX = displayWidth - GEAR_MARGIN_PX - gearRadius;
   gearCenterY = titleHeight / 2;
   gearTouchRect = Rect16(displayWidth - GEAR_TAP_WIDTH_PX, 0, GEAR_TAP_WIDTH_PX, titleHeight + topGap);
#endif

   nowSummaryRow.setBounds(chartX, nowRowY, chartWidth);

   RangeF powerRange(-MAX_POWER_KW, MAX_POWER_KW);
   RangeF energyRange(-MAX_ENERGY_KWH, MAX_ENERGY_KWH);

   initBreakdownLayout(&nowBreakdown, nowSectionRect, powerRange, &nowBreakdownValues, rowHeight, itemGap);
   initBreakdownLayout(&todayBreakdown, todaySectionRect, energyRange, &todayBreakdownValues, rowHeight, itemGap);

   // Reserve enough right-side value width for both kW/kWh and $/hr/$ formats so bar edges
   // and center-zero lines stay aligned across sections and across unit mode switches.
   size_t maxNetFormatChars = max(Format("+###.# kWh").length(), Format("+$##.##/hr").length());
   int16_t maxNetValueWidth = static_cast<int16_t>(maxNetFormatChars) * charWidth;
   int16_t barLeftX = contentLeftX + MULTI_BAR_LABEL_CHARS * charWidth + CHART_LABEL_GAP_PX;
   int16_t barRightX = displayWidth - maxNetValueWidth - CHART_LABEL_GAP_PX;
   int16_t barWidth = max<int16_t>(16, barRightX - barLeftX);

   initMultiBarLayout(&nowMultiBar, nowSectionRect, MAX_POWER_KW, &nowBarValues, &nowNetBarValue, barLeftX, barWidth, displayWidth, rowHeight);
   initMultiBarLayout(&todayMultiBar, todaySectionRect, MAX_ENERGY_KWH, &todayBarValues, &todayNetBarValue, barLeftX, barWidth, displayWidth, rowHeight);

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
/// Updates every DisplayValue and Field format and re-computes alignment positions to match
/// the currently selected displayUnit (kW/kWh or $/hr/$).
/// </summary>
///
void applyUnitFormats()
{
   const bool useCost = (displayUnit == DisplayUnit::COST);
   const Format nowFormat = useCost ? Format("$##.##/hr") : Format("##.# kW");
   const Format nowNetFormat = useCost ? Format("+$##.##/hr") : Format("+##.# kW");
   const Format todayFormat = useCost ? Format("$###.##") : Format("###.# kWh");
   const Format todayNetFormat = useCost ? Format("+$###.##") : Format("+###.# kWh");

   int16_t displayWidth = sketch.arduino.width();

   for (uint8_t i = 0; i < NUM_MULTI_BARS; i++)
   {
      nowBarValues[i].setFormat(nowFormat);
      nowBarValues[i].setPosition(displayWidth, nowMultiBar.barTextY[i]);
      todayBarValues[i].setFormat(todayFormat);
      todayBarValues[i].setPosition(displayWidth, todayMultiBar.barTextY[i]);
   }
   nowNetBarValue.setFormat(nowNetFormat);
   nowNetBarValue.setPosition(displayWidth, nowMultiBar.netBarTextY);
   todayNetBarValue.setFormat(todayNetFormat);
   todayNetBarValue.setPosition(displayWidth, todayMultiBar.netBarTextY);

   nowUsageField.setFormat(nowFormat);
   nowSolarField.setFormat(nowFormat);
   nowNetField.setFormat(nowNetFormat);
   nowSummaryRow.setBounds(todayChartRect.x, nowSummaryRowY, todayChartRect.width);

   for (uint8_t i = 0; i < NUM_BREAKDOWN_COLS; i++)
   {
      bool isNet = (i == NUM_BREAKDOWN_COLS - 1);
      nowBreakdownValues[i].setFormat(isNet ? nowNetFormat : nowFormat);
      todayBreakdownValues[i].setFormat(isNet ? todayNetFormat : todayFormat);
   }
   layoutBreakdownColumns(&nowBreakdown, nowSectionRect, &nowBreakdownValues);
   layoutBreakdownColumns(&todayBreakdown, todaySectionRect, &todayBreakdownValues);
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

   if (!sketch.arduino.preferences.begin(PREFERENCES_NAMESPACE, false))
   {
      Logger.log("Today chart save failed: preferences.begin() failed", LogSeverity::ERROR);
      return;
   }

   sketch.arduino.preferences.putInt(TODAY_CHART_DAY_PREF_KEY, todayChartDayId);
   size_t written = sketch.arduino.preferences.putBytes(TODAY_CHART_BINS_PREF_KEY, todayChartSamples.data(), numBytes);
   if (written != numBytes)
   {
      // NVS overwrites blobs by writing a new copy before erasing the old one; if free
      // space is tight, remove the old blob first and retry once.
      sketch.arduino.preferences.remove(TODAY_CHART_BINS_PREF_KEY);
      written = sketch.arduino.preferences.putBytes(TODAY_CHART_BINS_PREF_KEY, todayChartSamples.data(), numBytes);
   }
   size_t freeEntries = sketch.arduino.preferences.freeEntries();
   sketch.arduino.preferences.end();

   if (written == numBytes)
   {
      todayChartPrefsDirty = false;
   }
   else
   {
      std::ostringstream msg;
      msg << "Today chart save failed: wrote " << written << "/" << numBytes
          << " bytes (free NVS entries: " << freeEntries << ")";
      Logger.log(msg.str(), LogSeverity::ERROR);
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
/// Loads the persisted Now/Today view modes, display units, and electricity rate from Preferences.
/// </summary>
///
void loadViewPreferences()
{
   if (sketch.arduino.preferences.begin(PREFERENCES_NAMESPACE, true))
   {
      uint8_t savedTop = sketch.arduino.preferences.getUChar(NOW_VIEW_PREF_KEY, 0);
      uint8_t savedBottom = sketch.arduino.preferences.getUChar(TODAY_VIEW_PREF_KEY, 0);
      uint8_t savedUnit = sketch.arduino.preferences.getUChar(UNIT_PREF_KEY, static_cast<uint8_t>(DisplayUnit::KW));
      float savedRate = sketch.arduino.preferences.getFloat(RATE_PREF_KEY, DEFAULT_RATE_PER_KWH);
      sketch.arduino.preferences.end();

      topViewMode = (savedTop < NUM_TOP_VIEW_MODES)
         ? static_cast<TopViewMode>(savedTop)
         : TopViewMode::BARS;
      bottomViewMode = (savedBottom == static_cast<uint8_t>(BottomViewMode::BREAKDOWN))
         ? BottomViewMode::BREAKDOWN
         : BottomViewMode::BARS;
      displayUnit = (savedUnit == static_cast<uint8_t>(DisplayUnit::COST))
         ? DisplayUnit::COST
         : DisplayUnit::KW;
      ratePerKwh = (std::isfinite(savedRate) && savedRate > 0.0f && savedRate <= MAX_RATE_PER_KWH)
         ? savedRate
         : DEFAULT_RATE_PER_KWH;
   }
}

///
/// <summary>
/// Persists the current top/bottom view modes, display units, and electricity rate to Preferences.
/// </summary>
///
void saveViewPreferences()
{
   sketch.arduino.preferences.begin(PREFERENCES_NAMESPACE, false);
   sketch.arduino.preferences.putUChar(NOW_VIEW_PREF_KEY, static_cast<uint8_t>(topViewMode));
   sketch.arduino.preferences.putUChar(TODAY_VIEW_PREF_KEY, static_cast<uint8_t>(bottomViewMode));
   sketch.arduino.preferences.putUChar(UNIT_PREF_KEY, static_cast<uint8_t>(displayUnit));
   sketch.arduino.preferences.putFloat(RATE_PREF_KEY, ratePerKwh);
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
   int32_t savedDayId = -1;
   size_t savedBytes = 0;

   if (sketch.arduino.preferences.begin(PREFERENCES_NAMESPACE, false))
   {
      if (sketch.arduino.preferences.isKey(LEGACY_TODAY_CHART_BINS_PREF_KEY))
      {
         sketch.arduino.preferences.remove(LEGACY_TODAY_CHART_BINS_PREF_KEY);
      }

      savedDayId = sketch.arduino.preferences.getInt(TODAY_CHART_DAY_PREF_KEY, -1);
      savedBytes = sketch.arduino.preferences.getBytesLength(TODAY_CHART_BINS_PREF_KEY);
      if (todayId >= 0 && savedDayId == todayId && savedBytes == expectedBytes)
      {
         sketch.arduino.preferences.getBytes(TODAY_CHART_BINS_PREF_KEY, todayChartSamples.data(), expectedBytes);
         sketch.arduino.preferences.end();
         todayChartDayId = todayId;

         uint16_t populatedCols = 0;
         for (uint16_t i = 0; i < numCols; i++)
         {
            if (todayChartSamples[i].prodHundredthsKw != EMPTY_HUNDREDTHS_KW &&
                todayChartSamples[i].consHundredthsKw != EMPTY_HUNDREDTHS_KW)
            {
               populatedCols++;
            }
         }
         std::ostringstream msg;
         msg << "Loaded Today chart: " << populatedCols << "/" << numCols << " cols (day " << todayId << ")";
         Logger.log(msg.str());
         return;
      }
      sketch.arduino.preferences.end();
   }

   std::ostringstream msg;
   msg << "Today chart not loaded (todayId=" << todayId
       << ", savedDayId=" << savedDayId
       << ", savedBytes=" << savedBytes
       << ", expectedBytes=" << expectedBytes << ")";
   Logger.log(msg.str());

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
   if (todayChartDayId < 0)
   {
      loadTodayChartHistory();
      todayChart->clear();
      populateTodayChartFromSamples();
      todayPersistTimer.reset();
      if (!showingSettings && topViewMode == TopViewMode::TODAY_HISTORY)
      {
         todayChart->reset();
         todayChart->draw(&sketch.arduino.display);
         drawTodayGridLines();
      }
   }
   else if (todayChartDayId != dayId)
   {
      todayChart->clear();
      resetTodayChartHistory(dayId);
      todayPersistTimer.reset();
      if (!showingSettings && topViewMode == TopViewMode::TODAY_HISTORY)
      {
         todayChart->reset();
         todayChart->draw(&sketch.arduino.display);
         drawTodayGridLines();
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
      if (!showingSettings && topViewMode == TopViewMode::TODAY_HISTORY)
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
/// Draws the static axis scale labels (max left, 0 center, max right) and optional table
/// column headers for a Breakdown section, and resets its bar and value widgets.
/// </summary>
/// <param name="layout">Laid-out breakdown section geometry and bar widget.</param>
/// <param name="values">Five centered DisplayValue widgets for Barn, Garage, House, Solar, and Net.</param>
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
/// Draws the right-aligned left-side labels (Solar, Total, House, Garage, Barn, and Net)
/// and resets the bar and value widgets for a 6-row MultiBar section.
/// </summary>
/// <param name="layout">Laid-out multi-bar widgets and row coordinates.</param>
/// <param name="values">Five right-aligned DisplayValue widgets for Solar, Total, House, Garage, and Barn.</param>
/// <param name="netValueWidget">Right-aligned DisplayValue widget for the signed Net readout.</param>
///
void renderMultiBarStatic(
   const MultiBarLayout& layout,
   std::array<DisplayValue, NUM_MULTI_BARS>* values,
   DisplayValue* netValueWidget)
{
   ASSERT(values != nullptr);
   ASSERT(netValueWidget != nullptr);
   ASSERT(layout.netBar != nullptr);

   int16_t charWidth = sketch.arduino.charW(TABLE_TEXT_SIZE);
   int16_t labelRightX = contentLeftX + MULTI_BAR_LABEL_CHARS * charWidth;

   sketch.arduino.setTextSize(TABLE_TEXT_SIZE);
   for (uint8_t i = 0; i < NUM_MULTI_BARS; i++)
   {
      ASSERT(layout.bars[i] != nullptr);

      int16_t labelX = labelRightX - static_cast<int16_t>(strlen(MULTI_BAR_LABELS[i])) * charWidth;
      sketch.arduino.setCursor(labelX, layout.barTextY[i]);
      sketch.arduino.print(MULTI_BAR_LABELS[i], Color::GRAY);
      layout.bars[i]->reset();
      (*values)[i].invalidate();
   }

   int16_t netLabelX = labelRightX - static_cast<int16_t>(strlen("Net")) * charWidth;
   sketch.arduino.setCursor(netLabelX, layout.netBarTextY);
   sketch.arduino.print("Net", Color::GRAY);
   layout.netBar->reset();
   netValueWidget->invalidate();
}

#ifdef ARDUINO_TOUCH_SUPPORTED
///
/// <summary>
/// Draws the settings gear icon in the upper-right corner of the main display.
/// </summary>
///
void drawGear()
{
   const uint16_t color = static_cast<uint16_t>(Color::GRAY);

   constexpr uint8_t NUM_TEETH = 8;
   for (uint8_t i = 0; i < NUM_TEETH; i++)
   {
      const float angle = i * 2.0f * static_cast<float>(M_PI) / NUM_TEETH;
      sketch.arduino.display.fillCircle(
         gearCenterX + static_cast<int16_t>(std::lround(gearRadius * 0.8f * std::cos(angle))),
         gearCenterY + static_cast<int16_t>(std::lround(gearRadius * 0.8f * std::sin(angle))),
         max<int16_t>(2, gearRadius / 5),
         color);
   }

   sketch.arduino.display.fillCircle(gearCenterX, gearCenterY, gearRadius * 7 / 10, color);
   sketch.arduino.display.fillCircle(gearCenterX, gearCenterY, gearRadius * 3 / 10, static_cast<uint16_t>(Color::BLACK));
}

///
/// <summary>
/// Draws a pencil edit icon centered at (cx, cy).
/// </summary>
/// <param name="cx">Center X coordinate.</param>
/// <param name="cy">Center Y coordinate.</param>
/// <param name="color">Icon stroke color.</param>
///
void drawEditIcon(int16_t cx, int16_t cy, Color color)
{
   const uint16_t c = static_cast<uint16_t>(color);
   const int16_t x0 = cx - 9;
   const int16_t y0 = cy + 9;

   // Pencil shaft (diagonal body from bottom-left to top-right)
   sketch.arduino.display.drawLine(x0 + 3, y0 - 7, x0 + 13, y0 - 17, c);
   sketch.arduino.display.drawLine(x0 + 7, y0 - 3, x0 + 17, y0 - 13, c);
   sketch.arduino.display.drawLine(x0 + 13, y0 - 17, x0 + 17, y0 - 13, c);
   sketch.arduino.display.drawLine(x0 + 11, y0 - 15, x0 + 15, y0 - 11, c);
   sketch.arduino.display.drawLine(x0 + 3, y0 - 7, x0 + 7, y0 - 3, c);

   // Pencil tip (pointing to bottom-left)
   sketch.arduino.display.drawLine(x0 + 3, y0 - 7, x0, y0, c);
   sketch.arduino.display.drawLine(x0 + 7, y0 - 3, x0, y0, c);
}

///
/// <summary>
/// Draws the kW and $/hr unit selection buttons on the Settings screen.
/// </summary>
///
void drawSettingsUnitButtons()
{
   constexpr std::array<const char*, NUM_UNIT_OPTIONS> UNIT_LABELS = { "kW", "$/hr" };
   sketch.arduino.setTextSize(settingsButtonTextSize);
   const int16_t textH = sketch.arduino.charH(settingsButtonTextSize);

   for (uint8_t i = 0; i < NUM_UNIT_OPTIONS; i++)
   {
      const Rect16& rect = unitBoxRects[i];
      const bool selected = (static_cast<uint8_t>(displayUnit) == i);
      const Color bg = selected ? Color::DARKGREEN : Color::BLUE;
      const Color border = selected ? Color::WHITE : Color::GRAY;

      sketch.arduino.display.fillRoundRect(rect.x, rect.y, rect.width, rect.height, SETTINGS_CORNER_RADIUS_PX, static_cast<uint16_t>(bg));
      sketch.arduino.display.drawRoundRect(rect.x, rect.y, rect.width, rect.height, SETTINGS_CORNER_RADIUS_PX, static_cast<uint16_t>(border));
      if (selected)
      {
         sketch.arduino.display.drawRoundRect(rect.x + 1, rect.y + 1, rect.width - 2, rect.height - 2, SETTINGS_CORNER_RADIUS_PX, static_cast<uint16_t>(border));
      }

      const char* label = UNIT_LABELS[i];
      const int16_t tw = sketch.arduino.display.textWidth(label);
      sketch.arduino.display.setTextColor(static_cast<uint16_t>(Color::WHITE), static_cast<uint16_t>(bg));
      sketch.arduino.display.setCursor(rect.x + (rect.width - tw) / 2, rect.y + (rect.height - textH) / 2);
      sketch.arduino.display.print(label);
   }
}

///
/// <summary>
/// Draws the editable electricity rate box on the Settings screen.
/// </summary>
///
void drawSettingsRateBox()
{
   sketch.arduino.display.fillRoundRect(
      rateBoxRect.x,
      rateBoxRect.y,
      rateBoxRect.width,
      rateBoxRect.height,
      SETTINGS_CORNER_RADIUS_PX,
      static_cast<uint16_t>(Color::DARKBLUE));
   sketch.arduino.display.drawRoundRect(
      rateBoxRect.x,
      rateBoxRect.y,
      rateBoxRect.width,
      rateBoxRect.height,
      SETTINGS_CORNER_RADIUS_PX,
      static_cast<uint16_t>(Color::WHITE));

   std::string rateText = "$" + formatRateValue(ratePerKwh) + "/kWh";
   sketch.arduino.setTextSize(settingsButtonTextSize);
   const int16_t textH = sketch.arduino.charH(settingsButtonTextSize);
   const int16_t tw = sketch.arduino.display.textWidth(rateText.c_str());
   sketch.arduino.display.setTextColor(static_cast<uint16_t>(Color::WHITE), static_cast<uint16_t>(Color::DARKBLUE));
   sketch.arduino.display.setCursor(
      rateBoxRect.x + (rateBoxRect.width - tw) / 2,
      rateBoxRect.y + (rateBoxRect.height - textH) / 2);
   sketch.arduino.display.print(rateText.c_str());

   drawEditIcon(rateBoxRect.right() - 28, rateBoxRect.y + rateBoxRect.height / 2, Color::LIGHTGRAY);
}

///
/// <summary>
/// Renders the full touch Settings screen for selecting display units (kW or $/hr) and
/// editing the electricity rate ($/kWh).
/// </summary>
///
void drawSettingsScreen()
{
   const int16_t displayWidth = sketch.arduino.width();
   const int16_t displayHeight = sketch.arduino.height();
   const int16_t contentW = displayWidth - 2 * SETTINGS_MARGIN_X;

   sketch.arduino.clearDisplay();

   // Screen heading
   sketch.arduino.setTextSize(HEADING_TEXT_SIZE);
   sketch.arduino.setCursor(SETTINGS_MARGIN_X, 8);
   sketch.arduino.println("Settings", Color::HEADING);

   const int16_t titleH = sketch.arduino.charH(HEADING_TEXT_SIZE);
   const int16_t labelH = sketch.arduino.charH(TABLE_TEXT_SIZE);
   int16_t y = 8 + titleH + SETTINGS_GAP_PX;

   const int16_t bottomMargin = 12;
   const int16_t overhead = 2 * (labelH + 4) + 2 * SETTINGS_SECTION_GAP_PX + bottomMargin;
   const int16_t availH = displayHeight - y - overhead;
   const int16_t boxH = constrain(static_cast<int16_t>(availH / 3), static_cast<int16_t>(labelH + 14), static_cast<int16_t>(56));

   settingsButtonTextSize = (sketch.arduino.charH(DEFAULT_TEXT_SIZE) + 12 <= boxH)
      ? DEFAULT_TEXT_SIZE
      : TABLE_TEXT_SIZE;

   // Display units selector
   sketch.arduino.setTextSize(TABLE_TEXT_SIZE);
   sketch.arduino.setCursor(SETTINGS_MARGIN_X, y);
   sketch.arduino.println("Units", Color::SUB_LABEL);
   y += labelH + 4;

   const int16_t unitBoxW = (contentW - SETTINGS_GAP_PX) / NUM_UNIT_OPTIONS;
   for (uint8_t i = 0; i < NUM_UNIT_OPTIONS; i++)
   {
      int16_t bx = SETTINGS_MARGIN_X + i * (unitBoxW + SETTINGS_GAP_PX);
      int16_t bw = (i + 1 == NUM_UNIT_OPTIONS) ? (SETTINGS_MARGIN_X + contentW - bx) : unitBoxW;
      unitBoxRects[i] = Rect16(bx, y, bw, boxH);
   }
   drawSettingsUnitButtons();
   y += boxH + SETTINGS_SECTION_GAP_PX;

   // Electricity rate editor box
   sketch.arduino.setTextSize(TABLE_TEXT_SIZE);
   sketch.arduino.setCursor(SETTINGS_MARGIN_X, y);
   sketch.arduino.println("Rate", Color::SUB_LABEL);
   y += labelH + 4;

   rateBoxRect = Rect16(SETTINGS_MARGIN_X, y, contentW, boxH);
   drawSettingsRateBox();

   // OK button anchored near the bottom
   const int16_t okY = max<int16_t>(rateBoxRect.bottom() + SETTINGS_SECTION_GAP_PX, displayHeight - bottomMargin - boxH);
   okBoxRect = Rect16(SETTINGS_MARGIN_X, okY, contentW, boxH);

   sketch.arduino.display.fillRoundRect(okBoxRect.x, okBoxRect.y, okBoxRect.width, okBoxRect.height, SETTINGS_CORNER_RADIUS_PX, static_cast<uint16_t>(Color::DARKGREEN));
   sketch.arduino.display.drawRoundRect(okBoxRect.x, okBoxRect.y, okBoxRect.width, okBoxRect.height, SETTINGS_CORNER_RADIUS_PX, static_cast<uint16_t>(Color::WHITE));

   sketch.arduino.setTextSize(settingsButtonTextSize);
   const int16_t okTextH = sketch.arduino.charH(settingsButtonTextSize);
   const int16_t okTw = sketch.arduino.display.textWidth("OK");
   sketch.arduino.display.setTextColor(static_cast<uint16_t>(Color::WHITE), static_cast<uint16_t>(Color::DARKGREEN));
   sketch.arduino.display.setCursor(okBoxRect.x + (okBoxRect.width - okTw) / 2, okBoxRect.y + (okBoxRect.height - okTextH) / 2);
   sketch.arduino.display.print("OK");
}

///
/// <summary>
/// Opens the on-screen touch numeric keypad to edit the electricity rate ($/kWh) and
/// persists any valid positive rate to Preferences immediately.
/// </summary>
///
void editRateTouch()
{
   std::string promptTitle = "Rate ($/kWh)";
   String initialText = formatRateValue(ratePerKwh).c_str();

   while (true)
   {
      bool cancelled = false;
      String entered = sketch.arduino.promptTouchNumber(
         promptTitle.c_str(),
         initialText,
         true,
         SETTINGS_PROMPT_TIMEOUT_M * 60UL * 1000UL,
         true,
         false,
         &cancelled);
      if (cancelled)
      {
         return;
      }

      entered.trim();
      char* endPtr = nullptr;
      float parsedRate = std::strtof(entered.c_str(), &endPtr);
      if (endPtr != entered.c_str() && *endPtr == '\0' &&
          std::isfinite(parsedRate) && parsedRate > 0.0f && parsedRate <= MAX_RATE_PER_KWH)
      {
         ratePerKwh = parsedRate;
         saveViewPreferences();
         return;
      }

      promptTitle = "Invalid - Rate ($/kWh)";
      initialText = entered;
   }
}
#endif

///
/// <summary>
/// Draws the static and initial dynamic content of the top (kW or $/hr) section (including
/// its mode-specific rotated heading) for the current topViewMode.
/// </summary>
/// <param name="clearArea">True to clear the top section and its rotated heading before drawing.</param>
///
void renderNowSection(bool clearArea)
{
   if (clearArea)
   {
      sketch.arduino.display.fillRect(
         0,
         nowSectionRect.y,
         sketch.arduino.width(),
         nowSectionRect.height,
         static_cast<uint16_t>(Color::BLACK));
   }

   const bool useCost = (displayUnit == DisplayUnit::COST);
   const char* topHeading = (topViewMode == TopViewMode::TODAY_HISTORY)
      ? (useCost ? TODAY_COST_RATE_SECTION_TEXT : TODAY_POWER_SECTION_TEXT)
      : (useCost ? NOW_COST_SECTION_TEXT : NOW_POWER_SECTION_TEXT);
   sketch.arduino.drawRotatedText(
      topHeading,
      headingColWidth / 2.0f,
      nowSectionRect.y + nowSectionRect.height / 2.0f,
      ROTATED_HEADING_ANGLE_DEG,
      sectionTextSize);

   std::string maxLabel = formatAxisMaxLabel(useCost ? (MAX_POWER_KW * ratePerKwh) : MAX_POWER_KW);

   if (topViewMode == TopViewMode::BARS)
   {
      renderMultiBarStatic(nowMultiBar, &nowBarValues, &nowNetBarValue);
   }
   else if (topViewMode == TopViewMode::BREAKDOWN)
   {
      renderBreakdownStatic(nowBreakdown, &nowBreakdownValues, maxLabel.c_str());
   }
   else
   {
      drawChartLabels(todayChartRect, maxLabel.c_str(), "0", maxLabel.c_str());
      todayChart->reset();
      todayChart->draw(&sketch.arduino.display);
      drawTodayGridLines();

      nowSummaryRow.invalidate();
   }

   drawNowDynamic();
}

///
/// <summary>
/// Draws the static and initial dynamic content of the bottom (kWh or $) section content
/// area for the current bottomViewMode.
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

   if (bottomViewMode == BottomViewMode::BARS)
   {
      renderMultiBarStatic(todayMultiBar, &todayBarValues, &todayNetBarValue);
   }
   else
   {
      const bool useCost = (displayUnit == DisplayUnit::COST);
      std::string maxLabel = formatAxisMaxLabel(useCost ? (MAX_ENERGY_KWH * ratePerKwh) : MAX_ENERGY_KWH);
      renderBreakdownStatic(todayBreakdown, &todayBreakdownValues, maxLabel.c_str());
   }

   drawTodayDynamic();
}

///
/// <summary>
/// Draws the static screen elements (title, settings gear icon, section headings, and active
/// section views) and refreshes all dynamic fields and charts for a full redraw.
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

#ifdef ARDUINO_TOUCH_SUPPORTED
   drawGear();
#endif

   // Rotated bottom section heading on the left (top heading is drawn by renderNowSection)
   const char* bottomHeading = (displayUnit == DisplayUnit::COST)
      ? TODAY_COST_SECTION_TEXT
      : TODAY_ENERGY_SECTION_TEXT;
   sketch.arduino.drawRotatedText(
      bottomHeading,
      headingColWidth / 2.0f,
      todaySectionRect.y + todaySectionRect.height / 2.0f,
      ROTATED_HEADING_ANGLE_DEG,
      sectionTextSize);

   if (firstDraw)
   {
      firstDraw = false;
      todayChart->clear();
      populateTodayChartFromSamples();
      chartSampleTimer.reset();
      todayPersistTimer.reset();
   }

   renderNowSection(false);
   renderTodaySection(false);
}

///
/// <summary>
/// Polls the touch panel for debounced taps on the upper-right settings gear, the Settings
/// screen controls, or the top/bottom view sections.
/// </summary>
///
void handleTouch()
{
   Point16 tap;
   if (!sketch.arduino.pollTap(&tap))
   {
      return;
   }

#ifdef ARDUINO_TOUCH_SUPPORTED
   if (showingSettings)
   {
      if (unitBoxRects[0].contains(tap) && displayUnit != DisplayUnit::KW)
      {
         displayUnit = DisplayUnit::KW;
         saveViewPreferences();
         drawSettingsUnitButtons();
      }
      else if (unitBoxRects[1].contains(tap) && displayUnit != DisplayUnit::COST)
      {
         displayUnit = DisplayUnit::COST;
         saveViewPreferences();
         drawSettingsUnitButtons();
      }
      else if (rateBoxRect.contains(tap))
      {
         editRateTouch();
         drawSettingsScreen();
      }
      else if (okBoxRect.contains(tap))
      {
         showingSettings = false;
         applyUnitFormats();
         drawStatic();
      }
      return;
   }

   if (gearTouchRect.contains(tap))
   {
      showingSettings = true;
      drawSettingsScreen();
      return;
   }
#endif

   if (nowTouchRect.contains(tap))
   {
      topViewMode = static_cast<TopViewMode>((static_cast<uint8_t>(topViewMode) + 1) % NUM_TOP_VIEW_MODES);
      saveViewPreferences();
      renderNowSection(true);
   }
   else if (todayTouchRect.contains(tap))
   {
      bottomViewMode = (bottomViewMode == BottomViewMode::BARS)
         ? BottomViewMode::BREAKDOWN
         : BottomViewMode::BARS;
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
   applyUnitFormats();
   loadTodayChartHistory();

   TelemetrySubscriber* client = sketch.beginTelemetry(POWER_SOLAR_TOPIC, &telemetryHandler);
   telemetryHandler.configure(client);

   sketch.completeInitialization();
   drawStatic();
}

void loop()
{
   sketch.loop();

   if (!telemetryHandler.needsFullRedraw || showingSettings)
   {
      handleTouch();
   }

   TelemetrySubscriber* client = sketch.getClient();
   if (!client->isStarted())
   {
      return;
   }

   if (!showingSettings)
   {
      if (telemetryHandler.needsFullRedraw)
      {
         drawStatic();
      }

      if (readingsDirty && displayTimer.ready())
      {
         readingsDirty = false;
         drawFields();
      }
   }

   if (chartSampleTimer.ready())
   {
      sampleTodayChart();
   }
}
