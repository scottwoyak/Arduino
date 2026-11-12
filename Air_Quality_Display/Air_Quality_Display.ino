//
// Air Quality Display
//
// Reads particulate matter (PM1.0, PM2.5, PM4.0, PM10), VOC index, NOx index,
// temperature, and humidity from a Sensirion SEN55 sensor, plus CO2 from a Sensirion
// SCD41 sensor, displays each air quality metric as a vertical bar graph on the display
// (with a label below and the numeric value above each bar, colored and labeled with its
// air quality tier from Good through Hazardous), streams telemetry over a WebSocket
// connection, and uploads time-averaged readings to InfluxDB on a fixed interval.
//
// Behavior:
// - Uses the shared PublisherSketch class (see PublisherSketch.h) to own the boot/init
//   sequence: display init, status LED, sensor init, WiFi, OTA, InfluxDB setup, and the
//   telemetry client.
// - Site is fixed to "Bragg"; the device's InfluxDB location is prompted for the first
//   time it runs, then saved to Preferences. Hold buttonA or touch the screen right after
//   boot to force a re-prompt.
// - Continuously polls the SEN55 and SCD41 over I2C and updates the bar graph display,
//   telemetry stream, and InfluxDB averages.
//
// Outputs:
// - Display: vertical bar graph of PM1.0, PM2.5, PM4.0, PM10, CO2, VOC, and NOx, with
//   per-tier colored bands, white value marker line/diamond, quality score (0.0-5.0)
//   above, metric label below, and temperature and humidity below the bars.
// - Telemetry: topics are item/site/location, published as PM1.0, PM2.5, PM4.0, PM10,
//   CO2, VOC, NOx, and AirQuality (worst 0-5 score across all active readings, e.g.
//   "AirQuality/Bragg/location").
//
// Wiring:
// - SEN55 connected to the board's primary I2C bus (Wire: SDA, SCL, power, and GND).
// - SCD41 connected to the secondary I2C bus (Wire1: SDA on GPIO 11, SCL on GPIO 10,
//   power, and GND).
//
// InfluxDB points uploaded (Measurement: Sensors):
//
// - site=Bragg, location=<from prompt/Preferences>, item=SEN55
//     pm1, pm1Quality, pm25, pm25Quality, pm4, pm4Quality, pm10, pm10Quality,
//     voc, vocQuality, nox, noxQuality, temperature, humidity: time-averaged values over
//     the INFLUX_INTERVAL_S upload interval.
//
// - site=Bragg, location=<from prompt/Preferences>, item=SCD41
//     co2, co2Quality, temperature, humidity: time-averaged values over the
//     INFLUX_INTERVAL_S upload interval.
//
// - site=Bragg, location=<from prompt/Preferences>
//     airQuality: time-averaged worst air quality score (0-5) across all active sensors.
//
// - site=Bragg, location=<from prompt/Preferences>, item=CPU
//     temperature: the ESP32 CPU temperature at upload time.
//

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Waveshare ESP32-S3-Touch-LCD-3.5)."
#include "WrongBoard.h"
#endif

#include <SensirionI2cScd4x.h>
#include <SensirionI2CSen5x.h>
#include <Wire.h>

#include "AirQualityTiers.h"
#include "DisplayValue.h"
#include "LibraryVersion.h"
#include "Timer.h"

#include "WiFiSettings.h"

#include "PublisherSketch.h"

// This sketch's own version (e.g. "1.0"); MakeVersion() appends the shared
// LIBRARY_VERSION build number so shared library changes bump every sketch's
// compiled VERSION without manually editing each sketch.
const auto VERSION = MakeVersion("1.0");
constexpr auto SKETCH_NAME = "Air_Quality_Display";
constexpr auto PM1_TOPIC_PREFIX = "PM1.0";
constexpr auto PM25_TOPIC_PREFIX = "PM2.5";
constexpr auto PM4_TOPIC_PREFIX = "PM4.0";
constexpr auto PM10_TOPIC_PREFIX = "PM10";
constexpr auto CO2_TOPIC_PREFIX = "CO2";
constexpr auto VOC_TOPIC_PREFIX = "VOC";
constexpr auto NOX_TOPIC_PREFIX = "NOx";
constexpr auto AIR_QUALITY_TOPIC_PREFIX = "AirQuality";
constexpr size_t MAX_TOPIC_LENGTH = 64;
constexpr uint8_t TELEMETRY_DECIMAL_PLACES = 1;
constexpr uint8_t AIR_QUALITY_DECIMAL_PLACES = 3;
constexpr uint8_t TEXT_SIZE = DEFAULT_TEXT_SIZE;
constexpr uint8_t TEMP_HUMIDITY_TEXT_SIZE = 3;
constexpr uint8_t HEADER_TEXT_SIZE = DEFAULT_HEADING_SIZE;
constexpr uint16_t SECTION_MARGIN = 12;
constexpr uint8_t STARTUP_DELAY_S = 5;
constexpr uint8_t INFLUX_INTERVAL_S = 60;
constexpr uint8_t INFLUX_DECIMAL_PLACES = 1;
constexpr uint16_t DISPLAY_UPDATE_MS = 100;
constexpr uint8_t SCD41_SDA_PIN = 11;
constexpr uint8_t SCD41_SCL_PIN = 10;
constexpr auto PREFERENCES_NAMESPACE = "AirQuality";
constexpr auto VOC_STATE_PREF_KEY = "vocState";
constexpr uint8_t SEN55_VOC_STATE_SIZE = 8;
constexpr uint8_t VOC_CALIBRATION_M = 180;

// VOC algorithm tuning for a poorly ventilated 3D-printer room: keep the standard 100
// baseline offset, extend offset/gain learning from 12 h to 168 h (7 days) so sustained
// prints do not rapidly become the "normal" baseline, and raise gating to the maximum
// 3000 min (50 h) so elevated VOC events freeze baseline adaptation.
constexpr int16_t VOC_INDEX_OFFSET = 100;
constexpr int16_t VOC_LEARNING_TIME_OFFSET_H = 168;
constexpr int16_t VOC_LEARNING_TIME_GAIN_H = 168;
constexpr int16_t VOC_GATING_MAX_DURATION_M = 3000;
constexpr int16_t VOC_STD_INITIAL = 50;
constexpr int16_t VOC_GAIN_FACTOR = 230;

constexpr uint8_t NUM_BARS = 7;
constexpr const char* LABELS[NUM_BARS] = { "1.0", "2.5", "4.0", "10", "CO2", "VOC", "NOx" };

// Number of air quality tiers shown on each bar (0.0 to 4.0, evenly spaced; values above
// 4.0 are clamped to the top of the bar).
constexpr uint8_t DISPLAY_TIERS = 4;

// Upper limit of each tier except the last, per bar; PM1.0 and PM4.0 have no separate
// EPA AQI standard, so they use the PM2.5 limits (see AirQualityTiers.h).
constexpr const float* TIER_LIMITS[NUM_BARS] = {
   AirQualityTiers::PM25_LIMITS,
   AirQualityTiers::PM25_LIMITS,
   AirQualityTiers::PM25_LIMITS,
   AirQualityTiers::PM10_LIMITS,
   AirQualityTiers::CO2_LIMITS,
   AirQualityTiers::VOC_LIMITS,
   AirQualityTiers::NOX_LIMITS,
};

// Thickness of the value marker line, and half-width/height of the diamond drawn at its
// center, in pixels.
constexpr uint16_t VALUE_LINE_THICKNESS = 3;
constexpr uint16_t VALUE_DIAMOND_HALF = 8;

InfluxConfig INFLUX_CONFIG = {
   .context = { INFLUXDB_BUCKET, "Bragg", nullptr },
   .promptForContext = true,
   .includeCpuTemp = true,
};

// Topic buffers, filled in once the site and location are known (see onSiteResolved()).
char pm1Topic[MAX_TOPIC_LENGTH] = "";
char pm25Topic[MAX_TOPIC_LENGTH] = "";
char pm4Topic[MAX_TOPIC_LENGTH] = "";
char pm10Topic[MAX_TOPIC_LENGTH] = "";
char co2Topic[MAX_TOPIC_LENGTH] = "";
char vocTopic[MAX_TOPIC_LENGTH] = "";
char noxTopic[MAX_TOPIC_LENGTH] = "";
char airQualityTopic[MAX_TOPIC_LENGTH] = "";

extern PublisherSketch sketch;

///
/// <summary>
/// Builds the telemetry topics as "item/site/location".
/// </summary>
/// <param name="site">The resolved InfluxDB site.</param>
///
void onSiteResolved(const InfluxContext& site)
{
   std::string suffix;
   if (site.site != nullptr && site.site[0] != '\0')
   {
      suffix += std::string("/") + site.site;
   }
   if (site.location != nullptr && site.location[0] != '\0')
   {
      suffix += std::string("/") + site.location;
   }
   strlcpy(pm1Topic, (std::string(PM1_TOPIC_PREFIX) + suffix).c_str(), sizeof(pm1Topic));
   strlcpy(pm25Topic, (std::string(PM25_TOPIC_PREFIX) + suffix).c_str(), sizeof(pm25Topic));
   strlcpy(pm4Topic, (std::string(PM4_TOPIC_PREFIX) + suffix).c_str(), sizeof(pm4Topic));
   strlcpy(pm10Topic, (std::string(PM10_TOPIC_PREFIX) + suffix).c_str(), sizeof(pm10Topic));
   strlcpy(co2Topic, (std::string(CO2_TOPIC_PREFIX) + suffix).c_str(), sizeof(co2Topic));
   strlcpy(vocTopic, (std::string(VOC_TOPIC_PREFIX) + suffix).c_str(), sizeof(vocTopic));
   strlcpy(noxTopic, (std::string(NOX_TOPIC_PREFIX) + suffix).c_str(), sizeof(noxTopic));
   strlcpy(airQualityTopic, (std::string(AIR_QUALITY_TOPIC_PREFIX) + suffix).c_str(), sizeof(airQualityTopic));

   sketch.createTopic(pm1Topic, TelemetryTopicMetadata{ .unit = TelemetryUnits::MICROGRAMS_PER_CUBIC_METER, .description = "PM1.0 particulate matter concentration", .decimals = TELEMETRY_DECIMAL_PLACES });
   sketch.createTopic(pm25Topic, TelemetryTopicMetadata{ .unit = TelemetryUnits::MICROGRAMS_PER_CUBIC_METER, .description = "PM2.5 particulate matter concentration", .decimals = TELEMETRY_DECIMAL_PLACES });
   sketch.createTopic(pm4Topic, TelemetryTopicMetadata{ .unit = TelemetryUnits::MICROGRAMS_PER_CUBIC_METER, .description = "PM4.0 particulate matter concentration", .decimals = TELEMETRY_DECIMAL_PLACES });
   sketch.createTopic(pm10Topic, TelemetryTopicMetadata{ .unit = TelemetryUnits::MICROGRAMS_PER_CUBIC_METER, .description = "PM10 particulate matter concentration", .decimals = TELEMETRY_DECIMAL_PLACES });
   sketch.createTopic(co2Topic, TelemetryTopicMetadata{ .unit = TelemetryUnits::PARTS_PER_MILLION, .description = "Carbon dioxide concentration", .decimals = 0 });
   sketch.createTopic(vocTopic, TelemetryTopicMetadata{ .description = "Volatile organic compounds index", .decimals = 0 });
   sketch.createTopic(noxTopic, TelemetryTopicMetadata{ .description = "Nitrogen oxides index", .decimals = TELEMETRY_DECIMAL_PLACES });
   sketch.createTopic(airQualityTopic, TelemetryTopicMetadata{ .description = "Worst air quality score across active sensors", .decimals = AIR_QUALITY_DECIMAL_PLACES });
}

TelemetryConfig TELEMETRY_CONFIG = {
   .topic = pm25Topic,
   .decimals = TELEMETRY_DECIMAL_PLACES,
   .unit = TelemetryUnits::MICROGRAMS_PER_CUBIC_METER,
   .description = "PM2.5 particulate matter concentration",
};

SketchConfig PUBLISHER_CONFIG = {
   .sketchName = SKETCH_NAME,
   .version = VERSION,
   .preferencesNamespace = PREFERENCES_NAMESPACE,
   .cpuFrequencyMhz = 80,
   .enableOTA = true,
};

PublisherSketch sketch(PUBLISHER_CONFIG, INFLUX_CONFIG, TELEMETRY_CONFIG);

SensirionI2CSen5x sen55;
SensirionI2cScd4x scd41;
bool sen55Available = false;
bool scd41Available = false;

InfluxField* pm1Field = nullptr;
InfluxField* pm1QualityField = nullptr;
InfluxField* pm25Field = nullptr;
InfluxField* pm25QualityField = nullptr;
InfluxField* pm4Field = nullptr;
InfluxField* pm4QualityField = nullptr;
InfluxField* pm10Field = nullptr;
InfluxField* pm10QualityField = nullptr;
InfluxField* vocField = nullptr;
InfluxField* vocQualityField = nullptr;
InfluxField* noxField = nullptr;
InfluxField* noxQualityField = nullptr;
InfluxField* senTempField = nullptr;
InfluxField* senHumidityField = nullptr;

InfluxField* co2Field = nullptr;
InfluxField* co2QualityField = nullptr;
InfluxField* scdTempField = nullptr;
InfluxField* scdHumidityField = nullptr;

InfluxField* airQualityField = nullptr;

Timer influxTimer(INFLUX_INTERVAL_S * 1000UL);
Timer displayTimer(DISPLAY_UPDATE_MS);
Timer vocCalibrationTimer(VOC_CALIBRATION_M * 60UL * 1000UL);
bool vocCalibrating = false;

float pm1 = NAN;
float pm25 = NAN;
float pm4 = NAN;
float pm10 = NAN;
float co2 = NAN;
float vocIndex = NAN;
float noxIndex = NAN;
float tempF = NAN;
float humidity = NAN;
float scdTempF = NAN;
float scdHumidity = NAN;

float* const BAR_VALUES[NUM_BARS] = { &pm1, &pm25, &pm4, &pm10, &co2, &vocIndex, &noxIndex };
float drawnValues[NUM_BARS] = { -1, -1, -1, -1, -1, -1, -1 };

// Y position of each bar's last-drawn value marker (see drawValueLine()), or UINT16_MAX
// if no marker has been drawn yet; used to erase only the marker's old position instead
// of refilling the whole bar background on every update, which caused visible flicker.
uint16_t lastMarkerY[NUM_BARS] = { UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX };
Rect16 barRects[NUM_BARS] = {};
uint16_t barCenters[NUM_BARS] = {};
uint16_t valueY = 0;
uint16_t labelY = 0;
uint16_t tempHumidityY = 0;
uint16_t footerY = 0;
DisplayValue* valueFields[NUM_BARS] = {};
DisplayValue* tempField = nullptr;
DisplayValue* humidityField = nullptr;

///
/// <summary>
/// Computes the bar layout from the display's dimensions. Must be called after the display
/// has been initialized.
/// </summary>
///
void initLayout()
{
   uint16_t displayWidth = sketch.arduino.width();
   uint16_t displayHeight = sketch.arduino.height();
   uint16_t charH = sketch.arduino.charH(TEXT_SIZE);
   uint16_t tempHumidityCharH = sketch.arduino.charH(TEMP_HUMIDITY_TEXT_SIZE);
   uint16_t headerHeight = sketch.arduino.charH(HEADER_TEXT_SIZE) + SECTION_MARGIN;

   valueY = headerHeight;
   footerY = displayHeight - charH;
   tempHumidityY = footerY - tempHumidityCharH - SECTION_MARGIN;
   labelY = tempHumidityY - charH - SECTION_MARGIN;

   uint16_t barTop = valueY + charH + 2;
   uint16_t barHeight = labelY - 2 - barTop;
   uint16_t slotWidth = displayWidth / NUM_BARS;
   uint16_t barWidth = slotWidth * 2 / 3;

   Format qualityValueFormat("#.#");

   for (uint8_t i = 0; i < NUM_BARS; i++)
   {
      barCenters[i] = slotWidth * i + slotWidth / 2;
      barRects[i] = { static_cast<uint16_t>(barCenters[i] - barWidth / 2), barTop, barWidth, barHeight };

      valueFields[i] = new DisplayValue(&sketch.arduino, qualityValueFormat, TEXT_SIZE, DisplayValue::Alignment::CENTER);
      valueFields[i]->setPosition(barCenters[i], valueY);
   }

   Format tempFormat("###.#F");
   Format humidityFormat("###.#%");
   tempField = new DisplayValue(&sketch.arduino, tempFormat, TEMP_HUMIDITY_TEXT_SIZE, DisplayValue::Alignment::CENTER);
   tempField->setPosition(displayWidth / 4, tempHumidityY);
   humidityField = new DisplayValue(&sketch.arduino, humidityFormat, TEMP_HUMIDITY_TEXT_SIZE, DisplayValue::Alignment::CENTER);
   humidityField->setPosition(displayWidth * 3 / 4, tempHumidityY);
}

///
/// <summary>
/// Fills the portion of a bar's background (bounded by [yTop, yBottom)) with its tier
/// bands, so only that slice needs to be touched (e.g. to erase the old value marker)
/// instead of refilling the whole bar.
/// </summary>
/// <param name="i">Index of the bar to draw.</param>
/// <param name="yTop">Top (inclusive) of the y range to redraw.</param>
/// <param name="yBottom">Bottom (exclusive) of the y range to redraw.</param>
///
void drawBarBackgroundRange(uint8_t i, uint16_t yTop, uint16_t yBottom)
{
   const Rect16& rect = barRects[i];

   for (uint8_t tier = 0; tier < DISPLAY_TIERS; tier++)
   {
      uint16_t bandTop = rect.y + rect.height - static_cast<uint16_t>((tier + 1) * rect.height / DISPLAY_TIERS);
      uint16_t bandBottom = rect.y + rect.height - static_cast<uint16_t>(tier * rect.height / DISPLAY_TIERS);

      uint16_t drawTop = max(bandTop, yTop);
      uint16_t drawBottom = min(bandBottom, yBottom);
      if (drawBottom > drawTop)
      {
         sketch.arduino.display.fillRect(rect.x, drawTop, rect.width, drawBottom - drawTop, static_cast<uint16_t>(AirQualityTiers::TIER_COLORS[tier]));
      }
   }
}

///
/// <summary>
/// Fills a bar's entire background with horizontal bands, one per air quality tier,
/// showing where each tier's value range falls within the bar's height. This lets the
/// current value's line (drawn on top) be compared visually against the other tiers.
/// </summary>
/// <param name="i">Index of the bar to draw.</param>
///
void drawBarBackground(uint8_t i)
{
   const Rect16& rect = barRects[i];
   drawBarBackgroundRange(i, rect.y, rect.y + rect.height);
}

///
/// <summary>
/// Draws a thick white line across a bar at the height corresponding to the current
/// quality score (0.0 to DISPLAY_TIERS), with a white diamond centered on the bar, so it
/// can be visually compared against the tier bands and stands out clearly against the
/// dimmed tier colors. Only erases the previous marker's position (rather than refilling
/// the whole bar), to avoid visible flicker on every value update.
/// </summary>
/// <param name="i">Index of the bar to draw.</param>
/// <param name="quality">Current quality score to mark.</param>
///
void drawValueLine(uint8_t i, float quality)
{
   const Rect16& rect = barRects[i];
   float clampedQuality = constrain(quality, 0.0f, static_cast<float>(DISPLAY_TIERS));
   uint16_t y = rect.y + rect.height - static_cast<uint16_t>(clampedQuality / DISPLAY_TIERS * rect.height);
   y = min(y, static_cast<uint16_t>(rect.y + rect.height - 1));

   constexpr uint16_t MARKER_HALF = max(static_cast<uint16_t>(VALUE_LINE_THICKNESS / 2), VALUE_DIAMOND_HALF);
   if (lastMarkerY[i] != UINT16_MAX)
   {
      uint16_t eraseTop = (lastMarkerY[i] >= rect.y + MARKER_HALF) ? lastMarkerY[i] - MARKER_HALF : rect.y;
      uint16_t eraseBottom = min(static_cast<uint16_t>(lastMarkerY[i] + MARKER_HALF + 1), static_cast<uint16_t>(rect.y + rect.height));
      drawBarBackgroundRange(i, eraseTop, eraseBottom);
   }
   lastMarkerY[i] = y;

   uint16_t top = (y >= rect.y + VALUE_LINE_THICKNESS / 2) ? y - VALUE_LINE_THICKNESS / 2 : rect.y;
   uint16_t centerX = barCenters[i];

   // Clip the marker line and diamond to the bar's rect so the diamond's points (which
   // can extend above/below the bar) don't draw over the label/tier text above and below.
   sketch.arduino.display.setClipRect(rect.x, rect.y, rect.width, rect.height);
   sketch.arduino.display.fillRect(rect.x, top, rect.width, VALUE_LINE_THICKNESS, static_cast<uint16_t>(Color::WHITE));
   sketch.arduino.display.fillTriangle(centerX - VALUE_DIAMOND_HALF, y, centerX, y - VALUE_DIAMOND_HALF, centerX + VALUE_DIAMOND_HALF, y, static_cast<uint16_t>(Color::WHITE));
   sketch.arduino.display.fillTriangle(centerX - VALUE_DIAMOND_HALF, y, centerX, y + VALUE_DIAMOND_HALF, centerX + VALUE_DIAMOND_HALF, y, static_cast<uint16_t>(Color::WHITE));
   sketch.arduino.display.clearClipRect();
}

///
/// <summary>
/// Prints text horizontally centered on the given x position.
/// </summary>
/// <param name="text">Text to print.</param>
/// <param name="centerX">X coordinate of the center of the text.</param>
/// <param name="y">Y coordinate of the top of the text.</param>
/// <param name="color">Text color.</param>
///
void printCentered(const std::string& text, uint16_t centerX, uint16_t y, Color color)
{
   uint16_t width = text.length() * sketch.arduino.charW(TEXT_SIZE);
   sketch.arduino.setTextSize(TEXT_SIZE);
   sketch.arduino.setCursor(centerX > width / 2 ? centerX - width / 2 : 0, y);
   sketch.arduino.println(text, color);
}

///
/// <summary>
/// Clears the display and draws the static header, location/version, and bar backgrounds/labels.
/// </summary>
///
void drawStatic()
{
   sketch.arduino.clearDisplay();
   sketch.arduino.setCursor(0, 0);
   sketch.arduino.setTextSize(HEADER_TEXT_SIZE);
   sketch.arduino.printlnC("Air Quality", Color::HEADING);

   std::string siteText = sketch.context().site ? sketch.context().site : "";
   std::string locationText = sketch.context().location ? sketch.context().location : "";
   std::string topicInfo = siteText;
   if (!locationText.empty())
   {
      topicInfo += (topicInfo.empty() ? "" : "/") + locationText;
   }

   sketch.arduino.setTextSize(TEXT_SIZE);
   if (!topicInfo.empty())
   {
      sketch.arduino.setCursor(0, footerY);
      sketch.arduino.println(topicInfo, Color::DARKGRAY);
   }

   uint16_t versionWidth = strlen(VERSION) * sketch.arduino.charW(TEXT_SIZE);
   sketch.arduino.setCursor(sketch.arduino.width() > versionWidth ? sketch.arduino.width() - versionWidth : 0, footerY);
   sketch.arduino.println(VERSION, Color::DARKGRAY);

   for (uint8_t i = 0; i < NUM_BARS; i++)
   {
      drawBarBackground(i);
      printCentered(LABELS[i], barCenters[i], labelY, Color::HEADING);
      drawnValues[i] = -1;
      lastMarkerY[i] = UINT16_MAX;
   }
}

///
/// <summary>
/// Computes the worst (highest) fractional air quality score (0.00-5.00) across all
/// active sensor readings, or NAN if no sensor has produced a valid reading yet.
/// </summary>
/// <returns>Worst fractional air quality score (0.00-5.00), or NAN if no readings are available.</returns>
///
float getWorstAirQualityScore()
{
   struct MetricRating
   {
      float value;
      const float* limits;
   };

   const MetricRating metrics[] = {
      { pm1, AirQualityTiers::PM25_LIMITS },
      { pm25, AirQualityTiers::PM25_LIMITS },
      { pm4, AirQualityTiers::PM25_LIMITS },
      { pm10, AirQualityTiers::PM10_LIMITS },
      { co2, AirQualityTiers::CO2_LIMITS },
      { vocIndex > 0 ? vocIndex : NAN, AirQualityTiers::VOC_LIMITS },
      { noxIndex > 0 ? noxIndex : NAN, AirQualityTiers::NOX_LIMITS },
   };

   float worstScore = NAN;
   for (const MetricRating& metric : metrics)
   {
      if (!isnan(metric.value))
      {
         float score = AirQualityTiers::scoreFor(metric.value, metric.limits);
         if (isnan(worstScore) || score > worstScore)
         {
            worstScore = score;
         }
      }
   }

   return worstScore;
}

///
/// <summary>
/// Computes the worst (highest) discrete air quality tier (0-5) across all active sensor
/// readings, or -1 if no sensor has produced a valid reading yet.
/// </summary>
/// <returns>Worst tier index (0-5), or -1 if no readings are available.</returns>
///
int8_t getWorstTier()
{
   struct MetricRating
   {
      float value;
      const float* limits;
   };

   const MetricRating metrics[] = {
      { pm1, AirQualityTiers::PM25_LIMITS },
      { pm25, AirQualityTiers::PM25_LIMITS },
      { pm4, AirQualityTiers::PM25_LIMITS },
      { pm10, AirQualityTiers::PM10_LIMITS },
      { co2, AirQualityTiers::CO2_LIMITS },
      { vocIndex > 0 ? vocIndex : NAN, AirQualityTiers::VOC_LIMITS },
      { noxIndex > 0 ? noxIndex : NAN, AirQualityTiers::NOX_LIMITS },
   };

   int8_t worstTier = -1;
   for (const MetricRating& metric : metrics)
   {
      if (!isnan(metric.value))
      {
         int8_t tier = AirQualityTiers::tierFor(metric.value, metric.limits);
         if (tier > worstTier)
         {
            worstTier = tier;
         }
      }
   }

   return worstTier;
}

///
/// <summary>
/// Records the current worst fractional air quality score (0.00-5.00) for InfluxDB and
/// publishes it over telemetry if at least one sensor reading is available.
/// </summary>
///
void publishAirQuality()
{
   float worstScore = getWorstAirQualityScore();
   if (!isnan(worstScore))
   {
      if (airQualityField != nullptr)
      {
         airQualityField->set(worstScore);
      }
      sketch.submit(airQualityTopic, worstScore);
   }
}

///
/// <summary>
/// Formats the latest PM1.0, PM2.5, PM4.0, PM10, CO2, temperature, humidity, VOC, NOx,
/// and airQuality score/tier for the "GetReadings" command registered via sketch.onReadings().
/// </summary>
/// <returns>Formatted readings text.</returns>
///
static std::string getReadings()
{
   auto formatVal = [](float val, int decimals, const char* unit) -> std::string
   {
      if (isnan(val))
      {
         return "NAN";
      }
      std::ostringstream out;
      out.precision(decimals);
      out << std::fixed << val;
      if (unit != nullptr && unit[0] != '\0')
      {
         out << " " << unit;
      }
      return out.str();
   };

   int8_t worstTier = getWorstTier();
   float worstScore = getWorstAirQualityScore();
   std::string airQualityText = (worstTier >= 0 && !isnan(worstScore))
      ? formatVal(worstScore, AIR_QUALITY_DECIMAL_PLACES, "") + " (" + AirQualityTiers::TIER_NAMES[worstTier] + ")"
      : "NAN";

   return
      "pm1=" + formatVal(pm1, 1, "ug/m3") + "\n" +
      "pm25=" + formatVal(pm25, 1, "ug/m3") + "\n" +
      "pm4=" + formatVal(pm4, 1, "ug/m3") + "\n" +
      "pm10=" + formatVal(pm10, 1, "ug/m3") + "\n" +
      "co2=" + formatVal(co2, 0, "ppm") + "\n" +
      "temperature=" + formatVal(tempF, 1, "F") + "\n" +
      "humidity=" + formatVal(humidity, 1, "%") + "\n" +
      "scdTemperature=" + formatVal(scdTempF, 1, "F") + "\n" +
      "scdHumidity=" + formatVal(scdHumidity, 1, "%") + "\n" +
      "voc=" + formatVal(vocIndex, 0, "") + "\n" +
      "nox=" + formatVal(noxIndex, 1, "") + "\n" +
      "airQuality=" + airQualityText;
}

///
/// <summary>
/// Restores a previously saved clean-air SEN55 VOC algorithm state from Preferences, if
/// present. Must be called while the SEN55 is in idle mode before startMeasurement().
/// </summary>
/// <returns>True if a saved state was found and applied to the SEN55.</returns>
///
bool restoreSen55VocBaseline()
{
   uint8_t state[SEN55_VOC_STATE_SIZE] = {};
   sketch.arduino.preferences.begin(PREFERENCES_NAMESPACE, true);
   size_t readLen = sketch.arduino.preferences.getBytes(VOC_STATE_PREF_KEY, state, sizeof(state));
   sketch.arduino.preferences.end();

   if (readLen != sizeof(state))
   {
      return false;
   }

   return sen55.setVocAlgorithmState(state, sizeof(state)) == 0;
}

///
/// <summary>
/// Saves the current SEN55 VOC algorithm state to Preferences as the clean-air baseline.
/// </summary>
/// <returns>True if the state was read and saved successfully.</returns>
///
bool saveSen55VocBaseline()
{
   uint8_t state[SEN55_VOC_STATE_SIZE] = {};
   if (sen55.getVocAlgorithmState(state, sizeof(state)) != 0)
   {
      return false;
   }

   sketch.arduino.preferences.begin(PREFERENCES_NAMESPACE, false);
   size_t written = sketch.arduino.preferences.putBytes(VOC_STATE_PREF_KEY, state, sizeof(state));
   sketch.arduino.preferences.end();

   return written == sizeof(state);
}

///
/// <summary>
/// Executes the clean-air calibration cycle: optionally recalibrates SCD41 CO2 to
/// targetPpm, clears any saved SEN55 VOC baseline, resets SEN55 VOC learning, and starts
/// the automatic baseline-save timer.
/// </summary>
/// <param name="targetPpm">Reference CO2 in ppm (400-2000).</param>
///
static void executeCalibration(int targetPpm)
{
   std::ostringstream result;

   if (scd41Available)
   {
      scd41.stopPeriodicMeasurement();
      uint16_t frcCorrection = 0;
      int16_t err = scd41.performForcedRecalibration(static_cast<uint16_t>(targetPpm), frcCorrection);
      scd41.startPeriodicMeasurement();

      if (err != 0 || frcCorrection == 0xFFFF)
      {
         result << "SCD41 CO2 recalibration failed. ";
      }
      else
      {
         int32_t correctionPpm = static_cast<int32_t>(frcCorrection) - 0x8000;
         result << "Calibrated CO2 to " << targetPpm << " ppm (correction=" << correctionPpm << " ppm). ";
      }
   }

   sketch.arduino.preferences.begin(PREFERENCES_NAMESPACE, false);
   sketch.arduino.preferences.remove(VOC_STATE_PREF_KEY);
   sketch.arduino.preferences.end();

   if (sen55Available)
   {
      sen55.deviceReset();
      sen55.setVocAlgorithmTuningParameters(
         VOC_INDEX_OFFSET,
         VOC_LEARNING_TIME_OFFSET_H,
         VOC_LEARNING_TIME_GAIN_H,
         VOC_GATING_MAX_DURATION_M,
         VOC_STD_INITIAL,
         VOC_GAIN_FACTOR);
      sen55.startMeasurement();
      vocIndex = NAN;
      vocCalibrating = true;
      vocCalibrationTimer.reset();
      result << "Started " << static_cast<int>(VOC_CALIBRATION_M)
             << "-minute SEN55 VOC clean-air calibration; baseline will be saved automatically when complete.";
   }
   else
   {
      result << "Cleared saved VOC baseline (SEN55 not found).";
   }

   DeviceServerClient::respond(result.str().c_str());
}

static int pendingCalibrationPpm = 400;

///
/// <summary>
/// Handles the y/n confirmation answer for the interactive shell "Calibrate" prompt.
/// </summary>
/// <param name="line">User response entered in the shell.</param>
///
static void onCalibrateConfirm(const char* line)
{
   String input = String(line);
   input.trim();

   if (input.equalsIgnoreCase("y") || input.equalsIgnoreCase("yes"))
   {
      executeCalibration(pendingCalibrationPpm);
   }
   else
   {
      DeviceServerClient::respond("Calibration canceled.");
   }
}

///
/// <summary>
/// Commands registered with DeviceServerClient, listed by "Help" after the built-ins.
/// </summary>
///
static constexpr DeviceServerClient::CommandInfo AIR_QUALITY_COMMANDS[] = {
   {
      "Calibrate",
      "Calibrates CO2 to the reference ppm and runs a 3-hour clean-air VOC baseline calibration",
      { "cal", "fc" },
      {
         { "ppm", "Reference CO2 in ppm (400-2000)", false, "400" },
      },
      true,
   },
};

///
/// <summary>
/// Handles custom commands registered via sketch.setCommandHandler().
/// </summary>
/// <param name="command">Full command text received from the DeviceServer.</param>
///
static void onAirQualityCommand(const char* command)
{
   std::istringstream stream(command);
   std::string verb;
   stream >> verb;

   if (strcasecmp(verb.c_str(), "Calibrate") == 0)
   {
      int targetPpm = 400;
      if (stream >> targetPpm)
      {
         if (targetPpm < 400 || targetPpm > 2000)
         {
            DeviceServerClient::respond("Error: target ppm must be between 400 and 2000");
            return;
         }
      }

      if (DeviceServerClient::isShellCommand())
      {
         pendingCalibrationPpm = targetPpm;
         DeviceServerClient::shell().println("Clean-air calibration will:");
         if (scd41Available)
         {
            DeviceServerClient::shell().print("  1. Recalibrate the SCD41 CO2 sensor to ");
            DeviceServerClient::shell().print(targetPpm);
            DeviceServerClient::shell().println(" ppm.");
         }
         DeviceServerClient::shell().println("  2. Clear the saved VOC baseline and restart SEN55 VOC learning.");
         DeviceServerClient::shell().print("  3. Automatically save the new clean-air VOC baseline after ");
         DeviceServerClient::shell().print(VOC_CALIBRATION_M);
         DeviceServerClient::shell().println(" minutes (3 hours).");
         DeviceServerClient::shell().println("Ensure the room is well ventilated and the 3D printer remains off for the next 3 hours.");
         DeviceServerClient::shell().print("Continue? (y/N): ");
         DeviceServerClient::shell().flushOutput();
         DeviceServerClient::promptShellLine(onCalibrateConfirm);
         return;
      }

      executeCalibration(targetPpm);
   }
   else
   {
      DeviceServerClient::respond((std::string("Unhandled command: ") + command).c_str());
   }
}

void setup()
{
   sketch.addSensor("SEN55", []()
   {
      sen55.begin(Wire);
      if (sen55.deviceReset() != 0)
      {
         return false;
      }
      sen55.setVocAlgorithmTuningParameters(
         VOC_INDEX_OFFSET,
         VOC_LEARNING_TIME_OFFSET_H,
         VOC_LEARNING_TIME_GAIN_H,
         VOC_GATING_MAX_DURATION_M,
         VOC_STD_INITIAL,
         VOC_GAIN_FACTOR);
      restoreSen55VocBaseline();
      sen55Available = sen55.startMeasurement() == 0;
      return sen55Available;
   }, nullptr, false);
   sketch.addSensor("SCD41", []()
   {
      Wire1.begin(SCD41_SDA_PIN, SCD41_SCL_PIN);
      scd41.begin(Wire1, SCD41_I2C_ADDR_62);
      scd41.wakeUp();
      scd41.stopPeriodicMeasurement();
      scd41.reinit();
      scd41Available = scd41.startPeriodicMeasurement() == 0;
      return scd41Available;
   }, nullptr, false);
   sketch.setValueSource([]() { return pm25; });
   sketch.onSiteResolved(onSiteResolved);
   sketch.onReadings(getReadings, "Reports the latest PM1.0, PM2.5, PM4.0, PM10, CO2, temperature, humidity, VOC, NOx, and airQuality");
   sketch.setCommandHandler(onAirQualityCommand, AIR_QUALITY_COMMANDS, std::size(AIR_QUALITY_COMMANDS));
   sketch.arduino.setRotation(DisplayRotation::PORTRAIT);

   sketch.begin();

   initLayout();

   if (sen55Available)
   {
      InfluxPoint* senPoint = sketch.addPoint({ { "item", "SEN55" } });
      pm1Field = senPoint->addTimeAverageField(INFLUX_INTERVAL_S, "pm1", INFLUX_DECIMAL_PLACES);
      pm1QualityField = senPoint->addTimeAverageField(INFLUX_INTERVAL_S, "pm1Quality", AIR_QUALITY_DECIMAL_PLACES);
      pm25Field = senPoint->addTimeAverageField(INFLUX_INTERVAL_S, "pm25", INFLUX_DECIMAL_PLACES);
      pm25QualityField = senPoint->addTimeAverageField(INFLUX_INTERVAL_S, "pm25Quality", AIR_QUALITY_DECIMAL_PLACES);
      pm4Field = senPoint->addTimeAverageField(INFLUX_INTERVAL_S, "pm4", INFLUX_DECIMAL_PLACES);
      pm4QualityField = senPoint->addTimeAverageField(INFLUX_INTERVAL_S, "pm4Quality", AIR_QUALITY_DECIMAL_PLACES);
      pm10Field = senPoint->addTimeAverageField(INFLUX_INTERVAL_S, "pm10", INFLUX_DECIMAL_PLACES);
      pm10QualityField = senPoint->addTimeAverageField(INFLUX_INTERVAL_S, "pm10Quality", AIR_QUALITY_DECIMAL_PLACES);
      vocField = senPoint->addTimeAverageField(INFLUX_INTERVAL_S, "voc", INFLUX_DECIMAL_PLACES);
      vocQualityField = senPoint->addTimeAverageField(INFLUX_INTERVAL_S, "vocQuality", AIR_QUALITY_DECIMAL_PLACES);
      noxField = senPoint->addTimeAverageField(INFLUX_INTERVAL_S, "nox", INFLUX_DECIMAL_PLACES);
      noxQualityField = senPoint->addTimeAverageField(INFLUX_INTERVAL_S, "noxQuality", AIR_QUALITY_DECIMAL_PLACES);
      senTempField = senPoint->addTimeAverageField(INFLUX_INTERVAL_S, "temperature", INFLUX_DECIMAL_PLACES);
      senHumidityField = senPoint->addTimeAverageField(INFLUX_INTERVAL_S, "humidity", INFLUX_DECIMAL_PLACES);
   }
   else
   {
      Logger.log("Sensor 'SEN55' was not found; SEN55 data will not be posted", LogSeverity::WARN);
   }

   if (scd41Available)
   {
      InfluxPoint* scdPoint = sketch.addPoint({ { "item", "SCD41" } });
      co2Field = scdPoint->addTimeAverageField(INFLUX_INTERVAL_S, "co2", INFLUX_DECIMAL_PLACES);
      co2QualityField = scdPoint->addTimeAverageField(INFLUX_INTERVAL_S, "co2Quality", AIR_QUALITY_DECIMAL_PLACES);
      scdTempField = scdPoint->addTimeAverageField(INFLUX_INTERVAL_S, "temperature", INFLUX_DECIMAL_PLACES);
      scdHumidityField = scdPoint->addTimeAverageField(INFLUX_INTERVAL_S, "humidity", INFLUX_DECIMAL_PLACES);
   }
   else
   {
      Logger.log("Sensor 'SCD41' was not found; SCD41 data will not be posted", LogSeverity::WARN);
   }

   if (sen55Available || scd41Available)
   {
      InfluxPoint* airQualityPoint = sketch.addPoint();
      airQualityField = airQualityPoint->addTimeAverageField(INFLUX_INTERVAL_S, "airQuality", AIR_QUALITY_DECIMAL_PLACES);
   }

   // Pause so the initialization info can be read before it's replaced with the bar graph.
   delay(STARTUP_DELAY_S * 1000UL);

   drawStatic();

   Logger.logInitializationComplete();
}

void loop()
{
   sketch.loop();

   bool senDataReady = false;
   if (sen55Available && sen55.readDataReady(senDataReady) == 0 && senDataReady)
   {
      float measuredPm1 = NAN;
      float measuredPm25 = NAN;
      float measuredPm4 = NAN;
      float measuredPm10 = NAN;
      float measuredHumidity = NAN;
      float measuredTempC = NAN;
      float measuredVoc = NAN;
      float measuredNox = NAN;

      if (sen55.readMeasuredValues(
             measuredPm1,
             measuredPm25,
             measuredPm4,
             measuredPm10,
             measuredHumidity,
             measuredTempC,
             measuredVoc,
             measuredNox) == 0)
      {
         bool updatedAirQuality = false;

         if (!isnan(measuredPm1))
         {
            pm1Field->set(measuredPm1);
            pm1QualityField->set(AirQualityTiers::scoreFor(measuredPm1, AirQualityTiers::PM25_LIMITS));
            pm1 = pm1Field->get();
            sketch.submit(pm1Topic, pm1);
            updatedAirQuality = true;
         }

         if (!isnan(measuredPm25))
         {
            pm25Field->set(measuredPm25);
            pm25QualityField->set(AirQualityTiers::scoreFor(measuredPm25, AirQualityTiers::PM25_LIMITS));
            pm25 = pm25Field->get();
            updatedAirQuality = true;
         }

         if (!isnan(measuredPm4))
         {
            pm4Field->set(measuredPm4);
            pm4QualityField->set(AirQualityTiers::scoreFor(measuredPm4, AirQualityTiers::PM25_LIMITS));
            pm4 = pm4Field->get();
            sketch.submit(pm4Topic, pm4);
            updatedAirQuality = true;
         }

         if (!isnan(measuredPm10))
         {
            pm10Field->set(measuredPm10);
            pm10QualityField->set(AirQualityTiers::scoreFor(measuredPm10, AirQualityTiers::PM10_LIMITS));
            pm10 = pm10Field->get();
            sketch.submit(pm10Topic, pm10);
            updatedAirQuality = true;
         }

         if (!isnan(measuredTempC))
         {
            tempF = measuredTempC * 9.0f / 5.0f + 32.0f;
            senTempField->set(tempF);
         }

         if (!isnan(measuredHumidity))
         {
            humidity = measuredHumidity;
            senHumidityField->set(humidity);
         }

         if (!isnan(measuredVoc) && measuredVoc > 0)
         {
            vocIndex = measuredVoc;
            vocField->set(vocIndex);
            vocQualityField->set(AirQualityTiers::scoreFor(vocIndex, AirQualityTiers::VOC_LIMITS));
            sketch.submit(vocTopic, vocIndex);
            updatedAirQuality = true;
         }

         if (!isnan(measuredNox) && measuredNox > 0)
         {
            noxField->set(measuredNox);
            noxQualityField->set(AirQualityTiers::scoreFor(measuredNox, AirQualityTiers::NOX_LIMITS));
            noxIndex = noxField->get();
            sketch.submit(noxTopic, noxIndex);
            updatedAirQuality = true;
         }

         if (updatedAirQuality)
         {
            publishAirQuality();
         }
      }
   }

   bool scdDataReady = false;
   if (scd41Available && scd41.getDataReadyStatus(scdDataReady) == 0 && scdDataReady)
   {
      uint16_t co2Ppm = 0;
      float tempC = 0;
      float rh = 0;
      if (scd41.readMeasurement(co2Ppm, tempC, rh) == 0 && co2Ppm != 0)
      {
         co2 = co2Ppm;
         scdTempF = tempC * 9.0f / 5.0f + 32.0f;
         scdHumidity = rh;

         co2Field->set(co2);
         co2QualityField->set(AirQualityTiers::scoreFor(co2, AirQualityTiers::CO2_LIMITS));
         scdTempField->set(scdTempF);
         scdHumidityField->set(scdHumidity);

         sketch.submit(co2Topic, co2);
         publishAirQuality();
      }
   }

   if (vocCalibrating && vocCalibrationTimer.ready())
   {
      vocCalibrating = false;
      if (saveSen55VocBaseline())
      {
         Logger.log("Saved clean-air SEN55 VOC baseline to Preferences");
      }
      else
      {
         Logger.log("Failed to save SEN55 VOC baseline to Preferences", LogSeverity::ERROR);
      }
   }

   if (influxTimer.ready())
   {
      sketch.postPoints();
   }

   if (displayTimer.ready())
   {
      for (uint8_t i = 0; i < NUM_BARS; i++)
      {
         float value = *BAR_VALUES[i];
         if (isnan(value) || value == drawnValues[i])
         {
            continue;
         }

         drawnValues[i] = value;

         float quality = AirQualityTiers::scoreFor(value, TIER_LIMITS[i]);
         drawValueLine(i, quality);
         valueFields[i]->draw(quality, Color::WHITE);
      }

      float displayTempF = !isnan(tempF) ? tempF : scdTempF;
      if (!isnan(displayTempF))
      {
         tempField->draw(displayTempF);
      }

      float displayHumidity = !isnan(humidity) ? humidity : scdHumidity;
      if (!isnan(displayHumidity))
      {
         humidityField->draw(displayHumidity);
      }
   }
}
