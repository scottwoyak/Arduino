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

#define TEXT_SIZES_CUSTOM
#define TEXT_SIZE_2
#define TEXT_SIZE_3

#include <string>

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Hosyond ESP32-S3 Viewer or Feather ESP32-S3)."
#include "WrongBoard.h"
#endif

#include "Bar.h"
#include "LibraryVersion.h"
#include "SerialX.h"
#include "SiteLocationResolver.h"
#include "Status.h"
#include "TelemetryClient.h"
#include "Timer.h"
#include "WiFiSettings.h"
#include "ViewerSketch.h"

const auto VERSION = MakeVersion("1.0");
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

// Air quality tiers, from best to worst. Names are shortened to fit under a bar.
constexpr uint8_t NUM_TIERS = 6;
constexpr uint8_t TIER_CHARS = 6;
constexpr const char* TIER_NAMES[NUM_TIERS] = { "Good", "Mod", "Sens", "Bad", "V.Bad", "Haz" };

// Upper limit of each tier except the last, per bar. PM1.0 has no standard, so it uses
// the PM2.5 limits. PM values are in ug/m3, CO2 in ppm, VOC/NOx are gas index values.
constexpr float TIER_LIMITS[NUM_BARS][NUM_TIERS - 1] = {
   { 9, 35, 55, 125, 225 },
   { 9, 35, 55, 125, 225 },
   { 54, 154, 254, 354, 424 },
   { 800, 1000, 1500, 2000, 5000 },
   { 150, 250, 350, 400, 450 },
   { 20, 100, 200, 300, 400 },
};

constexpr uint8_t TEXT_SIZE = 2;
constexpr uint8_t HEADER_TEXT_SIZE = 3;
constexpr uint8_t VALUE_CHARS = 4;
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

ViewerSketch sketch(SKETCH_CONFIG, TELEMETRY_CONFIG);
SiteLocationResolver siteLocationResolver(PREFERENCES_NAMESPACE, SITE_OPTIONS);
Timer displayTimer(DISPLAY_UPDATE_MS);

constexpr Color TIER_COLORS[NUM_TIERS] = {
   Color565::fromRGB(0, 200, 0),
   Color565::fromRGB(255, 200, 0),
   Color565::fromRGB(255, 126, 0),
   Color565::fromRGB(255, 0, 0),
   Color565::fromRGB(143, 63, 151),
   Color565::fromRGB(160, 0, 50),
};

float values[NUM_BARS] = { NAN, NAN, NAN, NAN, NAN, NAN };
float drawnValues[NUM_BARS] = { -1, -1, -1, -1, -1, -1 };
VerticalBar* pmBars[NUM_BARS] = {};
uint16_t barCenters[NUM_BARS] = {};
uint16_t valueY = 0;
uint16_t labelY = 0;
uint16_t tierY = 0;
bool needsInitialDisplay = true;
bool topicsSet = false;
std::string telemetryTopics[NUM_BARS];

///
/// <summary>
/// Handles telemetry lifecycle events: subscribes to all of the air topics on the
/// connection once started, and flags the display for a full redraw.
/// </summary>
///
class AirTelemetryHandler : public TelemetryEventHandler
{
public:
   explicit AirTelemetryHandler(IStatus* status) : TelemetryEventHandler(status, &sketch.arduino)
   {
   }

   void onStarted() override
   {
      TelemetryEventHandler::onStarted();
      needsInitialDisplay = true;

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

   for (uint8_t i = 0; i < NUM_BARS; i++)
   {
      barCenters[i] = slotWidth * i + slotWidth / 2;
      Rect16 rect = { (uint16_t)(barCenters[i] - barWidth / 2), barTop, barWidth, barHeight };
      pmBars[i] = new VerticalBar(rect, RangeF(0, BAR_MAXES[i]), TIER_COLORS[0], Color::BLACK);
   }
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

   std::string location = std::string(siteLocationResolver.site().c_str()) + "/" + std::string(siteLocationResolver.location().c_str());
   uint16_t locationWidth = location.length() * sketch.arduino.charW(TEXT_SIZE);
   sketch.arduino.setTextSize(TEXT_SIZE);
   sketch.arduino.setCursor(sketch.arduino.width() - locationWidth, 0);
   sketch.arduino.println(location, Color::GRAY);

   for (uint8_t i = 0; i < NUM_BARS; i++)
   {
      printCentered(LABELS[i], barCenters[i], labelY, Color::HEADING);
      pmBars[i]->reset();
      drawnValues[i] = -1;
   }
}

void setup()
{
   SerialX::begin();

   sketch.beginBanner();

   initLayout();

   siteLocationResolver.resolve(sketch.arduino.preferences, true);
   std::string suffix = "/" + std::string(siteLocationResolver.site().c_str()) + "/" + std::string(siteLocationResolver.location().c_str());
   for (uint8_t i = 0; i < NUM_BARS; i++)
   {
      telemetryTopics[i] = std::string(TOPIC_ITEMS[i]) + suffix;
   }
   Serial.print("Location... ");
   Serial.println(suffix.c_str());

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
   sketch.setOnLocateEndCallback([]() { needsInitialDisplay = true; });

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

   if (needsInitialDisplay)
   {
      needsInitialDisplay = false;
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

      pmBars[i]->setColor(TIER_COLORS[tier]);
      pmBars[i]->set(value);
      pmBars[i]->draw(&sketch.arduino.display);

      std::string text = std::to_string((int)lround(value));
      while (text.length() < VALUE_CHARS)
      {
         text += " ";
      }
      printCentered(text, barCenters[i], valueY, Color::VALUE);

      std::string tierName = TIER_NAMES[tier];
      while (tierName.length() < TIER_CHARS)
      {
         tierName = " " + tierName + " ";
      }
      printCentered(tierName, barCenters[i], tierY, TIER_COLORS[tier]);
   }
}
