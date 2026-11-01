//
// Solar Viewer
//
// Polls two Enphase IQ Gateways (Envoys) over the local network via HTTPS using JWT
// bearer tokens and renders a live view of solar production and home consumption.
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
#include "Influx.h"
#include "LibraryVersion.h"
#include "MovingBarChart.h"
#include "SerialX.h"
#include "Status.h"
#include "Timer.h"
#include "WiFiSettings.h"
#include "ViewerSketch.h"

const auto VERSION = MakeVersion("1.02");
constexpr auto SKETCH_NAME = "Solar_Viewer";

// ----------- Enphase IQ Gateway Configuration
// Gateway 1 (envoy.local / 192.168.1.222, SN 202202116226): Solar production array & inverters.
// Gateway 2 (envoy-2.local / 192.168.1.62, SN 202451018055): Home consumption CTs.
#ifndef ENPHASE_PROD_HOST
constexpr auto ENPHASE_PROD_HOST = "192.168.1.222";
#endif

#ifndef ENPHASE_PROD_JWT_TOKEN
constexpr auto ENPHASE_PROD_JWT_TOKEN = "eyJraWQiOiI3ZDEwMDA1ZC03ODk5LTRkMGQtYmNiNC0yNDRmOThlZTE1NmIiLCJ0eXAiOiJKV1QiLCJhbGciOiJFUzI1NiJ9.eyJhdWQiOiIyMDIyMDIxMTYyMjYiLCJpc3MiOiJFbnRyZXoiLCJlbnBoYXNlVXNlciI6Im93bmVyIiwiZXhwIjoxODI0OTAwMzgxLCJpYXQiOjE3OTMzNjQzODEsImp0aSI6ImViZjg5NWI5LWEyZjktNGU0MC1hNTk3LTcyODE0NWU3MTQ3NiIsInVzZXJuYW1lIjoic2NvdHQud295YWtAZ21haWwuY29tIn0.bA4mdV0y_CmPoRPBD8Sy1kiKkFIYmv50U1NHE7oTscwpWbNsnumY5rekUgG1puUbZBImEz0tDHC1Slqxqc82hA";
#endif

#ifndef ENPHASE_CONS_HOST
constexpr auto ENPHASE_CONS_HOST = "192.168.1.62";
#endif

#ifndef ENPHASE_CONS_JWT_TOKEN
constexpr auto ENPHASE_CONS_JWT_TOKEN = "eyJraWQiOiI3ZDEwMDA1ZC03ODk5LTRkMGQtYmNiNC0yNDRmOThlZTE1NmIiLCJ0eXAiOiJKV1QiLCJhbGciOiJFUzI1NiJ9.eyJhdWQiOiIyMDI0NTEwMTgwNTUiLCJpc3MiOiJFbnRyZXoiLCJlbnBoYXNlVXNlciI6Im93bmVyIiwiZXhwIjoxODI0OTAyNTYxLCJpYXQiOjE3OTMzNjY1NjEsImp0aSI6ImYxMzgxZjUxLWJmNGMtNDVlNC05ZjJiLWQ1MjI0YThiY2YyZSIsInVzZXJuYW1lIjoic2NvdHQud295YWtAZ21haWwuY29tIn0.X7_YYmbZe2jrH0yiH6qrDYGFFnaL7zRptNPSk6J0r1448JexvNU9dD0SgOsHT0XMgMl4783cKfrZqd9Uma0qDA";
#endif

constexpr auto ENPHASE_PRODUCTION_PATH = "/production.json";
constexpr auto ENPHASE_PDM_ENERGY_PATH = "/ivp/pdm/energy";
constexpr auto ENPHASE_METERS_READINGS_PATH = "/ivp/meters/readings";
constexpr uint32_t GW2_NET_CONSUMPTION_EID = 704643584UL;
constexpr uint16_t HTTP_TIMEOUT_MS = 10000;
constexpr uint8_t POLL_INTERVAL_S = 15;
constexpr uint8_t INFLUX_UPLOAD_INTERVAL_S = 30;
constexpr auto INFLUX_MEASUREMENT = "Solar";
constexpr auto INFLUX_SITE = "Bragg";
constexpr auto PREFERENCES_NAMESPACE = "SolarViewer";
constexpr auto INFLUX_LOG_PREF_KEY = "influxLog";
constexpr auto BASELINE_DAY_PREF_KEY = "baseDay";
constexpr auto BASELINE_GW1_WH_PREF_KEY = "baseGw1Wh";
constexpr auto BASELINE_GW2_WH_PREF_KEY = "baseGw2Wh";
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
Timer pollTimer(POLL_INTERVAL_S * 1000UL);
Influx influx(INFLUX_UPLOAD_INTERVAL_S);
bool influxReady = false;
bool influxLoggingEnabled = false;
int32_t baselineDayId = -1;
double baselineGw1WhLifetime = NAN;
double baselineGw2WhLifetime = NAN;

///
/// <summary>
/// Commands registered with DeviceServerClient, listed by "Help" after the built-ins.
/// </summary>
///
constexpr uint8_t NUM_SOLAR_COMMANDS = 2;
constexpr std::array<DeviceServerClient::CommandInfo, NUM_SOLAR_COMMANDS> SOLAR_COMMANDS = {{
   { "GetInfluxLogging", "Reports whether InfluxDB logging is enabled on this device", { "gil" } },
   {
      "SetInfluxLogging",
      "Enables or disables InfluxDB logging (on/off) and persists the setting in Preferences",
      { "sil", "LogToInflux" },
      {
         { "enabled", "Enable or disable InfluxDB logging (on/off, true/false, 1/0)", true },
      },
   },
}};

///
/// <summary>
/// Holds the latest production, consumption, and raw gateway diagnostic readings parsed
/// from both Enphase gateways.
/// </summary>
///
struct SolarReadings
{
   float productionKw = NAN;
   float productionTodayKwh = NAN;
   float consumptionKw = NAN;
   float consumptionTodayKwh = NAN;

   // Gateway 1 (Solar Production) raw metrics
   float gw1EimWatts = NAN;
   float gw1InvWatts = NAN;
   float gw1PcuWhToday = NAN;
   double gw1EimWhLifetime = NAN;
   float gw1Voltage = NAN;
   float gw1Current = NAN;
   float gw1PowerFactor = NAN;

   // Gateway 2 (Consumption / Net CT) /production.json metrics
   float gw2Watts = NAN;
   float gw2WhToday = NAN;
   double gw2WhLifetime = NAN;
   float gw2ReactiveVar = NAN;
   float gw2ApparentVa = NAN;
   float gw2PowerFactor = NAN;
   float gw2Voltage = NAN;
   float gw2Current = NAN;

   // Gateway 2 /ivp/meters/readings monotonic accumulators and split-phase L1/L2 channels
   double gw2WhDlvd = NAN;
   double gw2WhRcvd = NAN;
   float gw2L1Watts = NAN;
   float gw2L1ReactiveVar = NAN;
   float gw2L1Voltage = NAN;
   float gw2L1Current = NAN;
   float gw2L1PowerFactor = NAN;
   double gw2L1WhDlvd = NAN;
   double gw2L1WhRcvd = NAN;
   float gw2L2Watts = NAN;
   float gw2L2ReactiveVar = NAN;
   float gw2L2Voltage = NAN;
   float gw2L2Current = NAN;
   float gw2L2PowerFactor = NAN;
   double gw2L2WhDlvd = NAN;
   double gw2L2WhRcvd = NAN;

   bool valid = false;
};

SolarReadings latestReadings;
float netPowerKw = NAN;
float netEnergyKwh = NAN;

///
/// <summary>
/// Maintains a persistent HTTPS connection and cached Envoy sessionId cookie for a single
/// Enphase IQ Gateway so polls reuse both the TLS socket and the authenticated session.
/// </summary>
///
struct GatewayClient
{
   const char* host = nullptr;
   const char* jwtToken = nullptr;
   WiFiClientSecure tlsClient;
   HTTPClient http;
   std::string sessionCookie;
   bool configured = false;
};

GatewayClient prodGateway = { .host = ENPHASE_PROD_HOST, .jwtToken = ENPHASE_PROD_JWT_TOKEN };
GatewayClient consGateway = { .host = ENPHASE_CONS_HOST, .jwtToken = ENPHASE_CONS_JWT_TOKEN };

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
int16_t nowSectionTopY = 0;
int16_t nowSectionHeight = 0;
int16_t todaySectionTopY = 0;
int16_t todaySectionHeight = 0;
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
/// within the left heading column, vertically centered along the section.
/// </summary>
/// <param name="text">Heading text to draw.</param>
/// <param name="sectionTopY">Top Y coordinate of the section.</param>
/// <param name="sectionHeight">Total pixel height of the section.</param>
///
void drawRotatedSectionHeading(const char* text, int16_t sectionTopY, int16_t sectionHeight)
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
   float dstY = sectionTopY + sectionHeight / 2.0f;
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
   nowSectionTopY = rollingChartY;
   nowSectionHeight = (nowRowY + rowHeight) - rollingChartY;

   int16_t todayChartY = nowRowY + rowHeight + sectionGap;
   int16_t todayRowY = todayChartY + todayChartHeight + itemGap;
   todaySectionTopY = todayChartY;
   todaySectionHeight = (todayRowY + rowHeight) - todayChartY;

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
   sketch.arduino.println("Solar Monitor", Color::HEADING);

   // Rotated section headings on the left
   drawRotatedSectionHeading(NOW_SECTION_TEXT, nowSectionTopY, nowSectionHeight);
   drawRotatedSectionHeading(TODAY_SECTION_TEXT, todaySectionTopY, todaySectionHeight);

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

   nowSolarField.invalidate();
   nowUsageField.invalidate();
   nowNetField.invalidate();
   todaySolarField.invalidate();
   todayUsageField.invalidate();
   todayNetField.invalidate();

   nowSolarField.draw(latestReadings.productionKw, Color::GRAY, DELTA_POSITIVE_COLOR);
   nowUsageField.draw(latestReadings.consumptionKw, Color::GRAY, Color::TOMATO);
   nowNetField.draw(fabsf(netPowerKw), Color::GRAY, deltaValueColor(netPowerKw));
   todaySolarField.draw(latestReadings.productionTodayKwh, Color::GRAY, DELTA_POSITIVE_COLOR);
   todayUsageField.draw(latestReadings.consumptionTodayKwh, Color::GRAY, Color::TOMATO);
   todayNetField.draw(fabsf(netEnergyKwh), Color::GRAY, deltaValueColor(netEnergyKwh));
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
/// Performs an HTTPS GET request against an Enphase gateway using a persistent TLS client
/// and cached sessionId cookie (falling back to the Bearer JWT token only on initial login
/// or session expiration) and parses the JSON response.
/// </summary>
/// <param name="gateway">Target gateway client state (host, JWT, TLS socket, and session cookie).</param>
/// <param name="path">URL path to request.</param>
/// <param name="doc">Receives the parsed JSON document.</param>
/// <returns>True if the HTTP request and JSON deserialization succeeded; otherwise false.</returns>
///
bool fetchGatewayJson(GatewayClient* gateway, const char* path, JsonDocument* doc)
{
   ASSERT(gateway != nullptr);
   ASSERT(path != nullptr);
   ASSERT(doc != nullptr);

   if (!gateway->configured)
   {
      gateway->tlsClient.setInsecure();
      gateway->tlsClient.setTimeout(HTTP_TIMEOUT_MS / 1000);
      gateway->http.setReuse(true);
      gateway->http.setTimeout(HTTP_TIMEOUT_MS);
      gateway->http.setConnectTimeout(HTTP_TIMEOUT_MS);
      gateway->configured = true;
   }

   std::string url = std::string("https://") + gateway->host + path;
   int httpCode = 0;

   for (uint8_t attempt = 0; attempt < 2; attempt++)
   {
      bool hadOpenSocket = gateway->tlsClient.connected();
      bool usedCookie = !gateway->sessionCookie.empty();

      if (!gateway->http.begin(gateway->tlsClient, url.c_str()))
      {
         Logger.log(std::string("Enphase HTTP begin failed for ") + url, LogSeverity::WARN);
         gateway->tlsClient.stop();
         return false;
      }

      const char* headerKeys[] = { "Set-Cookie" };
      gateway->http.collectHeaders(headerKeys, 1);

      if (usedCookie)
      {
         gateway->http.addHeader("Cookie", gateway->sessionCookie.c_str());
      }
      else
      {
         std::string authHeader = std::string("Bearer ") + gateway->jwtToken;
         gateway->http.addHeader("Authorization", authHeader.c_str());
      }
      gateway->http.addHeader("Accept", "application/json");

      httpCode = gateway->http.GET();

      if (gateway->http.hasHeader("Set-Cookie"))
      {
         extractSessionCookie(gateway->http.header("Set-Cookie"), &gateway->sessionCookie);
      }

      // If the gateway closed the idle keep-alive socket between polls, reconnect and retry once.
      if (httpCode < 0 && hadOpenSocket && attempt == 0)
      {
         gateway->http.end();
         gateway->tlsClient.stop();
         continue;
      }

      // If the cached sessionId cookie expired (e.g. after a gateway reboot), re-authenticate with the JWT.
      if ((httpCode == HTTP_CODE_UNAUTHORIZED || httpCode == HTTP_CODE_FORBIDDEN) && usedCookie && attempt == 0)
      {
         gateway->sessionCookie.clear();
         gateway->http.end();
         gateway->tlsClient.stop();
         continue;
      }

      break;
   }

   if (httpCode != HTTP_CODE_OK)
   {
      Logger.log("Enphase HTTP GET failed (" + url + "): " + std::to_string(httpCode), LogSeverity::WARN);
      gateway->http.end();
      gateway->tlsClient.stop();
      return false;
   }

   String payload = gateway->http.getString();
   gateway->http.end();

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
/// Loads the persisted midnight lifetime Wh baselines from Preferences.
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
/// Updates and persists the start-of-day lifetime Wh baselines whenever the local calendar
/// day advances (at midnight) or when no baseline exists yet for the current day.
/// When initializing mid-day on first boot, seeds Gateway 1's baseline using its reported
/// daily production (gw1PcuWhToday) if available so Today production starts with the
/// full day's accumulated solar energy.
/// </summary>
/// <param name="readings">Latest raw gateway readings containing lifetime Wh accumulators.</param>
///
void updateDailyBaseline(const SolarReadings& readings)
{
   int32_t todayId = currentLocalDayId();
   if (todayId < 0 || std::isnan(readings.gw1EimWhLifetime) || std::isnan(readings.gw2WhLifetime))
   {
      return;
   }

   bool dayChanged = (baselineDayId != todayId);
   bool missingBaseline = std::isnan(baselineGw1WhLifetime) || std::isnan(baselineGw2WhLifetime);
   if (!dayChanged && !missingBaseline)
   {
      return;
   }

   time_t now = time(nullptr);
   struct tm timeInfo;
   localtime_r(&now, &timeInfo);
   bool nearMidnight = (timeInfo.tm_hour == 0 && timeInfo.tm_min < 5);

   // At midnight rollover, snapshot both lifetime counters directly.
   // If initializing for the very first time mid-day, back-calculate GW1's midnight
   // baseline from gw1PcuWhToday so Today solar doesn't start at 0 mid-day.
   if (!nearMidnight && missingBaseline && !std::isnan(readings.gw1PcuWhToday) && readings.gw1PcuWhToday > 0.0f)
   {
      baselineGw1WhLifetime = readings.gw1EimWhLifetime - static_cast<double>(readings.gw1PcuWhToday);
   }
   else
   {
      baselineGw1WhLifetime = readings.gw1EimWhLifetime;
   }

   baselineGw2WhLifetime = readings.gw2WhLifetime;
   baselineDayId = todayId;

   sketch.arduino.preferences.begin(PREFERENCES_NAMESPACE, false);
   sketch.arduino.preferences.putInt(BASELINE_DAY_PREF_KEY, baselineDayId);
   sketch.arduino.preferences.putDouble(BASELINE_GW1_WH_PREF_KEY, baselineGw1WhLifetime);
   sketch.arduino.preferences.putDouble(BASELINE_GW2_WH_PREF_KEY, baselineGw2WhLifetime);
   sketch.arduino.preferences.end();

   Logger.log("Saved daily solar baseline for dayId=" + std::to_string(baselineDayId) +
      ": gw1BaseWh=" + std::to_string(baselineGw1WhLifetime) +
      ", gw2BaseWh=" + std::to_string(baselineGw2WhLifetime));
}

///
/// <summary>
/// Queries both Enphase IQ Gateways over HTTPS (Gateway 1 for solar production and daily
/// energy, Gateway 2 for home consumption) and populates the latest readings.
/// Secondary endpoints (/ivp/pdm/energy on Gateway 1 and /ivp/meters/readings on Gateway 2)
/// are only queried when <paramref name="includeExtendedMetrics"/> is true, when seeding a
/// missing daily baseline, or as a fallback when Gateway 2's primary endpoint fails.
/// </summary>
/// <param name="readings">Receives the parsed production and consumption readings.</param>
/// <param name="includeExtendedMetrics">True to also query secondary diagnostic endpoints.</param>
/// <returns>True if at least one gateway reading succeeded; otherwise false.</returns>
///
bool fetchSolarReadings(SolarReadings* readings, bool includeExtendedMetrics = false)
{
   ASSERT(readings != nullptr);

   SolarReadings previousReadings = *readings;
   *readings = SolarReadings();

   JsonDocument doc;
   float prodWatts = NAN;
   float prodWhToday = NAN;

   // 1. Production live power from Gateway 1 (/production.json)
   if (fetchGatewayJson(&prodGateway, ENPHASE_PRODUCTION_PATH, &doc))
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
               readings->gw1EimWatts = prodWatts;
            }
            if (entry["whLifetime"].is<double>())
            {
               readings->gw1EimWhLifetime = entry["whLifetime"].as<double>();
            }
            if (entry["rmsVoltage"].is<float>())
            {
               readings->gw1Voltage = entry["rmsVoltage"].as<float>();
            }
            if (entry["rmsCurrent"].is<float>())
            {
               readings->gw1Current = entry["rmsCurrent"].as<float>();
            }
            if (entry["pwrFactor"].is<float>())
            {
               readings->gw1PowerFactor = entry["pwrFactor"].as<float>();
            }
         }
         else if (strcmp(type, "inverters") == 0 && entry["wNow"].is<float>())
         {
            readings->gw1InvWatts = entry["wNow"].as<float>();
            if (isnan(prodWatts))
            {
               prodWatts = readings->gw1InvWatts;
            }
         }
      }
   }

   // 2. Production daily energy from Gateway 1 (/ivp/pdm/energy). Only needed when extended
   // InfluxDB metrics are requested, when seeding a missing daily baseline on first boot, or
   // when gw1EimWhLifetime was unavailable from /production.json.
   bool needGw1PdmEnergy = includeExtendedMetrics ||
      std::isnan(baselineGw1WhLifetime) ||
      std::isnan(readings->gw1EimWhLifetime);
   if (needGw1PdmEnergy)
   {
      if (fetchGatewayJson(&prodGateway, ENPHASE_PDM_ENERGY_PATH, &doc))
      {
         JsonVariant pcuWhToday = doc["production"]["pcu"]["wattHoursToday"];
         if (pcuWhToday.is<float>())
         {
            prodWhToday = pcuWhToday.as<float>();
            readings->gw1PcuWhToday = prodWhToday;
         }
      }
   }
   else
   {
      readings->gw1PcuWhToday = previousReadings.gw1PcuWhToday;
   }

   // 3. Consumption live power and daily energy from Gateway 2 (/production.json)
   float consWatts = NAN;
   float consWhToday = NAN;

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
               readings->gw2Watts = entry["wNow"].as<float>();
            }
            if (entry["whToday"].is<float>())
            {
               readings->gw2WhToday = entry["whToday"].as<float>();
            }
            if (entry["whLifetime"].is<double>())
            {
               readings->gw2WhLifetime = entry["whLifetime"].as<double>();
            }
            if (entry["reactPwr"].is<float>())
            {
               readings->gw2ReactiveVar = entry["reactPwr"].as<float>();
            }
            if (entry["apprntPwr"].is<float>())
            {
               readings->gw2ApparentVa = entry["apprntPwr"].as<float>();
            }
            if (entry["pwrFactor"].is<float>())
            {
               readings->gw2PowerFactor = entry["pwrFactor"].as<float>();
            }
            if (entry["rmsVoltage"].is<float>())
            {
               readings->gw2Voltage = entry["rmsVoltage"].as<float>();
            }
            if (entry["rmsCurrent"].is<float>())
            {
               readings->gw2Current = entry["rmsCurrent"].as<float>();
            }
            break;
         }
      }
   }

   // 4. Raw monotonic energy accumulators and per-phase (L1/L2) CT readings from Gateway 2
   // (/ivp/meters/readings). Only queried when extended InfluxDB metrics are requested or as
   // a fallback if Gateway 2's /production.json request failed.
   bool needGw2MetersReadings = includeExtendedMetrics || isnan(readings->gw2Watts);
   if (needGw2MetersReadings &&
       fetchGatewayJson(&consGateway, ENPHASE_METERS_READINGS_PATH, &doc))
   {
      JsonArray metersArray = doc.as<JsonArray>();
      for (JsonObject meter : metersArray)
      {
         uint32_t eid = meter["eid"] | 0UL;
         if (eid != GW2_NET_CONSUMPTION_EID)
         {
            continue;
         }

         if (meter["actEnergyDlvd"].is<double>())
         {
            readings->gw2WhDlvd = meter["actEnergyDlvd"].as<double>();
         }
         if (meter["actEnergyRcvd"].is<double>())
         {
            readings->gw2WhRcvd = meter["actEnergyRcvd"].as<double>();
         }

         JsonArray channels = meter["channels"].as<JsonArray>();
         if (channels.size() >= 2)
         {
            JsonObject ch1 = channels[0];
            readings->gw2L1Watts = ch1["activePower"] | NAN;
            readings->gw2L1ReactiveVar = ch1["reactivePower"] | NAN;
            readings->gw2L1Voltage = ch1["voltage"] | NAN;
            readings->gw2L1Current = ch1["current"] | NAN;
            readings->gw2L1PowerFactor = ch1["pwrFactor"] | NAN;
            readings->gw2L1WhDlvd = ch1["actEnergyDlvd"] | static_cast<double>(NAN);
            readings->gw2L1WhRcvd = ch1["actEnergyRcvd"] | static_cast<double>(NAN);

            JsonObject ch2 = channels[1];
            readings->gw2L2Watts = ch2["activePower"] | NAN;
            readings->gw2L2ReactiveVar = ch2["reactivePower"] | NAN;
            readings->gw2L2Voltage = ch2["voltage"] | NAN;
            readings->gw2L2Current = ch2["current"] | NAN;
            readings->gw2L2PowerFactor = ch2["pwrFactor"] | NAN;
            readings->gw2L2WhDlvd = ch2["actEnergyDlvd"] | static_cast<double>(NAN);
            readings->gw2L2WhRcvd = ch2["actEnergyRcvd"] | static_cast<double>(NAN);
         }
         break;
      }
   }

   // Compensate display consumption using Gateway 1 solar production + signed Gateway 2 net meter flow
   // (when exporting, gw2Watts is negative so Usage = Solar - Export; when importing, Usage = Solar + Import).
   // Only use the previous poll's Gateway 2 reading as a fallback if Gateway 2 failed to respond this cycle
   // AND that previous reading is non-negative (since combining a stale negative export reading with a new
   // lower solar production value can falsely clamp Usage to 0).
   float gw2NetWatts = readings->gw2Watts;
   if (isnan(gw2NetWatts) && !isnan(readings->gw2L1Watts) && !isnan(readings->gw2L2Watts))
   {
      gw2NetWatts = readings->gw2L1Watts + readings->gw2L2Watts;
      readings->gw2Watts = gw2NetWatts;
   }

   if (!needGw2MetersReadings)
   {
      readings->gw2WhDlvd = previousReadings.gw2WhDlvd;
      readings->gw2WhRcvd = previousReadings.gw2WhRcvd;
      readings->gw2L1Watts = previousReadings.gw2L1Watts;
      readings->gw2L1ReactiveVar = previousReadings.gw2L1ReactiveVar;
      readings->gw2L1Voltage = previousReadings.gw2L1Voltage;
      readings->gw2L1Current = previousReadings.gw2L1Current;
      readings->gw2L1PowerFactor = previousReadings.gw2L1PowerFactor;
      readings->gw2L1WhDlvd = previousReadings.gw2L1WhDlvd;
      readings->gw2L1WhRcvd = previousReadings.gw2L1WhRcvd;
      readings->gw2L2Watts = previousReadings.gw2L2Watts;
      readings->gw2L2ReactiveVar = previousReadings.gw2L2ReactiveVar;
      readings->gw2L2Voltage = previousReadings.gw2L2Voltage;
      readings->gw2L2Current = previousReadings.gw2L2Current;
      readings->gw2L2PowerFactor = previousReadings.gw2L2PowerFactor;
      readings->gw2L2WhDlvd = previousReadings.gw2L2WhDlvd;
      readings->gw2L2WhRcvd = previousReadings.gw2L2WhRcvd;
   }

   if (!isnan(prodWatts) && !isnan(gw2NetWatts))
   {
      consWatts = prodWatts + gw2NetWatts;
   }
   else if (!isnan(gw2NetWatts))
   {
      consWatts = gw2NetWatts;
   }

   // Compute Today display values from the hardware lifetime Wh accumulators relative to midnight's
   // persisted baseline (bypassing Gateway 2's defective whToday counter).
   updateDailyBaseline(*readings);

   float netGridWhToday = readings->gw2WhToday;
   if (!std::isnan(readings->gw1EimWhLifetime) && !std::isnan(baselineGw1WhLifetime))
   {
      prodWhToday = static_cast<float>(readings->gw1EimWhLifetime - baselineGw1WhLifetime);
   }
   if (!std::isnan(readings->gw2WhLifetime) && !std::isnan(baselineGw2WhLifetime))
   {
      netGridWhToday = static_cast<float>(readings->gw2WhLifetime - baselineGw2WhLifetime);
   }

   if (!isnan(prodWhToday) && !isnan(netGridWhToday))
   {
      consWhToday = prodWhToday + netGridWhToday;
   }
   else if (!isnan(netGridWhToday))
   {
      consWhToday = netGridWhToday;
   }

   if (isnan(prodWatts) && isnan(consWatts))
   {
      *readings = previousReadings;
      Logger.log("Enphase poll failed to retrieve both production and consumption wNow", LogSeverity::WARN);
      return false;
   }

   readings->productionKw = !isnan(prodWatts)
      ? max(0.0f, prodWatts / WATTS_PER_KW)
      : previousReadings.productionKw;
   readings->productionTodayKwh = !isnan(prodWhToday)
      ? max(0.0f, prodWhToday / WATTS_PER_KW)
      : previousReadings.productionTodayKwh;
   readings->consumptionKw = (!isnan(consWatts) && consWatts >= 0.0f)
      ? (consWatts / WATTS_PER_KW)
      : previousReadings.consumptionKw;
   readings->consumptionTodayKwh = (!isnan(consWhToday) && consWhToday >= 0.0f)
      ? (consWhToday / WATTS_PER_KW)
      : previousReadings.consumptionTodayKwh;

   readings->valid = true;
   return true;
}

///
/// <summary>
/// Loads the persisted InfluxDB logging setting from Preferences (defaulting to false so
/// only an explicitly enabled viewer uploads to InfluxDB).
/// </summary>
///
void loadInfluxLoggingSetting()
{
   sketch.arduino.preferences.begin(PREFERENCES_NAMESPACE, true);
   influxLoggingEnabled = sketch.arduino.preferences.getBool(INFLUX_LOG_PREF_KEY, false);
   sketch.arduino.preferences.end();
}

///
/// <summary>
/// Persists the InfluxDB logging setting to Preferences and initializes the InfluxDB
/// client if logging is enabled for the first time since boot.
/// </summary>
/// <param name="enabled">True to enable InfluxDB uploads; false to disable them.</param>
///
void saveInfluxLoggingSetting(bool enabled)
{
   influxLoggingEnabled = enabled;
   sketch.arduino.preferences.begin(PREFERENCES_NAMESPACE, false);
   sketch.arduino.preferences.putBool(INFLUX_LOG_PREF_KEY, influxLoggingEnabled);
   sketch.arduino.preferences.end();

   if (influxLoggingEnabled && !influxReady)
   {
      influxReady = influx.begin(sketch.arduino);
   }
}

///
/// <summary>
/// Adds a float field to an InfluxDB Point if the value is finite.
/// </summary>
/// <param name="point">Target InfluxDB Point.</param>
/// <param name="name">Field name.</param>
/// <param name="value">Field value.</param>
/// <param name="decimalPlaces">Number of decimal places.</param>
///
void addFiniteField(Point* point, const char* name, float value, int decimalPlaces = 2)
{
   if (!std::isnan(value) && !std::isinf(value))
   {
      point->addField(name, value, decimalPlaces);
   }
}

///
/// <summary>
/// Adds a double-precision field to an InfluxDB Point if the value is finite (used for
/// large lifetime Wh accumulators that exceed 32-bit float mantissa precision).
/// </summary>
/// <param name="point">Target InfluxDB Point.</param>
/// <param name="name">Field name.</param>
/// <param name="value">Field value.</param>
/// <param name="decimalPlaces">Number of decimal places.</param>
///
void addFiniteDoubleField(Point* point, const char* name, double value, int decimalPlaces = 3)
{
   if (!std::isnan(value) && !std::isinf(value))
   {
      point->addField(name, value, decimalPlaces);
   }
}

///
/// <summary>
/// Writes the latest solar and raw Enphase gateway diagnostic metrics to InfluxDB when
/// InfluxDB logging is enabled on this device.
/// </summary>
/// <param name="readings">The solar readings to upload.</param>
///
void postSolarToInflux(const SolarReadings& readings)
{
   if (!influxLoggingEnabled || !influxReady || !readings.valid)
   {
      return;
   }

   Point point(INFLUX_MEASUREMENT);
   point.addTag("site", INFLUX_SITE);
   point.addTag("sketch", SKETCH_NAME);

   // Display-level computed values
   addFiniteField(&point, "prod_kw", readings.productionKw, 3);
   addFiniteField(&point, "cons_kw", readings.consumptionKw, 3);
   addFiniteField(&point, "net_kw", netPowerKw, 3);
   addFiniteField(&point, "prod_today_kwh", readings.productionTodayKwh, 3);
   addFiniteField(&point, "cons_today_kwh", readings.consumptionTodayKwh, 3);
   addFiniteField(&point, "net_today_kwh", netEnergyKwh, 3);

   // Gateway 1 (Solar Production) raw metrics
   addFiniteField(&point, "gw1_eim_w", readings.gw1EimWatts, 2);
   addFiniteField(&point, "gw1_inv_w", readings.gw1InvWatts, 1);
   addFiniteField(&point, "gw1_pcu_wh_today", readings.gw1PcuWhToday, 1);
   addFiniteDoubleField(&point, "gw1_eim_wh_lifetime", readings.gw1EimWhLifetime, 3);
   addFiniteField(&point, "gw1_voltage", readings.gw1Voltage, 2);
   addFiniteField(&point, "gw1_current", readings.gw1Current, 2);
   addFiniteField(&point, "gw1_pf", readings.gw1PowerFactor, 3);

   // Gateway 2 (/production.json) raw metrics
   addFiniteField(&point, "gw2_w", readings.gw2Watts, 2);
   addFiniteField(&point, "gw2_wh_today", readings.gw2WhToday, 1);
   addFiniteDoubleField(&point, "gw2_wh_lifetime", readings.gw2WhLifetime, 3);
   addFiniteField(&point, "gw2_react_var", readings.gw2ReactiveVar, 2);
   addFiniteField(&point, "gw2_apprnt_va", readings.gw2ApparentVa, 2);
   addFiniteField(&point, "gw2_pf", readings.gw2PowerFactor, 3);
   addFiniteField(&point, "gw2_voltage", readings.gw2Voltage, 2);
   addFiniteField(&point, "gw2_current", readings.gw2Current, 2);

   // Gateway 2 (/ivp/meters/readings) monotonic accumulators and L1/L2 split-phase channels
   addFiniteDoubleField(&point, "gw2_wh_dlvd", readings.gw2WhDlvd, 3);
   addFiniteDoubleField(&point, "gw2_wh_rcvd", readings.gw2WhRcvd, 3);
   addFiniteField(&point, "gw2_l1_w", readings.gw2L1Watts, 2);
   addFiniteField(&point, "gw2_l1_react_var", readings.gw2L1ReactiveVar, 2);
   addFiniteField(&point, "gw2_l1_v", readings.gw2L1Voltage, 2);
   addFiniteField(&point, "gw2_l1_a", readings.gw2L1Current, 2);
   addFiniteField(&point, "gw2_l1_pf", readings.gw2L1PowerFactor, 3);
   addFiniteDoubleField(&point, "gw2_l1_wh_dlvd", readings.gw2L1WhDlvd, 3);
   addFiniteDoubleField(&point, "gw2_l1_wh_rcvd", readings.gw2L1WhRcvd, 3);
   addFiniteField(&point, "gw2_l2_w", readings.gw2L2Watts, 2);
   addFiniteField(&point, "gw2_l2_react_var", readings.gw2L2ReactiveVar, 2);
   addFiniteField(&point, "gw2_l2_v", readings.gw2L2Voltage, 2);
   addFiniteField(&point, "gw2_l2_a", readings.gw2L2Current, 2);
   addFiniteField(&point, "gw2_l2_pf", readings.gw2L2PowerFactor, 3);
   addFiniteDoubleField(&point, "gw2_l2_wh_dlvd", readings.gw2L2WhDlvd, 3);
   addFiniteDoubleField(&point, "gw2_l2_wh_rcvd", readings.gw2L2WhRcvd, 3);

   if (!influx.client()->writePoint(point))
   {
      DeviceHealth::influxFailures++;
      Logger.log(std::string("Solar InfluxDB write failed: ") + influx.client()->getLastErrorMessage().c_str(), LogSeverity::WARN);
   }
}

///
/// <summary>
/// Handles the custom commands registered via sketch.setCommandHandler().
/// </summary>
/// <param name="command">Full command text received from the DeviceServer.</param>
///
void onSolarCommand(const char* command)
{
   std::istringstream stream(command);
   std::string verb;
   stream >> verb;

   if (strcasecmp(verb.c_str(), "GetInfluxLogging") == 0)
   {
      DeviceServerClient::respond(influxLoggingEnabled ? "ON" : "OFF");
   }
   else if (strcasecmp(verb.c_str(), "SetInfluxLogging") == 0)
   {
      std::string arg;
      std::string extra;
      if (!(stream >> arg) || (stream >> extra))
      {
         DeviceServerClient::respond("Error: specify ON or OFF (or true/false, 1/0)");
         return;
      }

      bool enabled = false;
      if (strcasecmp(arg.c_str(), "on") == 0 || strcasecmp(arg.c_str(), "true") == 0 || arg == "1")
      {
         enabled = true;
      }
      else if (strcasecmp(arg.c_str(), "off") == 0 || strcasecmp(arg.c_str(), "false") == 0 || arg == "0")
      {
         enabled = false;
      }
      else
      {
         DeviceServerClient::respond("Error: specify ON or OFF (or true/false, 1/0)");
         return;
      }

      saveInfluxLoggingSetting(enabled);
      std::string msg = std::string("InfluxDB logging ") + (influxLoggingEnabled ? "ENABLED" : "DISABLED");
      Logger.log(msg);
      DeviceServerClient::respond(msg.c_str());
   }
   else
   {
      DeviceServerClient::respond((std::string("Unhandled command: ") + command).c_str());
   }
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

   if (!isnan(readings.productionTodayKwh) && !isnan(readings.consumptionTodayKwh))
   {
      float roundedProdKwh = std::round(readings.productionTodayKwh * 10.0f) / 10.0f;
      float roundedConsKwh = std::round(readings.consumptionTodayKwh * 10.0f) / 10.0f;
      netEnergyKwh = roundedProdKwh - roundedConsKwh;

      if (TimeSync::isSynced())
      {
         int32_t dayId = currentLocalDayId();
         if (todayChartDayId != dayId)
         {
            todayChart->clear();
            resetTodayChartHistory(dayId);
         }

         time_t now = time(nullptr);
         struct tm timeInfo;
         localtime_r(&now, &timeInfo);
         uint32_t secondsSinceMidnight =
            static_cast<uint32_t>(timeInfo.tm_hour) * 3600UL +
            static_cast<uint32_t>(timeInfo.tm_min) * 60UL +
            static_cast<uint32_t>(timeInfo.tm_sec);

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

   nowSolarField.draw(readings.productionKw, Color::GRAY, DELTA_POSITIVE_COLOR);
   nowUsageField.draw(readings.consumptionKw, Color::GRAY, Color::TOMATO);
   nowNetField.draw(fabsf(netPowerKw), Color::GRAY, deltaValueColor(netPowerKw));

   todaySolarField.draw(readings.productionTodayKwh, Color::GRAY, DELTA_POSITIVE_COLOR);
   todayUsageField.draw(readings.consumptionTodayKwh, Color::GRAY, Color::TOMATO);
   todayNetField.draw(fabsf(netEnergyKwh), Color::GRAY, deltaValueColor(netEnergyKwh));
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

   std::ostringstream stream;
   stream.setf(std::ios::fixed);
   stream.precision(2);
   stream << "Production: " << latestReadings.productionKw << " kW ("
          << latestReadings.productionTodayKwh << " kWh today), "
          << "Consumption: " << latestReadings.consumptionKw << " kW ("
          << latestReadings.consumptionTodayKwh << " kWh today), "
          << "Net: " << (latestReadings.productionKw - latestReadings.consumptionKw) << " kW, "
          << "InfluxLogging: " << (influxLoggingEnabled ? "ON" : "OFF") << " | "
          << "GW1[eim=" << latestReadings.gw1EimWatts << "W, inv=" << latestReadings.gw1InvWatts
          << "W, pcuToday=" << latestReadings.gw1PcuWhToday << "Wh] "
          << "GW2[wNow=" << latestReadings.gw2Watts << "W, L1=" << latestReadings.gw2L1Watts
          << "W, L2=" << latestReadings.gw2L2Watts << "W, whToday=" << latestReadings.gw2WhToday
          << "Wh, dlvd=" << latestReadings.gw2WhDlvd << "Wh, rcvd=" << latestReadings.gw2WhRcvd << "Wh]";
   return stream.str();
}

void setup()
{
   SerialX::begin();

   loadInfluxLoggingSetting();
   loadDailyBaseline();

   sketch.onReadings(formatReadings, "Reports the latest Enphase solar production and consumption readings");
   sketch.setCommandHandler(onSolarCommand, SOLAR_COMMANDS.data(), NUM_SOLAR_COMMANDS);
   sketch.begin();

   if (influxLoggingEnabled)
   {
      influxReady = influx.begin(sketch.arduino);
   }

   initLayout();
   loadTodayChartHistory();

   sketch.completeInitialization();
   drawStatic();

   if (fetchSolarReadings(&latestReadings, true))
   {
      sketch.getStatus()->setStatus(Status::RUNNING);
      updateDisplay(latestReadings);
      postSolarToInflux(latestReadings);
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

   bool uploadToInflux = influxLoggingEnabled && influx.ready();
   if (fetchSolarReadings(&latestReadings, uploadToInflux))
   {
      sketch.getStatus()->setStatus(Status::RUNNING);
      updateDisplay(latestReadings);
      if (uploadToInflux)
      {
         postSolarToInflux(latestReadings);
      }
   }
}
