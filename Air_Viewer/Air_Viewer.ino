//
// Air Viewer
//
// Subscribes to the air quality telemetry topics published by Air_Monitor_Display
// (item/site/location for PM1.0, PM2.5, PM10, CO2, VOC and NOx) and displays each as a
// vertical bar graph, with a label below and the numeric value above each bar. Each bar
// is colored and labeled with its air quality tier (Good through Hazardous; the PM tiers
// follow the EPA AQI categories).
//
// The layout is computed at runtime from the display's dimensions, so the sketch runs on
// any display size, e.g. the Hosyond ESP32-S3 4" 480x320 display (Viewer board, the
// default) or the 240x135 display on the Feather ESP32-S3 TFT.
//

// Uncomment this to build for the Feather ESP32-S3 TFT instead of the Hosyond ESP32-S3
// Viewer board.
//#define ARDUINO_ADAFRUIT_FEATHER_ESP32S3_TFT

// Default: build for the Hosyond ESP32-S3 Viewer board's larger display. Also requires
// selecting the generic "ESP32S3 Dev Module" board in Visual Micro.
#define ARDUINO_HOSYOND_ESP32_S3_VIEWER

// TEXT_SIZE_2 and TEXT_SIZE_3 are always included regardless (see ArduinoWithDisplay.h).
#define TEXT_SIZES_CUSTOM



#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Hosyond ESP32-S3 Viewer or Feather ESP32-S3)."
#include "WrongBoard.h"
#endif

#include "AirQualityTiers.h"
#include "DisplayValue.h"
#include "LibraryVersion.h"
#include "SerialX.h"
#include "Status.h"
#include "TelemetryClient.h"
#include "Timer.h"
#include "WiFiSettings.h"
#include "ViewerSketch.h"

const auto VERSION = MakeVersion("1.1");
constexpr auto SKETCH_NAME = "Air_Viewer";
constexpr auto PREFERENCES_NAMESPACE = "AirViewer";

constexpr uint8_t NUM_BARS = 6;
constexpr const char* TOPIC_ITEMS[NUM_BARS] = { "PM1.0", "PM2.5", "PM10", "CO2", "VOC", "NOx" };

// Site choices offered when selecting which air monitor location to display; location
// is entered as free text. Edit to match your publishers.
constexpr const char* SITE_OPTIONS[] = { "Bragg", "Lake" };

constexpr const char* LABELS[NUM_BARS] = { "PM1.0", "PM2.5", "PM10", "CO2", "VOC", "NOx" };

// Value represented by a full-height bar, per bar; larger values are clamped.
constexpr float BAR_MAXES[NUM_BARS] = { 100, 100, 100, 2000, 500, 500 };

// Air quality tiers, from best to worst (see AirQualityTiers.h, shared with
// Air_Monitor_Display.ino so the two sketches can't drift apart). TIER_CHARS is
// display-specific padding width, not part of the shared tier data.
constexpr uint8_t NUM_TIERS = AirQualityTiers::NUM_TIERS;
constexpr uint8_t TIER_CHARS = 5;

// Upper limit of each tier except the last, per bar; PM1.0 has no standard, so it uses
// the PM2.5 limits (see AirQualityTiers.h).
constexpr const float* TIER_LIMITS[NUM_BARS] = {
   AirQualityTiers::PM25_LIMITS,
   AirQualityTiers::PM25_LIMITS,
   AirQualityTiers::PM10_LIMITS,
   AirQualityTiers::CO2_LIMITS,
   AirQualityTiers::VOC_LIMITS,
   AirQualityTiers::NOX_LIMITS,
};

constexpr uint8_t TEXT_SIZE = 2;
constexpr uint8_t HEADER_TEXT_SIZE = 3;
constexpr uint16_t DISPLAY_UPDATE_MS = 100;

SketchConfig SKETCH_CONFIG = {
   .sketchName = SKETCH_NAME,
   .version = VERSION,
   .preferencesNamespace = PREFERENCES_NAMESPACE,
   .enableOTA = true,
};

TelemetryConfig TELEMETRY_CONFIG = {
   .topic = "unresolved",
};

ViewerSketch sketch(SKETCH_CONFIG, TELEMETRY_CONFIG, SITE_OPTIONS);
Timer displayTimer(DISPLAY_UPDATE_MS);

float values[NUM_BARS] = { NAN, NAN, NAN, NAN, NAN, NAN };
float drawnValues[NUM_BARS] = { -1, -1, -1, -1, -1, -1 };

// Y position of each bar's last-drawn value marker (see drawValueLine()), or UINT16_MAX
// if no marker has been drawn yet; used to erase only the marker's old position instead
// of refilling the whole bar background on every update, which caused visible flicker.
uint16_t lastMarkerY[NUM_BARS] = { UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX };
Rect16 barRects[NUM_BARS] = {};
uint16_t barCenters[NUM_BARS] = {};
uint16_t valueY = 0;
uint16_t labelY = 0;
uint16_t tierY = 0;
bool topicsSet = false;
std::string telemetryTopics[NUM_BARS];
DisplayValue* valueFields[NUM_BARS] = {};
DisplayValue* tierFields[NUM_BARS] = {};

///
/// <summary>
/// Handles telemetry lifecycle events: subscribes to all of the air topics on the
/// connection once started, and resets per-bar tracking state on every (re)connect.
/// </summary>
///
class AirTelemetryHandler : public TelemetryViewerEventHandler
{
public:
   explicit AirTelemetryHandler(IStatus* status) : TelemetryViewerEventHandler(status, &sketch.arduino)
   {
   }

   void onReconnected() override
   {
      for (uint8_t i = 0; i < NUM_BARS; i++)
      {
         drawnValues[i] = -1;
         lastMarkerY[i] = UINT16_MAX;
      }

      if (!topicsSet)
      {
         topicsSet = true;
         std::vector<std::string> topics;
         for (uint8_t i = 0; i < NUM_BARS; i++)
         {
            topics.push_back(telemetryTopics[i]);
         }
         sketch.getClient()->setTopics(topics);
      }
   }
};

AirTelemetryHandler telemetryHandler(&sketch.arduino.status);

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
   uint16_t headerHeight = sketch.arduino.charH(HEADER_TEXT_SIZE) + 4;

   valueY = headerHeight;
   tierY = displayHeight - charH;
   labelY = tierY - charH;

   uint16_t barTop = valueY + charH + 2;
   uint16_t barHeight = labelY - 2 - barTop;
   uint16_t slotWidth = displayWidth / NUM_BARS;
   uint16_t barWidth = slotWidth * 2 / 3;

   Format valueFormat("####");
   Format tierFormat(TIER_CHARS);

   for (uint8_t i = 0; i < NUM_BARS; i++)
   {
      barCenters[i] = slotWidth * i + slotWidth / 2;
      barRects[i] = { (uint16_t)(barCenters[i] - barWidth / 2), barTop, barWidth, barHeight };

      valueFields[i] = new DisplayValue(&sketch.arduino, valueFormat, TEXT_SIZE, DisplayValue::Alignment::CENTER);
      valueFields[i]->setPosition(barCenters[i], valueY);

      tierFields[i] = new DisplayValue(&sketch.arduino, tierFormat, TEXT_SIZE, DisplayValue::Alignment::CENTER);
      tierFields[i]->setPosition(barCenters[i], tierY);
   }
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
   float lowerLimit = 0;

   for (uint8_t tier = 0; tier < NUM_TIERS; tier++)
   {
      float upperLimit = (tier < NUM_TIERS - 1) ? min(TIER_LIMITS[i][tier], BAR_MAXES[i]) : BAR_MAXES[i];

      uint16_t bandTop = rect.y + rect.height - (uint16_t)(upperLimit / BAR_MAXES[i] * rect.height);
      uint16_t bandBottom = rect.y + rect.height - (uint16_t)(lowerLimit / BAR_MAXES[i] * rect.height);

      uint16_t drawTop = max(bandTop, yTop);
      uint16_t drawBottom = min(bandBottom, yBottom);
      if (drawBottom > drawTop)
      {
         sketch.arduino.display.fillRect(rect.x, drawTop, rect.width, drawBottom - drawTop, (uint16_t)AirQualityTiers::TIER_COLORS[tier]);
      }

      lowerLimit = upperLimit;
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

// Thickness of the value marker line, and half-width/height of the diamond drawn at its
// center, in pixels.
constexpr uint16_t VALUE_LINE_THICKNESS = 3;
constexpr uint16_t VALUE_DIAMOND_HALF = 8;

///
/// <summary>
/// Draws a thick white line across a bar at the height corresponding to the current
/// value, with a white diamond centered on the bar, so it can be visually compared
/// against the tier bands and stands out clearly against the dimmed tier colors. Only
/// erases the previous marker's position (rather than refilling the whole bar), to
/// avoid visible flicker on every value update.
/// </summary>
/// <param name="i">Index of the bar to draw.</param>
/// <param name="value">Current value to mark.</param>
///
void drawValueLine(uint8_t i, float value)
{
   const Rect16& rect = barRects[i];
   float clampedValue = constrain(value, 0, BAR_MAXES[i]);
   uint16_t y = rect.y + rect.height - (uint16_t)(clampedValue / BAR_MAXES[i] * rect.height);
   y = min(y, (uint16_t)(rect.y + rect.height - 1));

   constexpr uint16_t MARKER_HALF = max((uint16_t)(VALUE_LINE_THICKNESS / 2), VALUE_DIAMOND_HALF);
   if (lastMarkerY[i] != UINT16_MAX)
   {
      uint16_t eraseTop = (lastMarkerY[i] >= rect.y + MARKER_HALF) ? lastMarkerY[i] - MARKER_HALF : rect.y;
      uint16_t eraseBottom = min((uint16_t)(lastMarkerY[i] + MARKER_HALF + 1), (uint16_t)(rect.y + rect.height));
      drawBarBackgroundRange(i, eraseTop, eraseBottom);
   }
   lastMarkerY[i] = y;

   uint16_t top = (y >= rect.y + VALUE_LINE_THICKNESS / 2) ? y - VALUE_LINE_THICKNESS / 2 : rect.y;
   uint16_t centerX = barCenters[i];

   // Clip the marker line and diamond to the bar's rect so the diamond's points (which
   // can extend above/below the bar) don't draw over the label/tier text above and below.
   sketch.arduino.display.setClipRect(rect.x, rect.y, rect.width, rect.height);
   sketch.arduino.display.fillRect(rect.x, top, rect.width, VALUE_LINE_THICKNESS, (uint16_t)Color::WHITE);
   sketch.arduino.display.fillTriangle(centerX - VALUE_DIAMOND_HALF, y, centerX, y - VALUE_DIAMOND_HALF, centerX + VALUE_DIAMOND_HALF, y, (uint16_t)Color::WHITE);
   sketch.arduino.display.fillTriangle(centerX - VALUE_DIAMOND_HALF, y, centerX, y + VALUE_DIAMOND_HALF, centerX + VALUE_DIAMOND_HALF, y, (uint16_t)Color::WHITE);
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
/// Clears the display and draws the static header and bar labels.
/// </summary>
///
void drawStatic()
{
   sketch.arduino.clearDisplay();
   sketch.arduino.setCursor(0, 0);
   sketch.arduino.setTextSize(HEADER_TEXT_SIZE);
   sketch.arduino.println("Air Quality", Color::HEADING);

   std::string location = sketch.siteLocation();
   uint16_t locationWidth = location.length() * sketch.arduino.charW(TEXT_SIZE);
   sketch.arduino.setTextSize(TEXT_SIZE);
   sketch.arduino.setCursor(sketch.arduino.width() - locationWidth, 0);
   sketch.arduino.println(location, Color::GRAY);

   for (uint8_t i = 0; i < NUM_BARS; i++)
   {
      drawBarBackground(i);
      printCentered(LABELS[i], barCenters[i], labelY, Color::HEADING);
      drawnValues[i] = -1;
      lastMarkerY[i] = UINT16_MAX;
   }
}

void setup()
{
   SerialX::begin();

   sketch.beginBanner();

   initLayout();

   sketch.resolveSiteLocation(true);
   std::string suffix = "/" + sketch.siteLocation();
   for (uint8_t i = 0; i < NUM_BARS; i++)
   {
      telemetryTopics[i] = std::string(TOPIC_ITEMS[i]) + suffix;
   }

   sketch.beginConnect();

   sketch.beginTelemetry(telemetryTopics[0].c_str(), &telemetryHandler);
   sketch.getClient()->onSample([](const std::string& topic, double value, int64_t dtMicros)
   {
      for (uint8_t i = 0; i < NUM_BARS; i++)
      {
         if (topic == telemetryTopics[i])
         {
            values[i] = (float)value;
         }
      }
   });
   sketch.setOnLocateEndCallback([]() { telemetryHandler.needsFullRedraw = true; });

   delay(1000);

   Logger.logInitializationComplete();
}

void loop()
{
   sketch.loop();

   if (sketch.isLocating())
   {
      return;
   }

   if (sketch.getClient()->isStarted() == false)
   {
      return;
   }

   if (telemetryHandler.needsFullRedraw)
   {
      telemetryHandler.needsFullRedraw = false;
      drawStatic();
   }

   if (displayTimer.ready() == false)
   {
      return;
   }

   for (uint8_t i = 0; i < NUM_BARS; i++)
   {
      float value = values[i];
      if (isnan(value) || value == drawnValues[i])
      {
         continue;
      }

      drawnValues[i] = value;

      uint8_t tier = 0;
      while (tier < NUM_TIERS - 1 && value > TIER_LIMITS[i][tier])
      {
         tier++;
      }

      drawValueLine(i, value);

      valueFields[i]->draw((int)lround(value), Color::WHITE);
      tierFields[i]->draw(AirQualityTiers::TIER_NAMES[tier], AirQualityTiers::TIER_COLORS[tier]);
   }
}
