//
// Solar Viewer
//
// Polls two Enphase IQ Gateways (Envoys) over the local network via HTTPS using JWT
// bearer tokens and renders a live view of solar production and home consumption.
//
// The main area above the bottom rolling chart is split horizontally:
// - Left side: A sectioned FieldTable showing the numeric Production, Consumption, and Net
//   values (with inline kW and kWh units) for both "Now" and "Today" in LIME and TOMATO.
// - Right side: Two center-zero horizontal bars horizontally aligned with the "Now" and
//   "Today" table sections. Each bar renders the Net Delta as a filled bar (filling
//   rightward in LIME for net export and leftward in RED for net import) and fills
//   Production (right of zero) and Consumption (left of zero) behind the net bar in
//   darker shades of green and red.
//
// Along the bottom of the display, a center-zero rolling bar chart shows the recent
// history of the "Now" net delta (production - consumption).
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
#include "FieldTable.h"
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
constexpr uint8_t POLL_INTERVAL_S = 5;
constexpr uint8_t INFLUX_UPLOAD_INTERVAL_S = 30;
constexpr auto INFLUX_MEASUREMENT = "Solar";
constexpr auto INFLUX_SITE = "Bragg";
constexpr auto PREFERENCES_NAMESPACE = "SolarViewer";
constexpr auto INFLUX_LOG_PREF_KEY = "influxLog";
constexpr auto BASELINE_DAY_PREF_KEY = "baseDay";
constexpr auto BASELINE_GW1_WH_PREF_KEY = "baseGw1Wh";
constexpr auto BASELINE_GW2_WH_PREF_KEY = "baseGw2Wh";

// Full-scale power (in kW) and daily energy (in kWh) for the vertical bar gauges.
constexpr float MAX_POWER_KW = 20.0f;
constexpr float MAX_ENERGY_KWH = 200.0f;
constexpr float WATTS_PER_KW = 1000.0f;

// ----------- Display Configuration
constexpr auto NOW_SECTION_TEXT = "Now";
constexpr auto TODAY_SECTION_TEXT = "Today";
constexpr uint8_t HEADING_TEXT_SIZE = DEFAULT_HEADING_SIZE;
constexpr uint8_t TABLE_TEXT_SIZE = DEFAULT_FOOTER_SIZE;
constexpr int16_t OUTER_MARGIN_PX = 6;
constexpr int16_t SIDE_MARGIN_LARGE_PX = 16;
constexpr int16_t SIDE_MARGIN_SMALL_PX = 4;
constexpr int16_t BAR_FRAME_INSET_PX = 1;
constexpr int16_t ZERO_TICK_LENGTH_PX = 4;
constexpr int16_t ROLLING_LABEL_GAP_PX = 4;
constexpr uint8_t ROLLING_LABEL_CHARS = 3;
constexpr uint16_t ROLLING_CHART_SPAN_M = 60;
constexpr auto ROLLING_SPAN_LABEL_TEXT = "1 hr";

constexpr Color PRODUCTION_COLOR = Color565::fromRGB(0, 72, 0);
constexpr Color CONSUMPTION_COLOR = Color565::fromRGB(72, 0, 0);
constexpr Color BAR_BACKGROUND_COLOR = Color565::fromRGB(24, 24, 24);
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
/// ValueBase adapter that formats a float variable and dynamically colors it via a
/// callback for display inside a FieldTable.
/// </summary>
///
class ColoredValue : public ValueBase
{
private:
   const float* _value;
   Color(*_colorFor)(float);

public:
   ///
   /// <summary>
   /// Initializes a new instance of the ColoredValue class.
   /// </summary>
   /// <param name="format">Format pattern used to render the value.</param>
   /// <param name="value">Caller-owned variable holding the current value.</param>
   /// <param name="colorFor">Callback mapping a value to its display color.</param>
   ///
   ColoredValue(const char* format, const float* value, Color(*colorFor)(float))
      : ValueBase(format), _value(value), _colorFor(colorFor)
   {
   }

   std::string valueText() override
   {
      if (isnan(*_value))
      {
         return _format.toNoValueString();
      }

      return _format.toString(*_value);
   }

   bool hasColor() const override
   {
      return true;
   }

   Color color() const override
   {
      return _colorFor(*_value);
   }
};

///
/// <summary>
/// Returns the display color for production values.
/// </summary>
/// <param name="value">The production value.</param>
/// <returns>The display color.</returns>
///
Color prodValueColor(float value)
{
   (void)value;
   return DELTA_POSITIVE_COLOR;
}

///
/// <summary>
/// Returns the display color for consumption values.
/// </summary>
/// <param name="value">The consumption value.</param>
/// <returns>The display color.</returns>
///
Color consValueColor(float value)
{
   (void)value;
   return Color::TOMATO;
}

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

ColoredValue prodPowerValue("##.# kW", &latestReadings.productionKw, prodValueColor);
ColoredValue consPowerValue("##.# kW", &latestReadings.consumptionKw, consValueColor);
ColoredValue deltaPowerValue("+##.# kW", &netPowerKw, deltaValueColor);
ColoredValue prodEnergyValue("###.# kWh", &latestReadings.productionTodayKwh, prodValueColor);
ColoredValue consEnergyValue("###.# kWh", &latestReadings.consumptionTodayKwh, consValueColor);
ColoredValue deltaEnergyValue("+###.# kWh", &netEnergyKwh, deltaValueColor);

FieldTable table(&sketch.arduino, 0, 0, TABLE_TEXT_SIZE);

CenterZeroHorizontalBar* nowBar = nullptr;
CenterZeroHorizontalBar* todayBar = nullptr;
MovingBarChart* rollingChart = nullptr;

Rect16 nowFrameRect;
Rect16 todayFrameRect;
Rect16 rollingChartRect;
int16_t rollingLabelRightX = 0;
int16_t rollingRightLabelX = 0;
bool isLargeDisplay = false;
Timer rollingColumnTimer(POLL_INTERVAL_S * 1000UL);
float rollingColumnSum = 0.0f;
float rollingProdSum = 0.0f;
float rollingConsSum = 0.0f;
uint16_t rollingColumnSamples = 0;

///
/// <summary>
/// Computes an inset bar rectangle inside a 1-pixel border frame.
/// </summary>
/// <param name="frameRect">The outer frame rectangle.</param>
/// <returns>The inset rectangle for the bar fill area.</returns>
///
Rect16 makeBarRect(const Rect16& frameRect)
{
   return Rect16(
      frameRect.x + BAR_FRAME_INSET_PX,
      frameRect.y + BAR_FRAME_INSET_PX,
      frameRect.width - 2 * BAR_FRAME_INSET_PX,
      frameRect.height - 2 * BAR_FRAME_INSET_PX);
}

///
/// <summary>
/// Computes the responsive display layout and constructs the left-side FieldTable and
/// right-side horizontal bar gauges aligned with the table sections.
/// </summary>
///
void initLayout()
{
   int16_t displayWidth = sketch.arduino.width();
   int16_t displayHeight = sketch.arduino.height();
   isLargeDisplay = displayHeight >= LARGE_DISPLAY_MIN_HEIGHT;

   int16_t sideMargin = isLargeDisplay ? SIDE_MARGIN_LARGE_PX : SIDE_MARGIN_SMALL_PX;

   int16_t titleHeight = sketch.arduino.charH(HEADING_TEXT_SIZE);
   int16_t rowHeight = sketch.arduino.charH(TABLE_TEXT_SIZE);

   // Populate the left-side table rows (grouped into Now and Today sections)
   table.setSectionTextSize(TABLE_TEXT_SIZE + 1);
   table.setSectionGap(rowHeight);
   table.setValueAlignment(DisplayValue::Alignment::DECIMAL);
   FieldTable::Row rows[] = {
      FieldTable::Row(NOW_SECTION_TEXT, Color::WHITE),
      FieldTable::Row("Solar", &prodPowerValue, Color::GRAY),
      FieldTable::Row("Usage", &consPowerValue, Color::GRAY),
      FieldTable::Row("Net", &deltaPowerValue, Color::GRAY),
      FieldTable::Row(TODAY_SECTION_TEXT, Color::WHITE),
      FieldTable::Row("Solar", &prodEnergyValue, Color::GRAY),
      FieldTable::Row("Usage", &consEnergyValue, Color::GRAY),
      FieldTable::Row("Net", &deltaEnergyValue, Color::GRAY),
   };
   table.addRows(rows);

   int16_t contentTopY = titleHeight + OUTER_MARGIN_PX;
   table.setPosition(
      0,
      contentTopY,
      Anchor::TOP_LEFT);

   // Place two horizontal bars ("Now" and "Today") to the right of the table,
   // vertically spanning the data rows of each corresponding table section
   Rect16 tableRect = table.getRect();
   int16_t barX = tableRect.x + tableRect.width + sideMargin;
   int16_t barWidth = max(static_cast<int16_t>(16), static_cast<int16_t>(displayWidth - sideMargin - barX));

   // Row indices: 0 = "Now", 1..3 = Now Prod/Cons/Net, 4 = "Today", 5..7 = Today Prod/Cons/Net
   int16_t nowTopY = table.getRowY(1);
   int16_t nowBottomY = table.getRowY(3) + table.getRowHeight(3);
   int16_t todayTopY = table.getRowY(5);
   int16_t todayBottomY = table.getRowY(7) + table.getRowHeight(7);

   nowFrameRect = Rect16(barX, nowTopY, barWidth, nowBottomY - nowTopY);
   todayFrameRect = Rect16(barX, todayTopY, barWidth, todayBottomY - todayTopY);

   RangeF powerRange(-MAX_POWER_KW, MAX_POWER_KW);
   RangeF energyRange(-MAX_ENERGY_KWH, MAX_ENERGY_KWH);

   nowBar = new CenterZeroHorizontalBar(
      makeBarRect(nowFrameRect),
      powerRange,
      DELTA_POSITIVE_COLOR,
      DELTA_NEGATIVE_COLOR,
      BAR_BACKGROUND_COLOR);
   nowBar->setSecondaryColors(PRODUCTION_COLOR, CONSUMPTION_COLOR);
   nowBar->setShowCenterLine(false);

   todayBar = new CenterZeroHorizontalBar(
      makeBarRect(todayFrameRect),
      energyRange,
      DELTA_POSITIVE_COLOR,
      DELTA_NEGATIVE_COLOR,
      BAR_BACKGROUND_COLOR);
   todayBar->setSecondaryColors(PRODUCTION_COLOR, CONSUMPTION_COLOR);
   todayBar->setShowCenterLine(false);

   int16_t rollingChartY = tableRect.y + tableRect.height + OUTER_MARGIN_PX;
   int16_t rollingChartHeight = max(static_cast<int16_t>(16), static_cast<int16_t>(displayHeight - rollingChartY));
   int16_t charWidth = sketch.arduino.charW(TABLE_TEXT_SIZE);
   rollingLabelRightX = charWidth * ROLLING_LABEL_CHARS;
   int16_t rollingChartX = rollingLabelRightX + ROLLING_LABEL_GAP_PX;
   int16_t rightLabelWidth = static_cast<int16_t>(strlen(ROLLING_SPAN_LABEL_TEXT)) * charWidth;
   rollingRightLabelX = displayWidth - rightLabelWidth;
   int16_t rollingChartRight = rollingRightLabelX - ROLLING_LABEL_GAP_PX;
   int16_t rollingChartWidth = max(static_cast<int16_t>(16), static_cast<int16_t>(rollingChartRight - rollingChartX));
   rollingChartRect = Rect16(rollingChartX, rollingChartY, rollingChartWidth, rollingChartHeight);
   uint32_t columnPeriodMs = (ROLLING_CHART_SPAN_M * 60UL * 1000UL) / static_cast<uint32_t>(rollingChartWidth);
   rollingColumnTimer.setDurationMs(max(1000UL, columnPeriodMs));
   rollingChart = new MovingBarChart(
      rollingChartRect,
      powerRange,
      DELTA_POSITIVE_COLOR,
      DELTA_NEGATIVE_COLOR,
      Color::BLACK,
      Color::GRAY);
   rollingChart->setSecondaryColors(PRODUCTION_COLOR, CONSUMPTION_COLOR);
}

///
/// <summary>
/// Draws the right-aligned Y-axis labels ("20", "0", and "-20") in gray to the left of
/// the bottom rolling chart and the "1 hr" X-axis span label on the far right of the
/// zero line.
/// </summary>
///
void drawRollingChartLabels()
{
   int16_t charWidth = sketch.arduino.charW(TABLE_TEXT_SIZE);
   int16_t charHeight = sketch.arduino.charH(TABLE_TEXT_SIZE);

   int16_t topY = rollingChartRect.y;
   int16_t centerY = rollingChartRect.y + (rollingChartRect.height - charHeight) / 2;
   int16_t bottomY = rollingChartRect.y + rollingChartRect.height - charHeight;

   sketch.arduino.setTextSize(TABLE_TEXT_SIZE);

   sketch.arduino.setCursor(rollingLabelRightX - 2 * charWidth, topY);
   sketch.arduino.print("20", Color::GRAY);

   sketch.arduino.setCursor(rollingLabelRightX - charWidth, centerY);
   sketch.arduino.print("0", Color::GRAY);

   sketch.arduino.setCursor(rollingLabelRightX - 3 * charWidth, bottomY);
   sketch.arduino.print("-20", Color::GRAY);

   sketch.arduino.setCursor(rollingRightLabelX, centerY);
   sketch.arduino.print(ROLLING_SPAN_LABEL_TEXT, Color::GRAY);
}

///
/// <summary>
/// Draws a 1-pixel frame rectangle on the display with top and bottom outside tick
/// marks at the horizontal zero midpoint.
/// </summary>
/// <param name="frameRect">The rectangle to draw.</param>
///
void drawBarFrame(const Rect16& frameRect)
{
   uint16_t frameColor = static_cast<uint16_t>(Color::DARKGRAY);
   uint16_t tickColor = static_cast<uint16_t>(Color::GRAY);
   sketch.arduino.display.drawRect(
      frameRect.x,
      frameRect.y,
      frameRect.width,
      frameRect.height,
      frameColor);

   int16_t centerX = frameRect.x + frameRect.width / 2;
   sketch.arduino.display.drawFastVLine(
      centerX,
      frameRect.y - ZERO_TICK_LENGTH_PX,
      ZERO_TICK_LENGTH_PX,
      tickColor);
   sketch.arduino.display.drawFastVLine(
      centerX,
      frameRect.y + frameRect.height,
      ZERO_TICK_LENGTH_PX,
      tickColor);
}

///
/// <summary>
/// Draws the static screen elements (title and horizontal bar frames) and resets the
/// dynamic fields for a full redraw.
/// </summary>
///
void drawStatic()
{
   sketch.arduino.clearDisplay();

   // Top screen title
   sketch.arduino.setTextSize(HEADING_TEXT_SIZE);
   sketch.arduino.setCursor(0, 0);
   sketch.arduino.println("Solar Monitor", Color::HEADING);

   // Bar frames and rolling chart Y-axis labels
   drawBarFrame(nowFrameRect);
   drawBarFrame(todayFrameRect);
   drawRollingChartLabels();

   nowBar->clear();
   todayBar->clear();
   rollingChart->clear();
   rollingColumnSum = 0.0f;
   rollingProdSum = 0.0f;
   rollingConsSum = 0.0f;
   rollingColumnSamples = 0;
   rollingColumnTimer.reset();

   nowBar->draw(&sketch.arduino.display);
   todayBar->draw(&sketch.arduino.display);
   rollingChart->draw(&sketch.arduino.display);

   table.invalidate();
   table.draw();
}

///
/// <summary>
/// Performs an HTTPS GET request against an Enphase gateway and parses the JSON response.
/// </summary>
/// <param name="host">Gateway hostname or IP address.</param>
/// <param name="path">URL path to request.</param>
/// <param name="jwtToken">Bearer JWT token for the target gateway.</param>
/// <param name="doc">Receives the parsed JSON document.</param>
/// <returns>True if the HTTP request and JSON deserialization succeeded; otherwise false.</returns>
///
bool fetchGatewayJson(const char* host, const char* path, const char* jwtToken, JsonDocument* doc)
{
   ASSERT(host != nullptr);
   ASSERT(path != nullptr);
   ASSERT(jwtToken != nullptr);
   ASSERT(doc != nullptr);

   WiFiClientSecure secureClient;
   secureClient.setInsecure();
   secureClient.setTimeout(HTTP_TIMEOUT_MS / 1000);

   HTTPClient http;
   http.useHTTP10(true);
   http.setTimeout(HTTP_TIMEOUT_MS);
   http.setConnectTimeout(HTTP_TIMEOUT_MS);

   std::string url = std::string("https://") + host + path;
   if (!http.begin(secureClient, url.c_str()))
   {
      Logger.log(std::string("Enphase HTTP begin failed for ") + url, LogSeverity::WARN);
      return false;
   }

   std::string authHeader = std::string("Bearer ") + jwtToken;
   http.addHeader("Authorization", authHeader.c_str());
   http.addHeader("Accept", "application/json");
   http.addHeader("Connection", "close");

   int httpCode = http.GET();
   if (httpCode != HTTP_CODE_OK)
   {
      Logger.log("Enphase HTTP GET failed (" + url + "): " + std::to_string(httpCode), LogSeverity::WARN);
      http.end();
      return false;
   }

   String payload = http.getString();
   http.end();

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
/// </summary>
/// <param name="readings">Receives the parsed production and consumption readings.</param>
/// <returns>True if at least one gateway reading succeeded; otherwise false.</returns>
///
bool fetchSolarReadings(SolarReadings* readings)
{
   ASSERT(readings != nullptr);

   JsonDocument doc;
   float prodWatts = NAN;
   float prodWhToday = NAN;

   // 1. Production live power from Gateway 1 (/production.json)
   if (fetchGatewayJson(ENPHASE_PROD_HOST, ENPHASE_PRODUCTION_PATH, ENPHASE_PROD_JWT_TOKEN, &doc))
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

   // 2. Production daily energy from Gateway 1 (/ivp/pdm/energy, since eim whToday on GW1 reports lifetime Wh)
   if (fetchGatewayJson(ENPHASE_PROD_HOST, ENPHASE_PDM_ENERGY_PATH, ENPHASE_PROD_JWT_TOKEN, &doc))
   {
      JsonVariant pcuWhToday = doc["production"]["pcu"]["wattHoursToday"];
      if (pcuWhToday.is<float>())
      {
         prodWhToday = pcuWhToday.as<float>();
         readings->gw1PcuWhToday = prodWhToday;
      }
   }

   // 3. Consumption live power and daily energy from Gateway 2 (/production.json)
   float consWatts = NAN;
   float consWhToday = NAN;

   if (fetchGatewayJson(ENPHASE_CONS_HOST, ENPHASE_PRODUCTION_PATH, ENPHASE_CONS_JWT_TOKEN, &doc))
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

   // 4. Raw monotonic energy accumulators and per-phase (L1/L2) CT readings from Gateway 2 (/ivp/meters/readings)
   if (fetchGatewayJson(ENPHASE_CONS_HOST, ENPHASE_METERS_READINGS_PATH, ENPHASE_CONS_JWT_TOKEN, &doc))
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
   if (!isnan(prodWatts) && !isnan(readings->gw2Watts))
   {
      consWatts = prodWatts + readings->gw2Watts;
   }
   else if (!isnan(readings->gw2Watts))
   {
      consWatts = readings->gw2Watts;
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
      Logger.log("Enphase poll failed to retrieve both production and consumption wNow", LogSeverity::WARN);
      return false;
   }

   if (!isnan(prodWatts))
   {
      readings->productionKw = max(0.0f, prodWatts / WATTS_PER_KW);
   }
   if (!isnan(prodWhToday))
   {
      readings->productionTodayKwh = max(0.0f, prodWhToday / WATTS_PER_KW);
   }
   if (!isnan(consWatts))
   {
      readings->consumptionKw = max(0.0f, consWatts / WATTS_PER_KW);
   }
   if (!isnan(consWhToday))
   {
      readings->consumptionTodayKwh = max(0.0f, consWhToday / WATTS_PER_KW);
   }

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
/// Updates the horizontal bars, rolling chart, and left-side FieldTable with the latest
/// solar readings.
/// </summary>
/// <param name="readings">The solar readings to render.</param>
///
void updateDisplay(const SolarReadings& readings)
{
   if (!isnan(readings.productionKw))
   {
      nowBar->setRight(readings.productionKw);
   }
   if (!isnan(readings.productionTodayKwh))
   {
      todayBar->setRight(readings.productionTodayKwh);
   }

   if (!isnan(readings.consumptionKw))
   {
      nowBar->setLeft(readings.consumptionKw);
   }
   if (!isnan(readings.consumptionTodayKwh))
   {
      todayBar->setLeft(readings.consumptionTodayKwh);
   }

   if (!isnan(readings.productionKw) && !isnan(readings.consumptionKw))
   {
      float roundedProdKw = std::round(readings.productionKw * 10.0f) / 10.0f;
      float roundedConsKw = std::round(readings.consumptionKw * 10.0f) / 10.0f;
      netPowerKw = roundedProdKw - roundedConsKw;
      nowBar->set(netPowerKw);

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
      todayBar->set(netEnergyKwh);
   }

   nowBar->draw(&sketch.arduino.display);
   todayBar->draw(&sketch.arduino.display);
   table.draw();
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

   sketch.completeInitialization();
   drawStatic();

   if (fetchSolarReadings(&latestReadings))
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

   if (fetchSolarReadings(&latestReadings))
   {
      sketch.getStatus()->setStatus(Status::RUNNING);
      updateDisplay(latestReadings);
      if (influxLoggingEnabled && influx.ready())
      {
         postSolarToInflux(latestReadings);
      }
   }
}
