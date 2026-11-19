//
// Enphase Display
//
// Polls two Enphase IQ Gateways (Envoys) over the local network via HTTPS using JWT
// bearer tokens and renders a live view of solar production and home consumption.
//
// Gateways are polled slowly because frequent polling was causing the gateways to crash.
//
// The screen is divided vertically into two sections under the "Enphase Monitor" title:
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

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Hosyond ESP32-S3 Viewer or Feather ESP32-S3)."
#include "WrongBoard.h"
#endif

#include "Bar.h"
#include "Field.h"
#include "FieldRow.h"
#include "LibraryVersion.h"
#include "MovingBarChart.h"
#include "SerialX.h"
#include "Status.h"
#include "Timer.h"
#include "WiFiSettings.h"
#include "ViewerSketch.h"

const auto VERSION = MakeVersion("1.03");
constexpr auto SKETCH_NAME = "Enphase_Display";

// ----------- Enphase IQ Gateway Configuration
// Gateway 1 (envoy.local, SN 202202116226): Solar production array & inverters.
// Gateway 2 (envoy-2.local, SN 202451018055): Home consumption CTs.
#ifndef ENPHASE_PROD_HOST
constexpr auto ENPHASE_PROD_HOST = "solar-production.home";
#endif

#ifndef ENPHASE_PROD_JWT_TOKEN
constexpr auto ENPHASE_PROD_JWT_TOKEN = "eyJraWQiOiI3ZDEwMDA1ZC03ODk5LTRkMGQtYmNiNC0yNDRmOThlZTE1NmIiLCJ0eXAiOiJKV1QiLCJhbGciOiJFUzI1NiJ9.eyJhdWQiOiIyMDIyMDIxMTYyMjYiLCJpc3MiOiJFbnRyZXoiLCJlbnBoYXNlVXNlciI6Im93bmVyIiwiZXhwIjoxODI0OTAwMzgxLCJpYXQiOjE3OTMzNjQzODEsImp0aSI6ImViZjg5NWI5LWEyZjktNGU0MC1hNTk3LTcyODE0NWU3MTQ3NiIsInVzZXJuYW1lIjoic2NvdHQud295YWtAZ21haWwuY29tIn0.bA4mdV0y_CmPoRPBD8Sy1kiKkFIYmv50U1NHE7oTscwpWbNsnumY5rekUgG1puUbZBImEz0tDHC1Slqxqc82hA";
#endif

#ifndef ENPHASE_CONS_HOST
constexpr auto ENPHASE_CONS_HOST = "solar-consumption.home";
#endif

#ifndef ENPHASE_CONS_JWT_TOKEN
constexpr auto ENPHASE_CONS_JWT_TOKEN = "eyJraWQiOiI3ZDEwMDA1ZC03ODk5LTRkMGQtYmNiNC0yNDRmOThlZTE1NmIiLCJ0eXAiOiJKV1QiLCJhbGciOiJFUzI1NiJ9.eyJhdWQiOiIyMDI0NTEwMTgwNTUiLCJpc3MiOiJFbnRyZXoiLCJlbnBoYXNlVXNlciI6Im93bmVyIiwiZXhwIjoxODI0OTAyNTYxLCJpYXQiOjE3OTMzNjY1NjEsImp0aSI6ImYxMzgxZjUxLWJmNGMtNDVlNC05ZjJiLWQ1MjI0YThiY2YyZSIsInVzZXJuYW1lIjoic2NvdHQud295YWtAZ21haWwuY29tIn0.X7_YYmbZe2jrH0yiH6qrDYGFFnaL7zRptNPSk6J0r1448JexvNU9dD0SgOsHT0XMgMl4783cKfrZqd9Uma0qDA";
#endif

constexpr auto ENPHASE_PRODUCTION_PATH = "/production.json";
constexpr auto ENPHASE_PDM_ENERGY_PATH = "/ivp/pdm/energy";
constexpr uint16_t HTTP_TIMEOUT_MS = 10000;
constexpr uint8_t POLL_INTERVAL_S = 30;
constexpr auto PREFERENCES_NAMESPACE = "EnphaseDisplay";
constexpr auto BASELINE_DAY_PREF_KEY = "baseDay";
constexpr auto BASELINE_GW1_WH_PREF_KEY = "baseGw1Wh";
constexpr auto BASELINE_GW2_WH_PREF_KEY = "baseGw2Wh";
constexpr auto TODAY_CHART_DAY_PREF_KEY = "todayDay";
constexpr auto TODAY_CHART_BINS_PREF_KEY = "todayBins";
constexpr uint16_t EMPTY_TENTHS_KWH = 0xFFFF;
constexpr uint16_t MAX_TODAY_CHART_COLS = 480;
constexpr uint8_t NEAR_MIDNIGHT_WINDOW_M = 15;

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
constexpr Color CONSUMPTION_COLOR = Color565::fromRGB(0, 48, 128);
constexpr Color TODAY_GRID_COLOR = Color565::fromRGB(48, 48, 48);
constexpr Color DELTA_POSITIVE_COLOR = Color::LIME;
constexpr Color DELTA_NEGATIVE_COLOR = Color::DODGERBLUE;

// ----------- Sketch & State
SketchConfig SKETCH_CONFIG = {
   .sketchName = SKETCH_NAME,
   .version = VERSION,
   .preferencesNamespace = PREFERENCES_NAMESPACE,
   .enableOTA = true,
   .enableRebooter = true,
};

ViewerSketch sketch(SKETCH_CONFIG);
Timer pollTimer(POLL_INTERVAL_S * 1000UL);
int32_t baselineDayId = -1;
double baselineGw1WhLifetime = NAN;
double baselineGw2WhLifetime = NAN;

///
/// <summary>
/// Holds the latest production and consumption readings parsed from both Enphase gateways.
/// </summary>
///
struct SolarReadings
{
   float productionKw = NAN;
   float productionTodayKwh = NAN;
   float consumptionKw = NAN;
   float consumptionTodayKwh = NAN;
   int32_t dayId = -1;
   bool valid = false;
};

SolarReadings latestReadings;
float netPowerKw = NAN;
float netEnergyKwh = NAN;

///
/// <summary>
/// Holds the host, JWT token, and cached Envoy sessionId cookie for a single Enphase IQ
/// Gateway so polls reuse the authenticated session without holding persistent TLS sockets
/// in memory between polls.
/// </summary>
///
struct GatewayClient
{
   const char* host = nullptr;
   const char* jwtToken = nullptr;
   std::string sessionCookie;
};

GatewayClient prodGateway = { .host = ENPHASE_PROD_HOST, .jwtToken = ENPHASE_PROD_JWT_TOKEN };
GatewayClient consGateway = { .host = ENPHASE_CONS_HOST, .jwtToken = ENPHASE_CONS_JWT_TOKEN };

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

Field nowSolarField(&sketch.arduino, "Solar", "##.# kW", TABLE_TEXT_SIZE);
Field nowUsageField(&sketch.arduino, "Usage", "##.# kW", TABLE_TEXT_SIZE);
Field nowNetField(&sketch.arduino, "Net", "##.# kW", TABLE_TEXT_SIZE);
FieldRow nowSummaryRow(&nowSolarField, &nowUsageField, &nowNetField, Color::GRAY);

Field todaySolarField(&sketch.arduino, "Solar", "###.# kWh", TABLE_TEXT_SIZE);
Field todayUsageField(&sketch.arduino, "Usage", "###.# kWh", TABLE_TEXT_SIZE);
Field todayNetField(&sketch.arduino, "Net", "###.# kWh", TABLE_TEXT_SIZE);
FieldRow todaySummaryRow(&todaySolarField, &todayUsageField, &todayNetField, Color::GRAY);

///
/// <summary>
/// Renders all six Now and Today Solar/Usage/Net value fields.
/// </summary>
/// <param name="readings">The solar readings to render.</param>
///
void drawFields(const SolarReadings& readings)
{
   nowSummaryRow.draw(
      readings.productionKw,
      DELTA_POSITIVE_COLOR,
      readings.consumptionKw,
      DELTA_NEGATIVE_COLOR,
      std::isnan(netPowerKw) ? NAN : fabsf(netPowerKw),
      deltaValueColor(netPowerKw));

   todaySummaryRow.draw(
      readings.productionTodayKwh,
      DELTA_POSITIVE_COLOR,
      readings.consumptionTodayKwh,
      DELTA_NEGATIVE_COLOR,
      std::isnan(netEnergyKwh) ? NAN : fabsf(netEnergyKwh),
      deltaValueColor(netEnergyKwh));
}

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
Timer rollingColumnTimer(POLL_INTERVAL_S * 1000UL);
float rollingColumnSum = 0.0f;
float rollingProdSum = 0.0f;
float rollingConsSum = 0.0f;
uint16_t rollingColumnSamples = 0;

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

   nowSummaryRow.setBounds(chartX, nowRowY, chartWidth);
   todaySummaryRow.setBounds(chartX, todayRowY, chartWidth);

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
/// Captures the current NTP-synced local calendar day ID and elapsed seconds since local
/// midnight from a single clock read.
/// </summary>
/// <param name="dayIdOut">Receives (year * 1000 + yday), or -1 if NTP is not synced.</param>
/// <param name="secondsSinceMidnightOut">Receives seconds elapsed since local 00:00:00, or 0 if not synced.</param>
/// <returns>True if the system clock is synced via NTP; otherwise false.</returns>
///
bool getCurrentLocalDayAndSeconds(int32_t* dayIdOut, uint32_t* secondsSinceMidnightOut)
{
   ASSERT(dayIdOut != nullptr);
   ASSERT(secondsSinceMidnightOut != nullptr);

   if (!TimeSync::isSynced())
   {
      *dayIdOut = -1;
      *secondsSinceMidnightOut = 0;
      return false;
   }

   time_t now = time(nullptr);
   struct tm timeInfo;
   localtime_r(&now, &timeInfo);
   *dayIdOut = static_cast<int32_t>(timeInfo.tm_year) * 1000 + static_cast<int32_t>(timeInfo.tm_yday);
   *secondsSinceMidnightOut =
      static_cast<uint32_t>(timeInfo.tm_hour) * 3600UL +
      static_cast<uint32_t>(timeInfo.tm_min) * 60UL +
      static_cast<uint32_t>(timeInfo.tm_sec);
   return true;
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
   int32_t dayId = -1;
   uint32_t secondsSinceMidnight = 0;
   getCurrentLocalDayAndSeconds(&dayId, &secondsSinceMidnight);
   return dayId;
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
/// Repairs any un-reset previous-night carryover spikes in loaded Today chart samples by
/// enforcing cumulative non-decreasing daily energy from midnight forward.
/// </summary>
/// <param name="numCols">Number of active columns in todayChart.</param>
/// <returns>True if any sample was modified; otherwise false.</returns>
///
bool sanitizeTodayChartSamples(uint16_t numCols)
{
   constexpr uint16_t RESET_DROP_THRESHOLD_TENTHS_KWH = 5;
   uint16_t minProdAhead = EMPTY_TENTHS_KWH;
   uint16_t minConsAhead = EMPTY_TENTHS_KWH;
   bool modified = false;

   for (int32_t i = static_cast<int32_t>(numCols) - 1; i >= 0; i--)
   {
      TodayChartSample& sample = todayChartSamples[static_cast<size_t>(i)];
      if (sample.prodTenthsKwh == EMPTY_TENTHS_KWH || sample.consTenthsKwh == EMPTY_TENTHS_KWH)
      {
         continue;
      }

      if (minProdAhead != EMPTY_TENTHS_KWH && sample.prodTenthsKwh > minProdAhead)
      {
         sample.prodTenthsKwh = (sample.prodTenthsKwh > minProdAhead + RESET_DROP_THRESHOLD_TENTHS_KWH) ? 0 : minProdAhead;
         modified = true;
      }
      minProdAhead = sample.prodTenthsKwh;

      if (minConsAhead != EMPTY_TENTHS_KWH && sample.consTenthsKwh > minConsAhead)
      {
         sample.consTenthsKwh = (sample.consTenthsKwh > minConsAhead + RESET_DROP_THRESHOLD_TENTHS_KWH) ? 0 : minConsAhead;
         modified = true;
      }
      minConsAhead = sample.consTenthsKwh;
   }

   return modified;
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

      if (sanitizeTodayChartSamples(numCols))
      {
         sketch.arduino.preferences.begin(PREFERENCES_NAMESPACE, false);
         sketch.arduino.preferences.putBytes(TODAY_CHART_BINS_PREF_KEY, todayChartSamples.data(), expectedBytes);
         sketch.arduino.preferences.end();
      }
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
///
void recordTodayChartSample(uint16_t colIndex, float prodKwh, float consKwh)
{
   uint16_t numCols = min(todayChart->getNumBars(), MAX_TODAY_CHART_COLS);
   if (colIndex >= numCols)
   {
      return;
   }

   uint16_t prodTenths = static_cast<uint16_t>(std::clamp(std::lround(prodKwh * 10.0f), 0L, 65534L));
   uint16_t consTenths = static_cast<uint16_t>(std::clamp(std::lround(consKwh * 10.0f), 0L, 65534L));

   TodayChartSample& existing = todayChartSamples[colIndex];
   if (existing.prodTenthsKwh == prodTenths && existing.consTenthsKwh == consTenths)
   {
      return;
   }

   existing.prodTenthsKwh = prodTenths;
   existing.consTenthsKwh = consTenths;

   size_t numBytes = static_cast<size_t>(numCols) * sizeof(TodayChartSample);
   sketch.arduino.preferences.begin(PREFERENCES_NAMESPACE, false);
   sketch.arduino.preferences.putInt(TODAY_CHART_DAY_PREF_KEY, todayChartDayId);
   sketch.arduino.preferences.putBytes(TODAY_CHART_BINS_PREF_KEY, todayChartSamples.data(), numBytes);
   sketch.arduino.preferences.end();
}

///
/// <summary>
/// Draws the static screen elements (title, section headings, and chart labels) and
/// resets the dynamic fields and charts for a full redraw.
/// </summary>
///
void drawStatic()
{
   sketch.arduino.clearDisplay();

   // Top screen title
   sketch.arduino.setTextSize(HEADING_TEXT_SIZE);
   sketch.arduino.setCursor(0, 0);
   sketch.arduino.println("Enphase Monitor", Color::HEADING);

   // Rotated section headings on the left
   float headingCenterX = headingColWidth / 2.0f;
   sketch.arduino.drawRotatedText(
      NOW_SECTION_TEXT,
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

   rollingChart->draw(&sketch.arduino.display);
   todayChart->draw(&sketch.arduino.display);
   drawTodayGridLines();

   nowSummaryRow.invalidate();
   todaySummaryRow.invalidate();

   drawFields(latestReadings);
}

///
/// <summary>
/// Extracts the "sessionId=..." cookie pair from a Set-Cookie response header if present.
/// </summary>
/// <param name="setCookieHeader">Raw Set-Cookie header value.</param>
/// <param name="cookieOut">Receives the extracted "sessionId=..." string when found.</param>
///
void extractSessionCookie(const String& setCookieHeader, std::string* cookieOut)
{
   ASSERT(cookieOut != nullptr);

   int sessionPos = setCookieHeader.indexOf("sessionId=");
   if (sessionPos < 0)
   {
      return;
   }

   int endPos = setCookieHeader.indexOf(';', sessionPos);
   String cookiePair = (endPos >= 0)
      ? setCookieHeader.substring(sessionPos, endPos)
      : setCookieHeader.substring(sessionPos);
   cookiePair.trim();
   if (!cookiePair.isEmpty())
   {
      *cookieOut = cookiePair.c_str();
   }
}

///
/// <summary>
/// Performs an HTTPS GET request against an Enphase gateway using a cached sessionId
/// cookie (falling back to the Bearer JWT token only on initial login or session
/// expiration) and parses the JSON response.
/// </summary>
/// <param name="gateway">Target gateway client state (host, JWT, and session cookie).</param>
/// <param name="path">URL path to request.</param>
/// <param name="doc">Receives the parsed JSON document.</param>
/// <returns>True if the HTTP request and JSON deserialization succeeded; otherwise false.</returns>
///
bool fetchGatewayJson(GatewayClient* gateway, const char* path, JsonDocument* doc)
{
   ASSERT(gateway != nullptr);
   ASSERT(path != nullptr);
   ASSERT(doc != nullptr);

   std::string url = std::string("https://") + gateway->host + path;
   int httpCode = 0;
   String payload;

   for (uint8_t attempt = 0; attempt < 2; attempt++)
   {
      sketch.loop();

      WiFiClientSecure secureClient;
      secureClient.setInsecure();
      secureClient.setTimeout(HTTP_TIMEOUT_MS / 1000);

      HTTPClient http;
      http.useHTTP10(true);
      http.setTimeout(HTTP_TIMEOUT_MS);
      http.setConnectTimeout(HTTP_TIMEOUT_MS);

      if (!http.begin(secureClient, url.c_str()))
      {
         Logger.log(std::string("Enphase HTTP begin failed for ") + url, LogSeverity::WARN);
         return false;
      }

      const char* headerKeys[] = { "Set-Cookie" };
      http.collectHeaders(headerKeys, 1);

      bool usedCookie = !gateway->sessionCookie.empty();
      if (usedCookie)
      {
         http.addHeader("Cookie", gateway->sessionCookie.c_str());
      }
      else
      {
         std::string authHeader = std::string("Bearer ") + gateway->jwtToken;
         http.addHeader("Authorization", authHeader.c_str());
      }
      http.addHeader("Accept", "application/json");
      http.addHeader("Connection", "close");

      httpCode = http.GET();

      if (http.hasHeader("Set-Cookie"))
      {
         extractSessionCookie(http.header("Set-Cookie"), &gateway->sessionCookie);
      }

      // If the cached sessionId cookie expired (e.g. after a gateway reboot), re-authenticate with the JWT.
      if ((httpCode == HTTP_CODE_UNAUTHORIZED || httpCode == HTTP_CODE_FORBIDDEN) && usedCookie && attempt == 0)
      {
         gateway->sessionCookie.clear();
         http.end();
         continue;
      }

      if (httpCode == HTTP_CODE_OK)
      {
         payload = http.getString();
      }

      http.end();
      sketch.loop();
      break;
   }

   if (httpCode != HTTP_CODE_OK)
   {
      Logger.log("Enphase HTTP GET failed (" + url + "): " + std::to_string(httpCode), LogSeverity::WARN);
      return false;
   }

   if (payload.isEmpty())
   {
      Logger.log("Enphase HTTP response body was empty (" + url + ")", LogSeverity::WARN);
      return false;
   }

   doc->clear();
   DeserializationError error = deserializeJson(*doc, payload);
   if (error)
   {
      Logger.log(std::string("Enphase JSON parse failed (") + url + "): " + error.c_str(), LogSeverity::WARN);
      return false;
   }

   return true;
}

///
/// <summary>
/// Loads the persisted midnight production and consumption lifetime Wh baselines from Preferences.
/// </summary>
///
void loadDailyBaseline()
{
   sketch.arduino.preferences.begin(PREFERENCES_NAMESPACE, true);
   baselineDayId = sketch.arduino.preferences.getInt(BASELINE_DAY_PREF_KEY, -1);
   baselineGw1WhLifetime = sketch.arduino.preferences.getDouble(BASELINE_GW1_WH_PREF_KEY, NAN);
   baselineGw2WhLifetime = sketch.arduino.preferences.getDouble(BASELINE_GW2_WH_PREF_KEY, NAN);
   sketch.arduino.preferences.end();
}

///
/// <summary>
/// Updates and persists start-of-day lifetime Wh baselines for both gateways whenever the
/// local calendar day advances (at midnight) or when a baseline is missing on first boot.
/// Using lifetime Wh counters ensures both daily totals start cleanly at 0.0 kWh at local
/// midnight even when an Enphase gateway's internal whToday rollup resets several minutes
/// after midnight. When initializing for the first time mid-day, seeds each baseline from
/// the gateway's reported daily Wh so Today totals preserve the day's accumulated energy.
/// </summary>
/// <param name="todayId">Current encoded local calendar day ID, or -1 if time is not synced.</param>
/// <param name="secondsSinceMidnight">Elapsed seconds since local midnight.</param>
/// <param name="gw1EimWhLifetime">Gateway 1 EIM lifetime production Wh accumulator.</param>
/// <param name="gw1PcuWhToday">Gateway 1 PCU reported daily production Wh (or NAN if not queried).</param>
/// <param name="gw2EimWhLifetime">Gateway 2 EIM lifetime consumption Wh accumulator.</param>
/// <param name="gw2ReportedWhToday">Gateway 2 EIM reported daily consumption Wh.</param>
///
void updateDailyBaselines(
   int32_t todayId,
   uint32_t secondsSinceMidnight,
   double gw1EimWhLifetime,
   float gw1PcuWhToday,
   double gw2EimWhLifetime,
   float gw2ReportedWhToday)
{
   if (todayId < 0)
   {
      return;
   }

   bool changed = false;
   if (baselineDayId != todayId)
   {
      baselineDayId = todayId;
      baselineGw1WhLifetime = NAN;
      baselineGw2WhLifetime = NAN;
      changed = true;
   }

   bool nearMidnight = secondsSinceMidnight < (NEAR_MIDNIGHT_WINDOW_M * 60UL);

   if (std::isnan(baselineGw1WhLifetime) && !std::isnan(gw1EimWhLifetime))
   {
      if (!nearMidnight && !std::isnan(gw1PcuWhToday) && gw1PcuWhToday > 0.0f)
      {
         baselineGw1WhLifetime = gw1EimWhLifetime - static_cast<double>(gw1PcuWhToday);
      }
      else
      {
         baselineGw1WhLifetime = gw1EimWhLifetime;
      }
      changed = true;
   }

   if (std::isnan(baselineGw2WhLifetime) && !std::isnan(gw2EimWhLifetime))
   {
      if (!nearMidnight && !std::isnan(gw2ReportedWhToday) && gw2ReportedWhToday > 0.0f)
      {
         baselineGw2WhLifetime = gw2EimWhLifetime - static_cast<double>(gw2ReportedWhToday);
      }
      else
      {
         baselineGw2WhLifetime = gw2EimWhLifetime;
      }
      changed = true;
   }

   if (!changed)
   {
      return;
   }

   sketch.arduino.preferences.begin(PREFERENCES_NAMESPACE, false);
   sketch.arduino.preferences.putInt(BASELINE_DAY_PREF_KEY, baselineDayId);
   sketch.arduino.preferences.putDouble(BASELINE_GW1_WH_PREF_KEY, baselineGw1WhLifetime);
   sketch.arduino.preferences.putDouble(BASELINE_GW2_WH_PREF_KEY, baselineGw2WhLifetime);
   sketch.arduino.preferences.end();

   Logger.log("Saved daily baselines for dayId=" + std::to_string(baselineDayId) +
      ": gw1BaseWh=" + std::to_string(baselineGw1WhLifetime) +
      ", gw2BaseWh=" + std::to_string(baselineGw2WhLifetime));
}

///
/// <summary>
/// Queries both Enphase IQ Gateways over HTTPS (Gateway 1 for solar production and daily
/// energy, Gateway 2 for home consumption and daily energy) and populates the latest readings.
/// </summary>
/// <param name="readings">Receives the parsed production and consumption readings.</param>
/// <returns>True if at least one gateway reading succeeded; otherwise false.</returns>
///
bool fetchSolarReadings(SolarReadings* readings)
{
   ASSERT(readings != nullptr);

   SolarReadings previousReadings = *readings;
   *readings = SolarReadings();

   JsonDocument doc;
   float prodWatts = NAN;
   float prodWhToday = NAN;
   double gw1EimWhLifetime = NAN;
   float gw1PcuWhToday = NAN;

   // 1. Production live power and lifetime Wh from Gateway 1 (/production.json)
   bool gw1Ok = fetchGatewayJson(&prodGateway, ENPHASE_PRODUCTION_PATH, &doc);
   if (gw1Ok)
   {
      JsonArray productionArray = doc["production"].as<JsonArray>();
      for (JsonObject entry : productionArray)
      {
         const char* type = entry["type"] | "";
         int activeCount = entry["activeCount"] | 0;
         if (activeCount <= 0)
         {
            continue;
         }

         if (strcmp(type, "eim") == 0)
         {
            if (entry["wNow"].is<float>())
            {
               prodWatts = entry["wNow"].as<float>();
            }
            if (entry["whLifetime"].is<double>())
            {
               gw1EimWhLifetime = entry["whLifetime"].as<double>();
            }
         }
         else if (strcmp(type, "inverters") == 0 && isnan(prodWatts) && entry["wNow"].is<float>())
         {
            prodWatts = entry["wNow"].as<float>();
         }
      }
   }

   int32_t preCheckDayId = -1;
   uint32_t preCheckSecs = 0;
   bool preCheckSynced = getCurrentLocalDayAndSeconds(&preCheckDayId, &preCheckSecs);
   bool preCheckNearMidnight = preCheckSynced && (preCheckSecs < (NEAR_MIDNIGHT_WINDOW_M * 60UL));
   bool needGw1MiddaySeed = !preCheckNearMidnight &&
      (std::isnan(baselineGw1WhLifetime) || (preCheckSynced && baselineDayId != preCheckDayId));

   // 2. Production daily energy from Gateway 1 (/ivp/pdm/energy). Only needed when
   // /production.json succeeded and we are seeding a missing daily baseline mid-day on
   // first boot (or if whLifetime was unavailable from /production.json).
   if (gw1Ok && (needGw1MiddaySeed || std::isnan(gw1EimWhLifetime)))
   {
      if (fetchGatewayJson(&prodGateway, ENPHASE_PDM_ENERGY_PATH, &doc))
      {
         JsonVariant pcuWhToday = doc["production"]["pcu"]["wattHoursToday"];
         if (pcuWhToday.is<float>())
         {
            gw1PcuWhToday = pcuWhToday.as<float>();
            prodWhToday = gw1PcuWhToday;
         }
      }
   }

   // 3. Consumption live power and lifetime/daily energy from Gateway 2 (/production.json)
   float consWatts = NAN;
   float consWhToday = NAN;
   double gw2EimWhLifetime = NAN;
   float gw2ReportedWhToday = NAN;

   if (fetchGatewayJson(&consGateway, ENPHASE_PRODUCTION_PATH, &doc))
   {
      JsonArray consumptionArray = doc["consumption"].as<JsonArray>();
      for (JsonObject entry : consumptionArray)
      {
         const char* measurementType = entry["measurementType"] | "";
         int activeCount = entry["activeCount"] | 0;
         if (activeCount > 0 && strcmp(measurementType, "total-consumption") == 0)
         {
            if (entry["wNow"].is<float>())
            {
               consWatts = entry["wNow"].as<float>();
            }
            if (entry["whLifetime"].is<double>())
            {
               gw2EimWhLifetime = entry["whLifetime"].as<double>();
            }
            if (entry["whToday"].is<float>())
            {
               gw2ReportedWhToday = entry["whToday"].as<float>();
            }
            break;
         }
      }
   }

   if (isnan(prodWatts) && isnan(consWatts))
   {
      *readings = previousReadings;
      Logger.log("Enphase poll failed to retrieve both production and consumption wNow", LogSeverity::WARN);
      return false;
   }

   int32_t pollDayId = -1;
   uint32_t secondsSinceMidnight = 0;
   getCurrentLocalDayAndSeconds(&pollDayId, &secondsSinceMidnight);

   updateDailyBaselines(
      pollDayId,
      secondsSinceMidnight,
      gw1EimWhLifetime,
      gw1PcuWhToday,
      gw2EimWhLifetime,
      gw2ReportedWhToday);

   if (!std::isnan(gw1EimWhLifetime) && !std::isnan(baselineGw1WhLifetime))
   {
      prodWhToday = static_cast<float>(gw1EimWhLifetime - baselineGw1WhLifetime);
   }

   if (!std::isnan(gw2EimWhLifetime) && !std::isnan(baselineGw2WhLifetime))
   {
      consWhToday = static_cast<float>(gw2EimWhLifetime - baselineGw2WhLifetime);
   }
   else if (!std::isnan(gw2ReportedWhToday))
   {
      // Fallback if whLifetime was unavailable: ignore un-reset previous-night whToday right after midnight.
      bool nearMidnight = (pollDayId >= 0) && (secondsSinceMidnight < (NEAR_MIDNIGHT_WINDOW_M * 60UL));
      float maxPlausibleWh = MAX_POWER_KW * WATTS_PER_KW * ((secondsSinceMidnight + POLL_INTERVAL_S) / 3600.0f);
      consWhToday = (nearMidnight && gw2ReportedWhToday > maxPlausibleWh) ? 0.0f : gw2ReportedWhToday;
   }

   bool sameDayAsPrevious = (pollDayId >= 0) && (previousReadings.dayId == pollDayId);

   readings->productionKw = !isnan(prodWatts)
      ? max(0.0f, prodWatts / WATTS_PER_KW)
      : previousReadings.productionKw;
   readings->productionTodayKwh = !isnan(prodWhToday)
      ? max(0.0f, prodWhToday / WATTS_PER_KW)
      : (sameDayAsPrevious ? previousReadings.productionTodayKwh : NAN);
   readings->consumptionKw = !isnan(consWatts)
      ? max(0.0f, consWatts / WATTS_PER_KW)
      : previousReadings.consumptionKw;
   readings->consumptionTodayKwh = !isnan(consWhToday)
      ? max(0.0f, consWhToday / WATTS_PER_KW)
      : (sameDayAsPrevious ? previousReadings.consumptionTodayKwh : NAN);
   readings->dayId = pollDayId;

   readings->valid = true;
   return true;
}

///
/// <summary>
/// Updates the 1-hour rolling chart, the 24-hour midnight-anchored Today chart, and the
/// Now/Today one-row Field tables with the latest solar readings.
/// </summary>
/// <param name="readings">The solar readings to render.</param>
///
void updateDisplay(const SolarReadings& readings)
{
   if (!isnan(readings.productionKw) && !isnan(readings.consumptionKw))
   {
      float roundedProdKw = std::round(readings.productionKw * 10.0f) / 10.0f;
      float roundedConsKw = std::round(readings.consumptionKw * 10.0f) / 10.0f;
      netPowerKw = roundedProdKw - roundedConsKw;

      rollingColumnSum += netPowerKw;
      rollingProdSum += readings.productionKw;
      rollingConsSum += readings.consumptionKw;
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

   int32_t dayId = -1;
   uint32_t secondsSinceMidnight = 0;
   bool timeSynced = getCurrentLocalDayAndSeconds(&dayId, &secondsSinceMidnight);
   if (timeSynced && todayChartDayId != dayId)
   {
      todayChart->clear();
      resetTodayChartHistory(dayId);
      todayChart->draw(&sketch.arduino.display);
      drawTodayGridLines();
      netEnergyKwh = NAN;
   }

   if (!isnan(readings.productionTodayKwh) &&
       !isnan(readings.consumptionTodayKwh) &&
       (!timeSynced || readings.dayId == dayId))
   {
      float roundedProdKwh = std::round(readings.productionTodayKwh * 10.0f) / 10.0f;
      float roundedConsKwh = std::round(readings.consumptionTodayKwh * 10.0f) / 10.0f;
      netEnergyKwh = roundedProdKwh - roundedConsKwh;

      if (timeSynced)
      {
         uint16_t numCols = todayChart->getNumBars();
         if (numCols > 0)
         {
            uint16_t colIndex = static_cast<uint16_t>(
               min(static_cast<uint32_t>(numCols - 1), (secondsSinceMidnight * numCols) / SECONDS_PER_DAY));
            todayChart->setAt(
               colIndex,
               netEnergyKwh,
               readings.productionTodayKwh,
               readings.consumptionTodayKwh);
            recordTodayChartSample(colIndex, readings.productionTodayKwh, readings.consumptionTodayKwh);
            todayChart->draw(&sketch.arduino.display);
            drawTodayGridLines();
         }
      }
   }

   SolarReadings displayReadings = readings;
   if (timeSynced && readings.dayId != dayId)
   {
      displayReadings.productionTodayKwh = NAN;
      displayReadings.consumptionTodayKwh = NAN;
   }
   drawFields(displayReadings);
}

///
/// <summary>
/// Formats the latest solar readings as a human-readable string for the DeviceServer
/// "GetReadings" command.
/// </summary>
/// <returns>Formatted summary of live solar production, consumption, and daily energy.</returns>
///
std::string formatReadings()
{
   if (!latestReadings.valid)
   {
      return "No Enphase readings received yet";
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
   stream << "Production: " << formatMetric(latestReadings.productionKw, "kW")
          << " (" << formatMetric(latestReadings.productionTodayKwh, "kWh today") << "), "
          << "Consumption: " << formatMetric(latestReadings.consumptionKw, "kW")
          << " (" << formatMetric(latestReadings.consumptionTodayKwh, "kWh today") << "), "
          << "Net: " << formatMetric(netPowerKw, "kW");
   return stream.str();
}

void setup()
{
   SerialX::begin();

   loadDailyBaseline();

   sketch.onReadings(formatReadings, "Reports the latest Enphase solar production and consumption readings");
   sketch.begin();

   initLayout();
   loadTodayChartHistory();

   sketch.completeInitialization();
   drawStatic();

   if (fetchSolarReadings(&latestReadings))
   {
      sketch.getStatus()->setStatus(Status::RUNNING);
      updateDisplay(latestReadings);
   }
   pollTimer.reset();
}

void loop()
{
   sketch.loop();

   if (!pollTimer.ready())
   {
      return;
   }

   if (fetchSolarReadings(&latestReadings))
   {
      sketch.getStatus()->setStatus(Status::RUNNING);
      updateDisplay(latestReadings);
   }
}
