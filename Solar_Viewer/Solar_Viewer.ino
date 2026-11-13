//
// Solar Viewer
//
// Subscribes to live solar production and home power consumption telemetry published by
// the Power Hub (Power/Total, Power/TotalDaily, Solar/Total, and Solar/TotalDaily) over a
// WebSocket connection and renders a live view of solar production, home usage, and net
// flow.
//
// The screen is divided vertically into two sections under the "Solar Monitor" title:
// - "Now": A 1-hour center-zero rolling bar chart of recent power (production, usage, and
//   signed net delta in kW), followed by a single-row table of live Solar, Usage, and Net
//   kW readings.
// - "Today": A 24-hour center-zero chart anchored at local midnight on the left showing
//   cumulative daily energy (production, usage, and signed net delta in kWh) across the
//   day, followed by a single-row table of Today's Solar, Usage, and Net kWh totals.
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
#include "Field.h"
#include "LibraryVersion.h"
#include "MovingBarChart.h"
#include "SerialX.h"
#include "Status.h"
#include "TelemetryClient.h"
#include "TimeSync.h"
#include "Timer.h"
#include "WiFiSettings.h"
#include "ViewerSketch.h"

const auto VERSION = MakeVersion("1.03");
constexpr auto SKETCH_NAME = "Solar_Viewer";
constexpr auto PREFERENCES_NAMESPACE = "SolarViewer";

// ----------- Telemetry Configuration
constexpr auto POWER_TOTAL_TOPIC = "Power/Total";
constexpr auto POWER_DAILY_TOPIC = "Power/TotalDaily";
constexpr auto SOLAR_TOTAL_TOPIC = "Solar/Total";
constexpr auto SOLAR_DAILY_TOPIC = "Solar/TotalDaily";

constexpr uint16_t DISPLAY_UPDATE_MS = 200;
constexpr uint16_t CHART_SAMPLE_MS = 1000;
constexpr auto TODAY_CHART_DAY_PREF_KEY = "todayDay";
constexpr auto TODAY_CHART_BINS_PREF_KEY = "todayBins";
constexpr uint16_t EMPTY_TENTHS_KWH = 0xFFFF;
constexpr uint16_t MAX_TODAY_CHART_COLS = 480;

// Full-scale power (in kW) and daily energy (in kWh) for the vertical bar gauges.
constexpr float MAX_POWER_KW = 20.0f;
constexpr float MAX_ENERGY_KWH = 200.0f;
constexpr float WATTS_PER_KW = 1000.0f;

// ----------- Display Configuration
constexpr auto NOW_SECTION_TEXT = "Now";
constexpr auto TODAY_SECTION_TEXT = "Today";
constexpr uint8_t HEADING_TEXT_SIZE = DEFAULT_HEADING_SIZE;
constexpr uint8_t TABLE_TEXT_SIZE = DEFAULT_FOOTER_SIZE;
constexpr int16_t OUTER_MARGIN_PX = 4;
constexpr int16_t ROW_GAP_PX = 3;
constexpr int16_t SECTION_GAP_LARGE_PX = 20;
constexpr int16_t SECTION_GAP_SMALL_PX = 6;
constexpr int16_t HEADING_GAP_PX = 4;
constexpr int16_t CHART_LABEL_GAP_PX = 4;
constexpr uint8_t TODAY_LABEL_CHARS = 4;
constexpr float ROTATED_HEADING_ANGLE_DEG = 270.0f;
constexpr uint16_t ROLLING_CHART_SPAN_M = 60;
constexpr uint32_t SECONDS_PER_DAY = 24UL * 60UL * 60UL;

constexpr Color PRODUCTION_COLOR = Color565::fromRGB(0, 72, 0);
constexpr Color CONSUMPTION_COLOR = Color565::fromRGB(72, 0, 0);
constexpr Color TODAY_GRID_COLOR = Color565::fromRGB(48, 48, 48);
constexpr Color DELTA_POSITIVE_COLOR = Color::LIME;
constexpr Color DELTA_NEGATIVE_COLOR = Color::RED;

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
/// Holds the latest solar production and home consumption power and daily energy readings
/// received from telemetry.
/// </summary>
///
struct SolarReadings
{
   float productionKw = NAN;
   float productionTodayKwh = NAN;
   float consumptionKw = NAN;
   float consumptionTodayKwh = NAN;

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
         !std::isnan(consumptionTodayKwh);
   }
};

SolarReadings latestReadings;
float netPowerKw = NAN;
float netEnergyKwh = NAN;
bool readingsDirty = false;

void onTelemetrySample(const std::string& topic, double value, int64_t dtMicros);

///
/// <summary>
/// Handles telemetry WebSocket lifecycle events: configures the sample callback and
/// subscribes to all four solar and power topics on the connection, and flags a full
/// screen redraw if an error message overwrote the display.
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
   /// Registers the sample handler and subscribes the client to all four solar and power
   /// telemetry topics once.
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
         SOLAR_TOTAL_TOPIC,
         SOLAR_DAILY_TOPIC,
         POWER_TOTAL_TOPIC,
         POWER_DAILY_TOPIC,
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

Field todaySolarField(&sketch.arduino, "Solar", "###.# kWh", TABLE_TEXT_SIZE);
Field todayUsageField(&sketch.arduino, "Usage", "###.# kWh", TABLE_TEXT_SIZE);
Field todayNetField(&sketch.arduino, "Net", "###.# kWh", TABLE_TEXT_SIZE);

///
/// <summary>
/// Stores a single column of the 24-hour Today chart in tenths of a kWh (or
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
std::array<TodayChartSample, MAX_TODAY_CHART_COLS> todayChartSamples;

Rect16 rollingChartRect;
Rect16 todayChartRect;
int16_t headingColWidth = 0;
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
/// <returns>LIME when non-negative, TOMATO when negative.</returns>
///
Color deltaValueColor(float value)
{
   return value >= 0.0f ? DELTA_POSITIVE_COLOR : Color::TOMATO;
}

///
/// <summary>
/// Draws a single metric Field, showing "---" in gray when the value is NAN (not yet known).
/// </summary>
/// <param name="field">Target display Field.</param>
/// <param name="value">Current metric value, or NAN if not yet received.</param>
/// <param name="activeColor">Value color used when the reading is valid.</param>
///
void drawMetricField(Field* field, float value, Color activeColor)
{
   ASSERT(field != nullptr);

   if (std::isnan(value))
   {
      std::string placeholder = field->format().prefix() + "---" + field->format().postfix();
      field->draw(placeholder, Color::GRAY, Color::GRAY);
   }
   else
   {
      field->draw(value, Color::GRAY, activeColor);
   }
}

///
/// <summary>
/// Renders all six Now and Today Solar/Usage/Net value fields.
/// </summary>
///
void drawFields()
{
   drawMetricField(&nowSolarField, latestReadings.productionKw, DELTA_POSITIVE_COLOR);
   drawMetricField(&nowUsageField, latestReadings.consumptionKw, Color::TOMATO);
   drawMetricField(&nowNetField, std::isnan(netPowerKw) ? NAN : fabsf(netPowerKw), deltaValueColor(netPowerKw));

   drawMetricField(&todaySolarField, latestReadings.productionTodayKwh, DELTA_POSITIVE_COLOR);
   drawMetricField(&todayUsageField, latestReadings.consumptionTodayKwh, Color::TOMATO);
   drawMetricField(&todayNetField, std::isnan(netEnergyKwh) ? NAN : fabsf(netEnergyKwh), deltaValueColor(netEnergyKwh));
}

///
/// <summary>
/// Positions three inline "Label Value" fields across a single horizontal row starting at
/// leftX: the first field left-aligned at leftX, the second field horizontally centered
/// between leftX and displayWidth, and the third field right-aligned against displayWidth.
/// </summary>
/// <param name="leftField">Leftmost field (Solar).</param>
/// <param name="centerField">Center field (Usage).</param>
/// <param name="rightField">Rightmost field (Net).</param>
/// <param name="leftX">Left X coordinate of the row region (to the right of the rotated heading).</param>
/// <param name="y">Top Y coordinate of the row.</param>
/// <param name="displayWidth">Total display width in pixels.</param>
/// <param name="charWidth">Character width in pixels at TABLE_TEXT_SIZE.</param>
///
void positionOneRowFields(
   Field* leftField,
   Field* centerField,
   Field* rightField,
   int16_t leftX,
   int16_t y,
   int16_t displayWidth,
   int16_t charWidth)
{
   ASSERT(leftField != nullptr);
   ASSERT(centerField != nullptr);
   ASSERT(rightField != nullptr);

   int16_t availableWidth = displayWidth - leftX;
   int16_t centerFieldWidth = static_cast<int16_t>((strlen(centerField->label()) + 1) * charWidth) + centerField->valueWidth();
   int16_t rightFieldWidth = static_cast<int16_t>((strlen(rightField->label()) + 1) * charWidth) + rightField->valueWidth();

   leftField->setPosition(leftX, y);
   centerField->setPosition(leftX + (availableWidth - centerFieldWidth) / 2, y);
   rightField->setPosition(displayWidth - rightFieldWidth, y);
}

///
/// <summary>
/// Draws a section heading rotated 90 degrees counter-clockwise (reading bottom-to-top)
/// within the left heading column, vertically centered next to its chart.
/// </summary>
/// <param name="text">Heading text to draw.</param>
/// <param name="chartRect">Bounding rectangle of the section's chart.</param>
///
void drawRotatedSectionHeading(const char* text, const Rect16& chartRect)
{
   int16_t charW = sketch.arduino.charW(sectionTextSize);
   int16_t charH = sketch.arduino.charH(sectionTextSize);
   int16_t spriteW = static_cast<int16_t>(strlen(text)) * charW;
   int16_t spriteH = charH;

   LGFX_Sprite sprite(&sketch.arduino.display);
   sketch.arduino.createSprite(sprite, spriteW, spriteH, sectionTextSize);
   sprite.fillScreen(static_cast<uint16_t>(Color::BLACK));
   sprite.setTextColor(static_cast<uint16_t>(Color::WHITE), static_cast<uint16_t>(Color::BLACK));
   sprite.setCursor(0, 0);
   sprite.print(text);
   sprite.setPivot(spriteW / 2.0f, spriteH / 2.0f);

   float dstX = headingColWidth / 2.0f;
   float dstY = chartRect.y + chartRect.height / 2.0f;
   sprite.pushRotateZoom(dstX, dstY, ROTATED_HEADING_ANGLE_DEG, 1.0f, 1.0f);
   sprite.deleteSprite();
}

///
/// <summary>
/// Computes the responsive display layout for the "Now" section (rotated left heading,
/// 1-hour rolling chart, and 1-row Solar/Usage/Net table) and the "Today" section
/// (rotated left heading, 24-hour midnight-anchored chart, and 1-row Solar/Usage/Net table).
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
   int16_t contentLeftX = headingColWidth + HEADING_GAP_PX;
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

   int16_t todayChartY = nowRowY + rowHeight + sectionGap;
   int16_t todayRowY = todayChartY + todayChartHeight + itemGap;

   rollingChartRect = Rect16(chartX, rollingChartY, chartWidth, nowChartHeight);
   todayChartRect = Rect16(chartX, todayChartY, chartWidth, todayChartHeight);

   positionOneRowFields(&nowSolarField, &nowUsageField, &nowNetField, chartX, nowRowY, displayWidth, charWidth);
   positionOneRowFields(&todaySolarField, &todayUsageField, &todayNetField, chartX, todayRowY, displayWidth, charWidth);

   RangeF powerRange(-MAX_POWER_KW, MAX_POWER_KW);
   RangeF energyRange(-MAX_ENERGY_KWH, MAX_ENERGY_KWH);

   uint32_t columnPeriodMs = (ROLLING_CHART_SPAN_M * 60UL * 1000UL) / static_cast<uint32_t>(chartWidth);
   rollingColumnTimer.setDurationMs(max(1000UL, columnPeriodMs));
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
/// Draws the right-aligned Y-axis labels (top, "0", and bottom) in gray to the left of a
/// center-zero chart.
/// </summary>
/// <param name="chartRect">Bounding rectangle of the chart.</param>
/// <param name="topLabel">Top positive full-scale label text.</param>
/// <param name="bottomLabel">Bottom negative full-scale label text.</param>
///
void drawChartLabels(
   const Rect16& chartRect,
   const char* topLabel,
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

   sketch.arduino.setCursor(chartLabelRightX - charWidth, centerY);
   sketch.arduino.print("0", Color::GRAY);

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
/// Clears all in-memory Today chart column samples to their unrecorded state.
/// </summary>
///
void clearTodayChartSamples()
{
   todayChartSamples.fill({ EMPTY_TENTHS_KWH, EMPTY_TENTHS_KWH });
}

///
/// <summary>
/// Applies any persisted Today chart column samples for the current day into todayChart.
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
/// Loads the persisted Today chart column samples from Preferences if they match today's
/// local calendar day; otherwise resets the stored samples for the new day.
/// </summary>
///
void loadTodayChartHistory()
{
   clearTodayChartSamples();

   int32_t todayId = currentLocalDayId();
   uint16_t numCols = min(todayChart->getNumBars(), MAX_TODAY_CHART_COLS);
   size_t expectedBytes = static_cast<size_t>(numCols) * sizeof(TodayChartSample);

   sketch.arduino.preferences.begin(PREFERENCES_NAMESPACE, true);
   int32_t savedDayId = sketch.arduino.preferences.getInt(TODAY_CHART_DAY_PREF_KEY, -1);
   size_t savedBytes = sketch.arduino.preferences.getBytesLength(TODAY_CHART_BINS_PREF_KEY);
   if (todayId >= 0 && savedDayId == todayId && savedBytes == expectedBytes)
   {
      sketch.arduino.preferences.getBytes(TODAY_CHART_BINS_PREF_KEY, todayChartSamples.data(), expectedBytes);
      sketch.arduino.preferences.end();
      todayChartDayId = todayId;
      return;
   }
   sketch.arduino.preferences.end();

   if (todayId >= 0)
   {
      resetTodayChartHistory(todayId);
   }
}

///
/// <summary>
/// Records a Today chart column sample in memory and persists the updated array to
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
/// Updates the 24-hour midnight-anchored Today chart for the current local time of day
/// and handles local midnight rollover.
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
      todayChart->draw(&sketch.arduino.display);
      drawTodayGridLines();
   }

   if (std::isnan(latestReadings.productionTodayKwh) || std::isnan(latestReadings.consumptionTodayKwh))
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

   // Guard against brief clock skew at local midnight so yesterday's final daily totals
   // are not recorded into column 0 before Power Hub publishes its midnight reset.
   if (secondsSinceMidnight < 60 &&
       (latestReadings.productionTodayKwh > 0.5f || latestReadings.consumptionTodayKwh > 1.0f))
   {
      return;
   }

   uint16_t numCols = todayChart->getNumBars();
   if (numCols == 0)
   {
      return;
   }

   uint16_t colIndex = static_cast<uint16_t>(
      min(static_cast<uint32_t>(numCols - 1), (secondsSinceMidnight * numCols) / SECONDS_PER_DAY));
   todayChart->setAt(
      colIndex,
      netEnergyKwh,
      latestReadings.productionTodayKwh,
      latestReadings.consumptionTodayKwh);
   if (recordTodayChartSample(colIndex, latestReadings.productionTodayKwh, latestReadings.consumptionTodayKwh))
   {
      todayChart->draw(&sketch.arduino.display);
      drawTodayGridLines();
   }
}

///
/// <summary>
/// Samples the current live solar production and home consumption once per second and
/// advances the 1-hour rolling power chart whenever a column interval completes.
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
      rollingChart->draw(&sketch.arduino.display);
      rollingColumnSum = 0.0f;
      rollingProdSum = 0.0f;
      rollingConsSum = 0.0f;
      rollingColumnSamples = 0;
   }
}

///
/// <summary>
/// Draws the static screen elements (title, section headings, and chart labels) and
/// resets the dynamic fields and charts for a full redraw.
/// </summary>
///
void drawStatic()
{
   telemetryHandler.needsFullRedraw = false;
   sketch.arduino.clearDisplay();

   // Top screen title
   sketch.arduino.setTextSize(HEADING_TEXT_SIZE);
   sketch.arduino.setCursor(0, 0);
   sketch.arduino.println("Solar Monitor", Color::HEADING);

   // Rotated section headings on the left
   drawRotatedSectionHeading(NOW_SECTION_TEXT, rollingChartRect);
   drawRotatedSectionHeading(TODAY_SECTION_TEXT, todayChartRect);

   // Chart Y-axis labels
   drawChartLabels(rollingChartRect, "20", "-20");
   drawChartLabels(todayChartRect, "200", "-200");

   rollingChart->clear();
   todayChart->clear();
   populateTodayChartFromSamples();
   rollingColumnSum = 0.0f;
   rollingProdSum = 0.0f;
   rollingConsSum = 0.0f;
   rollingColumnSamples = 0;
   rollingColumnTimer.reset();
   chartSampleTimer.reset();

   rollingChart->draw(&sketch.arduino.display);
   todayChart->draw(&sketch.arduino.display);
   drawTodayGridLines();

   nowSolarField.invalidate();
   nowUsageField.invalidate();
   nowNetField.invalidate();
   todaySolarField.invalidate();
   todayUsageField.invalidate();
   todayNetField.invalidate();

   drawFields();
   updateTodayChart();
}

///
/// <summary>
/// Receives incoming telemetry samples for Solar/Total, Solar/TotalDaily, Power/Total,
/// and Power/TotalDaily, converting live Watts topics to kW and keeping daily kWh topics
/// in kWh.
/// </summary>
/// <param name="topic">Telemetry topic name.</param>
/// <param name="value">Sample value (Watts for */Total, kWh for */TotalDaily).</param>
/// <param name="dtMicros">Elapsed microseconds since the publisher's previous sample on this topic.</param>
///
void onTelemetrySample(const std::string& topic, double value, int64_t dtMicros)
{
   (void)dtMicros;

   if (!std::isfinite(value))
   {
      return;
   }

   if (topic == SOLAR_TOTAL_TOPIC)
   {
      latestReadings.productionKw = max(0.0f, static_cast<float>(value) / WATTS_PER_KW);
   }
   else if (topic == SOLAR_DAILY_TOPIC)
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
/// <returns>Formatted summary of live solar production, home consumption, and daily energy.</returns>
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
          << " (" << formatMetric(netEnergyKwh, "kWh today") << ")";
   return stream.str();
}

void setup()
{
   SerialX::begin();

   sketch.onReadings(formatReadings, "Reports the latest solar production and power consumption telemetry readings");
   sketch.begin();

   initLayout();
   loadTodayChartHistory();

   TelemetrySubscriber* client = sketch.beginTelemetry(SOLAR_TOTAL_TOPIC, &telemetryHandler);
   telemetryHandler.configure(client);

   sketch.completeInitialization();
   drawStatic();
}

void loop()
{
   sketch.loop();

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
