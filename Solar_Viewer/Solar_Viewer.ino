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
#include <cmath>
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
#include "LibraryVersion.h"
#include "MovingBarChart.h"
#include "SerialX.h"
#include "Status.h"
#include "Timer.h"
#include "WiFiSettings.h"
#include "ViewerSketch.h"

const auto VERSION = MakeVersion("1.0");
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
constexpr uint16_t HTTP_TIMEOUT_MS = 10000;
constexpr uint8_t POLL_INTERVAL_S = 5;

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

constexpr Color PRODUCTION_COLOR = Color565::fromRGB(0, 72, 0);
constexpr Color CONSUMPTION_COLOR = Color565::fromRGB(72, 0, 0);
constexpr Color BAR_BACKGROUND_COLOR = Color565::fromRGB(24, 24, 24);
constexpr Color DELTA_POSITIVE_COLOR = Color::LIME;
constexpr Color DELTA_NEGATIVE_COLOR = Color::RED;

// ----------- Sketch & State
SketchConfig SKETCH_CONFIG = {
   .sketchName = SKETCH_NAME,
   .version = VERSION,
   .enableOTA = true,
   .enableRebooter = true,
};

ViewerSketch sketch(SKETCH_CONFIG);
Timer pollTimer(POLL_INTERVAL_S * 1000UL);

///
/// <summary>
/// Holds the latest production and consumption readings parsed from the Enphase gateways.
/// </summary>
///
struct SolarReadings
{
   float productionKw = NAN;
   float productionTodayKwh = NAN;
   float consumptionKw = NAN;
   float consumptionTodayKwh = NAN;
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
bool isLargeDisplay = false;

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
   Rect16 rollingRect(0, rollingChartY, displayWidth, rollingChartHeight);
   rollingChart = new MovingBarChart(
      rollingRect,
      powerRange,
      DELTA_POSITIVE_COLOR,
      DELTA_NEGATIVE_COLOR,
      Color::BLACK,
      Color::GRAY);
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

   // Bar frames
   drawBarFrame(nowFrameRect);
   drawBarFrame(todayFrameRect);

   nowBar->clear();
   todayBar->clear();
   rollingChart->clear();

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

         if (strcmp(type, "eim") == 0 && entry["wNow"].is<float>())
         {
            prodWatts = entry["wNow"].as<float>();
            break;
         }
         else if (strcmp(type, "inverters") == 0 && isnan(prodWatts) && entry["wNow"].is<float>())
         {
            prodWatts = entry["wNow"].as<float>();
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
               // Gateway 2's consumption CTs report home load with negative polarity
               consWatts = std::fabs(entry["wNow"].as<float>());
            }
            if (entry["whToday"].is<float>())
            {
               consWhToday = std::fabs(entry["whToday"].as<float>());
            }
            break;
         }
      }
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
      netPowerKw = readings.productionKw - readings.consumptionKw;
      nowBar->set(netPowerKw);
      rollingChart->set(netPowerKw);
      rollingChart->draw(&sketch.arduino.display);
   }

   if (!isnan(readings.productionTodayKwh) && !isnan(readings.consumptionTodayKwh))
   {
      netEnergyKwh = readings.productionTodayKwh - readings.consumptionTodayKwh;
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
          << "Net: " << (latestReadings.productionKw - latestReadings.consumptionKw) << " kW";
   return stream.str();
}

void setup()
{
   SerialX::begin();

   sketch.onReadings(formatReadings, "Reports the latest Enphase solar production and consumption readings");
   sketch.begin();

   initLayout();

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
