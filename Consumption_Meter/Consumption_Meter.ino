//
// Consumption Meter
//
// Measures AC current and apparent power simultaneously across 8 split-core current
// transformer (CT) sensors using an Analog Devices AD7606 8-channel, 16-bit bipolar
// simultaneous-sampling ADC in 4-wire serial (SPI/DOUTA) mode. Displays live power (Watts)
// and RMS current (Amps) on the Hosyond 4.0" ST7796S touch display and publishes per-channel
// power readings over WebSocket telemetry to Power/<location>.
//
// Hardware & AD7606 Serial Mode Wiring (using the 4 external Hosyond GPIOs: 2, 3, 14, 21):
// - Set the AD7606 breakout board mode jumper to SER (Serial / SPI mode):
//     * PAR/SER (PS) jumper -> SER (HIGH / 3.3V)
//     * RANGE jumper -> +/-5V (LOW / GND)
//     * OS0, OS1, OS2 -> GND (no oversampling, fastest 4 us conversion)
// - Power connections:
//     * AVCC (5V) -> 5V rail from the Hosyond board
//     * VDRIVE (VIO) -> 3.3V rail from the Hosyond board (sets 3.3V logic levels)
//     * GND / AGND -> GND
// - 4-Pin Control & Serial Data connections (same 4 GPIOs used in PowerMulti_Publisher):
//     * CONVSTA + CONVSTB (tied together) -> GPIO2
//     * RD / SCLK -> GPIO3
//     * DB7 / DOUTA (MISO) -> GPIO14
//     * CS -> GPIO21
//     * RST -> Tie to GND (0V)
//     * BUSY -> Leave unconnected (AD7606 converts in <4 us; driver waits 5 us after CONVST)
//     * DB15 -> Tie to GND (selects normal serial output instead of byte-parallel)
// - CT Input connections (V1..V8 and AGND; NO 1.65V bias divider needed!):
//     * Connect each CT's S1 (signal) wire to V1..V8
//     * Connect each CT's S2 (return) wire directly to AGND (GND)
//     * For current-output CTs (e.g. 100A:33.3mA w/ 30 ohm, 150A:50mA w/ 20 ohm, or
//       500A:50mA w/ 47 ohm), connect the burden resistor directly across Vx and AGND.
//     * Optional unplugged-CT detection: connect a 2.2M ohm resistor from each Vx to 3.3V
//       (pulls open inputs to ~+1.0V DC against AD7606's internal 1M ohm to AGND).
//

#include <Arduino.h>
#include <Preferences.h>
#include <array>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

#define SOUNDS_CUSTOM
#define TEXT_SIZE_2
#define TEXT_SIZE_3
#define TEXT_SIZE_4
#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Hosyond ESP32-S3 4.0 Touch LCD)."
#include "WrongBoard.h"
#endif

#include "AD7606PowerSensor.h"
#include "Format.h"
#include "LibraryVersion.h"
#include "PublisherSketch.h"
#include "Timer.h"
#include "WiFiSettings.h"

const auto VERSION = MakeVersion("1.0");
constexpr auto SKETCH_NAME = "Consumption_Meter";
constexpr auto PREF_NAMESPACE = "ConsMeter";

// ----------- AD7606 8-Channel Configuration (GPIO2, GPIO3, GPIO14, GPIO21)
constexpr size_t NUM_CT_INPUTS = AD7606PowerSensor::MAX_CHANNELS;
constexpr size_t MAX_LOCATION_LENGTH = 32;

constexpr uint8_t PIN_AD7606_CONVST = 2;
constexpr uint8_t PIN_AD7606_SCLK = 3;
constexpr uint8_t PIN_AD7606_MISO = 14;
constexpr uint8_t PIN_AD7606_CS = 21;
constexpr int8_t PIN_AD7606_RESET = -1;
constexpr int8_t PIN_AD7606_BUSY = -1;

constexpr std::array<const char*, NUM_CT_INPUTS> DEFAULT_CT_LOCATIONS = {
   "1", "", "", "", "", "", "", "",
};

constexpr std::array<float, NUM_CT_INPUTS> DEFAULT_CT_AMPS = {
   AD7606PowerSensor::CORE_150A, AD7606PowerSensor::CORE_150A, AD7606PowerSensor::CORE_100A, AD7606PowerSensor::CORE_100A,
   AD7606PowerSensor::CORE_30A,  AD7606PowerSensor::CORE_30A,  AD7606PowerSensor::CORE_30A,  AD7606PowerSensor::CORE_30A,
};

constexpr std::array<const char*, NUM_CT_INPUTS> PREF_KEY_CT_LOCATION = {
   "loc1", "loc2", "loc3", "loc4", "loc5", "loc6", "loc7", "loc8",
};

constexpr std::array<const char*, NUM_CT_INPUTS> PREF_KEY_CT_AMPS = {
   "amps1", "amps2", "amps3", "amps4", "amps5", "amps6", "amps7", "amps8",
};

constexpr uint16_t DISPLAY_UPDATE_INTERVAL_MS = 200; // 5 updates per second

// ----------- Telemetry Configuration
constexpr auto POWER_TOPIC_PREFIX = "Power";
constexpr size_t MAX_TOPIC_LENGTH = 64;
constexpr uint8_t POWER_DECIMALS = 0;
constexpr uint16_t TELEMETRY_PUBLISH_INTERVAL_MS = DISPLAY_UPDATE_INTERVAL_MS;

// ----------- Display Layout
constexpr uint8_t HEADER_SIZE = DEFAULT_HEADING_SIZE;
constexpr uint8_t TABLE_TEXT_SIZE = 2;
constexpr uint8_t FOOTER_SIZE = DEFAULT_FOOTER_SIZE;
constexpr size_t LOCATION_COL_WIDTH = 16;

std::array<char[MAX_LOCATION_LENGTH], NUM_CT_INPUTS> ctLocations = {};
std::array<char[8], NUM_CT_INPUTS> defaultChannelNames = {
   "1", "2", "3", "4", "5", "6", "7", "8",
};
std::array<float, NUM_CT_INPUTS> ctAmps = DEFAULT_CT_AMPS;
std::array<char[MAX_TOPIC_LENGTH], NUM_CT_INPUTS> channelTopics = {};
char primaryTelemetryTopic[MAX_TOPIC_LENGTH] = "Power/1";
size_t primaryTelemetryChannel = 0;
std::array<AD7606PowerSensor::ChannelConfig, NUM_CT_INPUTS> initialChannelConfig = {
   AD7606PowerSensor::ChannelConfig{ "1", AD7606PowerSensor::CORE_150A },
   AD7606PowerSensor::ChannelConfig{ "2", AD7606PowerSensor::CORE_150A },
   AD7606PowerSensor::ChannelConfig{ "3", AD7606PowerSensor::CORE_100A },
   AD7606PowerSensor::ChannelConfig{ "4", AD7606PowerSensor::CORE_100A },
   AD7606PowerSensor::ChannelConfig{ "5", AD7606PowerSensor::CORE_30A },
   AD7606PowerSensor::ChannelConfig{ "6", AD7606PowerSensor::CORE_30A },
   AD7606PowerSensor::ChannelConfig{ "7", AD7606PowerSensor::CORE_30A },
   AD7606PowerSensor::ChannelConfig{ "8", AD7606PowerSensor::CORE_30A },
};
AD7606PowerSensor powerSensor(
   PIN_AD7606_CONVST,
   PIN_AD7606_SCLK,
   PIN_AD7606_MISO,
   PIN_AD7606_CS,
   initialChannelConfig.data(),
   NUM_CT_INPUTS,
   AD7606PowerSensor::DEFAULT_LINE_VOLTAGE_V,
   AD7606::Range::BIPOLAR_5V,
   PIN_AD7606_BUSY,
   PIN_AD7606_RESET);

size_t interactivePromptIndex = 0;
void sendNextLocationPrompt();
void sendNextAmpsPrompt();
void sendSingleChannelLocationPrompt();
void sendSingleChannelAmpsPrompt();

// Uses promptForContext with bucket = nullptr so PublisherSketch skips InfluxDB upload.
InfluxConfig INFLUX_CONFIG = {
   .context = { nullptr, "Bragg", nullptr },
   .promptForContext = true,
};

TelemetryConfig TELEMETRY_CONFIG = {
   .topic = primaryTelemetryTopic,
   .decimals = POWER_DECIMALS,
   .publishIntervalMs = TELEMETRY_PUBLISH_INTERVAL_MS,
};

SketchConfig PUBLISHER_CONFIG = {
   .sketchName = SKETCH_NAME,
   .version = VERSION,
   .preferencesNamespace = PREF_NAMESPACE,
   .cpuFrequencyMhz = 80,
   .enableOTA = true,
};

PublisherSketch sketch(PUBLISHER_CONFIG, INFLUX_CONFIG, TELEMETRY_CONFIG);

///
/// <summary>
/// Rebuilds the per-channel telemetry topics as "Power/<channelLocation>" for each configured CT
/// and updates the active TelemetryPublisher topic immediately if already running.
/// </summary>
///
void refreshTelemetryTopics()
{
   bool foundPrimary = false;
   for (size_t i = 0; i < NUM_CT_INPUTS; i++)
   {
      if (ctLocations[i][0] != '\0')
      {
         std::string fullTopic = std::string(POWER_TOPIC_PREFIX) + "/" + ctLocations[i];
         strlcpy(channelTopics[i], fullTopic.c_str(), sizeof(channelTopics[i]));
         if (!foundPrimary)
         {
            primaryTelemetryChannel = i;
            strlcpy(primaryTelemetryTopic, fullTopic.c_str(), sizeof(primaryTelemetryTopic));
            foundPrimary = true;
         }
      }
      else
      {
         channelTopics[i][0] = '\0';
      }
   }

   if (!foundPrimary)
   {
      primaryTelemetryChannel = 0;
      std::string fallbackTopic = std::string(POWER_TOPIC_PREFIX) + "/1";
      strlcpy(primaryTelemetryTopic, fallbackTopic.c_str(), sizeof(primaryTelemetryTopic));
   }

   if (sketch.client() != nullptr)
   {
      sketch.client()->setTopic(primaryTelemetryTopic);
   }
}

///
/// <summary>
/// Updates all 8 AD7606 channels (V1..V8) on powerSensor so every channel is
/// continuously sampled for connection status, Amps, and Watts regardless of whether a
/// location string is assigned.
/// </summary>
///
void applyChannelConfigToSensor()
{
   for (size_t i = 0; i < NUM_CT_INPUTS; i++)
   {
      const char* name = ctLocations[i][0] != '\0' ? ctLocations[i] : defaultChannelNames[i];
      powerSensor.setChannelName(i, name);
      powerSensor.setCoreRating(ctAmps[i], i);
   }

   powerSensor.setChannelCount(NUM_CT_INPUTS);
   refreshTelemetryTopics();
}

///
/// <summary>
/// Loads persisted per-input locations and CT current ratings from Preferences and
/// configures the enabled channels on powerSensor.
/// </summary>
///
void loadChannelConfig()
{
   Preferences prefs;
   prefs.begin(PREF_NAMESPACE, true);

   for (size_t i = 0; i < NUM_CT_INPUTS; i++)
   {
      String savedLoc = prefs.getString(PREF_KEY_CT_LOCATION[i], DEFAULT_CT_LOCATIONS[i]);
      strlcpy(ctLocations[i], savedLoc.c_str(), sizeof(ctLocations[i]));

      float savedAmps = prefs.getFloat(PREF_KEY_CT_AMPS[i], DEFAULT_CT_AMPS[i]);
      ctAmps[i] = savedAmps > 0.0f ? savedAmps : DEFAULT_CT_AMPS[i];
   }

   prefs.end();
   applyChannelConfigToSensor();
}

///
/// <summary>
/// Persists a single CT input's location string (empty string disables telemetry for that channel).
/// </summary>
/// <param name="inputIndex">Channel index (0 to 7).</param>
/// <param name="loc">Location string (or empty to disable).</param>
///
void saveCtLocation(size_t inputIndex, const char* loc)
{
   Preferences prefs;
   prefs.begin(PREF_NAMESPACE, false);
   prefs.putString(PREF_KEY_CT_LOCATION[inputIndex], loc);
   prefs.end();

   strlcpy(ctLocations[inputIndex], loc, sizeof(ctLocations[inputIndex]));
   applyChannelConfigToSensor();
}

///
/// <summary>
/// Persists a single CT input's current rating (Amps per 1.0V RMS) in Preferences.
/// </summary>
/// <param name="inputIndex">Channel index (0 to 7).</param>
/// <param name="amps">Rated current in Amps (e.g. 30, 50, 100, 150, 200, 213, 500).</param>
///
void saveCtAmps(size_t inputIndex, float amps)
{
   Preferences prefs;
   prefs.begin(PREF_NAMESPACE, false);
   prefs.putFloat(PREF_KEY_CT_AMPS[inputIndex], amps);
   prefs.end();

   ctAmps[inputIndex] = amps;
   applyChannelConfigToSensor();
}

///
/// <summary>
/// Returns true if the user input string requests clearing a channel's location.
/// </summary>
/// <param name="text">Trimmed user input string.</param>
/// <returns>True if the location should be cleared.</returns>
///
bool isClearLocationValue(const char* text)
{
   return strcmp(text, "-") == 0 ||
          strcasecmp(text, "none") == 0 ||
          strcasecmp(text, "off") == 0 ||
          strcasecmp(text, "unused") == 0;
}

///
/// <summary>
/// Handles the shell answer for a single-channel location prompt ("sc <1-8>").
/// Pressing Return (empty input) keeps the previous location; "-", "none", or "off" clears it.
/// </summary>
/// <param name="line">User-entered location line from the shell.</param>
///
void onSingleChannelLocationAnswer(const char* line)
{
   String input = String(line);
   input.trim();

   if (input.length() > 0)
   {
      const char* newLoc = isClearLocationValue(input.c_str()) ? "" : input.c_str();
      saveCtLocation(interactivePromptIndex, newLoc);
   }

   sendSingleChannelAmpsPrompt();
}

///
/// <summary>
/// Sends the interactive shell prompt for a single channel's location ("sc <1-8>").
/// </summary>
///
void sendSingleChannelLocationPrompt()
{
   String currentLoc = ctLocations[interactivePromptIndex][0] != '\0' ? String(ctLocations[interactivePromptIndex]) : String("unused");
   String label = "Enter l" + String(interactivePromptIndex + 1) + " [" + currentLoc + "]: ";
   DeviceServerClient::shell().print(label);
   DeviceServerClient::shell().flushOutput();
   DeviceServerClient::promptShellLine(onSingleChannelLocationAnswer);
}

///
/// <summary>
/// Handles the shell answer for a single-channel CT amp rating prompt ("sc <1-8>").
/// Pressing Return (empty input) keeps the previous amp rating.
/// </summary>
/// <param name="line">User-entered amp rating line from the shell.</param>
///
void onSingleChannelAmpsAnswer(const char* line)
{
   String input = String(line);
   input.trim();

   float rating = ctAmps[interactivePromptIndex];
   if (input.length() > 0)
   {
      float parsed = input.toFloat();
      if (parsed <= 0.0f)
      {
         DeviceServerClient::shell().println("Invalid amps rating (must be > 0).");
         sendSingleChannelAmpsPrompt();
         return;
      }
      rating = parsed;
      saveCtAmps(interactivePromptIndex, rating);
   }

   std::string locSummary = ctLocations[interactivePromptIndex][0] != '\0' ? ctLocations[interactivePromptIndex] : "(unused)";
   std::string summary =
      "l" + std::to_string(interactivePromptIndex + 1) + "=" + locSummary +
      ", a" + std::to_string(interactivePromptIndex + 1) + "=" + std::to_string(static_cast<int>(rating + 0.5f));
   DeviceServerClient::respond(summary.c_str());
}

///
/// <summary>
/// Sends the interactive shell prompt for a single channel's CT amp rating ("sc <1-8>").
/// </summary>
///
void sendSingleChannelAmpsPrompt()
{
   int currentAmps = static_cast<int>(ctAmps[interactivePromptIndex] + 0.5f);
   String label = "Enter a" + String(interactivePromptIndex + 1);
   if (ctLocations[interactivePromptIndex][0] != '\0')
   {
      label += " (" + String(ctLocations[interactivePromptIndex]) + ")";
   }
   label += " [" + String(currentAmps) + "]: ";
   DeviceServerClient::shell().print(label);
   DeviceServerClient::shell().flushOutput();
   DeviceServerClient::promptShellLine(onSingleChannelAmpsAnswer);
}

///
/// <summary>
/// Handles the shell answer for one of the 8 location prompts during interactive SetConfig.
/// Pressing Return (empty input) keeps that CT input's current location; "-", "none", or "off" clears it.
/// </summary>
/// <param name="line">User-entered location line from the shell.</param>
///
void onInteractiveLocationItemAnswer(const char* line)
{
   String input = String(line);
   input.trim();

   if (input.length() > 0)
   {
      const char* newLoc = isClearLocationValue(input.c_str()) ? "" : input.c_str();
      saveCtLocation(interactivePromptIndex, newLoc);
   }

   sketch.addInteractiveSummaryLine(
      "l" + std::to_string(interactivePromptIndex + 1),
      ctLocations[interactivePromptIndex][0] != '\0' ? ctLocations[interactivePromptIndex] : "(unused)");

   interactivePromptIndex++;
   if (interactivePromptIndex < NUM_CT_INPUTS)
   {
      sendNextLocationPrompt();
   }
   else
   {
      interactivePromptIndex = 0;
      sendNextAmpsPrompt();
   }
}

///
/// <summary>
/// Sends the interactive shell prompt for CT location 1..8.
/// </summary>
///
void sendNextLocationPrompt()
{
   String currentLoc = ctLocations[interactivePromptIndex][0] != '\0' ? String(ctLocations[interactivePromptIndex]) : String("unused");
   String label = "Enter l" + String(interactivePromptIndex + 1) + " [" + currentLoc + "]: ";
   DeviceServerClient::shell().print(label);
   DeviceServerClient::shell().flushOutput();
   DeviceServerClient::promptShellLine(onInteractiveLocationItemAnswer);
}

///
/// <summary>
/// Handles the shell answer for one of the 8 CT amp rating prompts during interactive SetConfig.
/// Pressing Return (empty input) keeps that CT input's current amp rating.
/// </summary>
/// <param name="line">User-entered amp rating line from the shell.</param>
///
void onInteractiveAmpsItemAnswer(const char* line)
{
   String input = String(line);
   input.trim();

   float rating = ctAmps[interactivePromptIndex];
   if (input.length() > 0)
   {
      float parsed = input.toFloat();
      if (parsed <= 0.0f)
      {
         DeviceServerClient::shell().println("Invalid amps rating (must be > 0).");
         sendNextAmpsPrompt();
         return;
      }
      rating = parsed;
   }

   saveCtAmps(interactivePromptIndex, rating);
   sketch.addInteractiveSummaryLine(
      "a" + std::to_string(interactivePromptIndex + 1),
      std::to_string(static_cast<int>(rating + 0.5f)));

   interactivePromptIndex++;
   if (interactivePromptIndex < NUM_CT_INPUTS)
   {
      sendNextAmpsPrompt();
   }
   else
   {
      sketch.finishInteractiveConfig();
   }
}

///
/// <summary>
/// Sends the interactive shell prompt for CT amp rating 1..8 (skipping unused inputs).
/// </summary>
///
void sendNextAmpsPrompt()
{
   while (interactivePromptIndex < NUM_CT_INPUTS && ctLocations[interactivePromptIndex][0] == '\0')
   {
      interactivePromptIndex++;
   }

   if (interactivePromptIndex >= NUM_CT_INPUTS)
   {
      sketch.finishInteractiveConfig();
      return;
   }

   int currentAmps = static_cast<int>(ctAmps[interactivePromptIndex] + 0.5f);
   String label = "Enter a" + String(interactivePromptIndex + 1) + " (" + String(ctLocations[interactivePromptIndex]) + ") [" + String(currentAmps) + "]: ";
   DeviceServerClient::shell().print(label);
   DeviceServerClient::shell().flushOutput();
   DeviceServerClient::promptShellLine(onInteractiveAmpsItemAnswer);
}

///
/// <summary>
/// Starts the 8-item location and amps prompt sequence during interactive SetConfig.
/// </summary>
///
void startInteractiveChannelPrompts()
{
   interactivePromptIndex = 0;
   sendNextLocationPrompt();
}

///
/// <summary>
/// Returns extra "key=value" configuration lines for the GetConfig command reporting
/// the 8 CT locations (l1..l8) and 8 CT amp ratings (a1..a8).
/// </summary>
/// <returns>Multi-line key=value string for GetConfig.</returns>
///
std::string getExtraConfigText()
{
   std::ostringstream out;
   for (size_t i = 0; i < NUM_CT_INPUTS; i++)
   {
      if (i > 0)
      {
         out << "\n";
      }
      out << "l" << (i + 1) << "=" << ctLocations[i]
          << "\na" << (i + 1) << "=" << static_cast<int>(ctAmps[i] + 0.5f);
   }
   return out.str();
}

///
/// <summary>
/// Handles extra SetConfig keys (applied immediately without requiring a restart):
/// - Channel number "1".."8" with no value: interactively prompts for that channel's location and amps
/// - Direct keys "l1".."l8" (or "location1".."location8"; use "-", "none", or "off" to clear)
/// - Direct keys "a1".."a8" (or "amps1".."amps8")
/// </summary>
/// <param name="key">Config key passed to SetConfig.</param>
/// <param name="value">New value string.</param>
/// <param name="result">Receives the confirmation or error message.</param>
/// <returns>True if the key was recognized; false otherwise.</returns>
///
bool trySetExtraConfig(const std::string& key, const std::string& value, std::string& result)
{
   char* endPtr = nullptr;
   long channelNum = std::strtol(key.c_str(), &endPtr, 10);
   if (endPtr != key.c_str() && *endPtr == '\0')
   {
      if (channelNum < 1 || channelNum > static_cast<long>(NUM_CT_INPUTS))
      {
         result = "Error: channel must be between 1 and " + std::to_string(NUM_CT_INPUTS);
         return true;
      }

      if (!DeviceServerClient::isShellCommand())
      {
         result = "Error: interactive channel prompt requires the shell";
         return true;
      }

      interactivePromptIndex = static_cast<size_t>(channelNum - 1);
      sendSingleChannelLocationPrompt();
      result = "";
      return true;
   }

   for (size_t i = 0; i < NUM_CT_INPUTS; i++)
   {
      std::string shortLocKey = "l" + std::to_string(i + 1);
      std::string longLocKey = "location" + std::to_string(i + 1);
      if (strcasecmp(key.c_str(), shortLocKey.c_str()) == 0 || strcasecmp(key.c_str(), longLocKey.c_str()) == 0)
      {
         if (value.empty())
         {
            result = "Error: value must not be empty (use '-' to clear)";
            return true;
         }

         bool clear = isClearLocationValue(value.c_str());
         const char* newLoc = clear ? "" : value.c_str();
         saveCtLocation(i, newLoc);
         result = shortLocKey + "=" + (clear ? "(unused)" : value);
         return true;
      }

      std::string shortAmpsKey = "a" + std::to_string(i + 1);
      std::string longAmpsKey = "amps" + std::to_string(i + 1);
      if (strcasecmp(key.c_str(), shortAmpsKey.c_str()) == 0 || strcasecmp(key.c_str(), longAmpsKey.c_str()) == 0)
      {
         if (value.empty())
         {
            result = "Error: value must not be empty";
            return true;
         }

         float rating = std::strtof(value.c_str(), nullptr);
         if (rating <= 0.0f)
         {
            result = "Error: " + shortAmpsKey + " must be a positive current rating (e.g. 30, 50, 100, 150, 200, 213, 500)";
            return true;
         }

         saveCtAmps(i, rating);
         result = shortAmpsKey + "=" + std::to_string(static_cast<int>(rating + 0.5f));
         return true;
      }
   }

   return false;
}

///
/// <summary>
/// Builds the per-channel telemetry topics as "Power/<channelLocation>" for each configured CT.
/// </summary>
/// <param name="site">The resolved site/location context.</param>
///
void onSiteResolved(const InfluxContext& site)
{
   (void)site;
   refreshTelemetryTopics();
}

Format rowIndexFormat("## ", Format::Alignment::RIGHT);
Format rowLocationFormat(LOCATION_COL_WIDTH, Format::Alignment::LEFT);
Format rowAmpsFormat(" ###.# A", Format::Alignment::RIGHT);
Format rowWattsFormat(" ##### W", Format::Alignment::RIGHT);

Timer updateTimer(DISPLAY_UPDATE_INTERVAL_MS);
int16_t valueStartY = 0;
bool displayReady = false;

#ifdef ARDUINO_TOUCH_SUPPORTED
// ----------- Gear/close icon & Touch Settings UI
constexpr int16_t GEAR_RADIUS = 16;
constexpr int16_t GEAR_MARGIN = 6;
constexpr int16_t GEAR_TAP_SIZE = 56;
constexpr std::array<float, 9> AMP_RATING_OPTIONS = {
   AD7606PowerSensor::CORE_15A,
   AD7606PowerSensor::CORE_20A,
   AD7606PowerSensor::CORE_30A,
   AD7606PowerSensor::CORE_50A,
   AD7606PowerSensor::CORE_100A,
   AD7606PowerSensor::CORE_150A,
   AD7606PowerSensor::CORE_200A,
   AD7606PowerSensor::CORE_213A,
   AD7606PowerSensor::CORE_500A,
};

bool showingSettings = false;
bool wasTouching = false;

///
/// <summary>
/// Draws the gear icon in the upper right corner of the main display.
/// </summary>
///
void drawGear()
{
   const int16_t cx = sketch.arduino.width() - GEAR_MARGIN - GEAR_RADIUS;
   const int16_t cy = GEAR_MARGIN + GEAR_RADIUS;
   const uint16_t color = static_cast<uint16_t>(Color::GRAY);

   constexpr uint8_t NUM_TEETH = 8;
   for (uint8_t i = 0; i < NUM_TEETH; i++)
   {
      const float angle = i * 2.0f * static_cast<float>(M_PI) / NUM_TEETH;
      sketch.arduino.display.fillCircle(
         cx + static_cast<int16_t>(std::lround(GEAR_RADIUS * 0.8f * std::cos(angle))),
         cy + static_cast<int16_t>(std::lround(GEAR_RADIUS * 0.8f * std::sin(angle))),
         GEAR_RADIUS / 5,
         color);
   }

   sketch.arduino.display.fillCircle(cx, cy, GEAR_RADIUS * 7 / 10, color);
   sketch.arduino.display.fillCircle(cx, cy, GEAR_RADIUS * 3 / 10, static_cast<uint16_t>(Color::BLACK));
}

///
/// <summary>
/// Draws the close icon (an X) in the upper right corner of the settings view.
/// </summary>
///
void drawCloseIcon()
{
   const int16_t cx = sketch.arduino.width() - GEAR_MARGIN - GEAR_RADIUS;
   const int16_t cy = GEAR_MARGIN + GEAR_RADIUS;
   const int16_t d = GEAR_RADIUS * 7 / 10;

   for (int16_t offset = -1; offset <= 1; offset++)
   {
      sketch.arduino.display.drawLine(cx - d + offset, cy - d, cx + d + offset, cy + d, static_cast<uint16_t>(Color::GRAY));
      sketch.arduino.display.drawLine(cx - d + offset, cy + d, cx + d + offset, cy - d, static_cast<uint16_t>(Color::GRAY));
   }
}

///
/// <summary>
/// Determines whether a touch is within the upper-right gear/close icon tap target.
/// </summary>
/// <param name="x">Touch x coordinate.</param>
/// <param name="y">Touch y coordinate.</param>
/// <returns>True if the touch is within the gear/close tap area.</returns>
///
bool onGear(int16_t x, int16_t y)
{
   return x >= sketch.arduino.width() - GEAR_TAP_SIZE && y < GEAR_TAP_SIZE;
}

///
/// <summary>
/// Draws the static frame (heading, gear icon, and version footer) for the main power view.
/// </summary>
///
void drawMainFrame()
{
   sketch.arduino.clearDisplay();
   sketch.arduino.setTextSize(HEADER_SIZE);
   sketch.arduino.setCursor(0, 0);
   sketch.arduino.println("Power", Color::HEADING);
   drawGear();

   int16_t footerHeight = sketch.arduino.charH(FOOTER_SIZE);
   sketch.arduino.setTextSize(FOOTER_SIZE);
   sketch.arduino.setCursor(0, sketch.arduino.height() - footerHeight);
   sketch.arduino.printR(VERSION, Color::LIGHTGRAY);
}

///
/// <summary>
/// Draws the 8-row touch configuration view showing each channel's index, location, and
/// CT amp rating. Tapping a row's location side edits the location; tapping the right side
/// selects the CT amp rating; tapping the top-right X returns to the live power table.
/// </summary>
///
void drawSettingsView()
{
   sketch.arduino.clearDisplay();
   sketch.arduino.setTextSize(HEADER_SIZE);
   sketch.arduino.setCursor(0, 0);
   sketch.arduino.println("Configure", Color::HEADING);
   drawCloseIcon();

   Format cfgAmpsFormat(" #### A", Format::Alignment::RIGHT);
   sketch.arduino.setCursorY(valueStartY);
   sketch.arduino.setTextSize(TABLE_TEXT_SIZE);

   for (size_t i = 0; i < NUM_CT_INPUTS; i++)
   {
      bool hasLoc = ctLocations[i][0] != '\0';
      const char* locText = hasLoc ? ctLocations[i] : "(tap to set)";
      Color locColor = hasLoc ? Color::LABEL : Color::GRAY;

      sketch.arduino.print(static_cast<int>(i + 1), rowIndexFormat, Color::SUB_LABEL);
      sketch.arduino.print(locText, rowLocationFormat, locColor);
      sketch.arduino.printlnR(static_cast<int>(ctAmps[i] + 0.5f), cfgAmpsFormat, Color::VALUE2);
   }

   int16_t footerHeight = sketch.arduino.charH(FOOTER_SIZE);
   sketch.arduino.setTextSize(FOOTER_SIZE);
   sketch.arduino.setCursor(0, sketch.arduino.height() - footerHeight);
   sketch.arduino.print("Tap Loc or Amps to edit", Color::GRAY);
}

///
/// <summary>
/// Opens the on-screen touch prompt to edit the location (including Clear) for a channel.
/// </summary>
/// <param name="ch">Channel index (0 to 7).</param>
///
void editChannelLocationTouch(size_t ch)
{
   if (ctLocations[ch][0] != '\0')
   {
      String menuTitle = "Ch " + String(ch + 1) + ": " + String(ctLocations[ch]);
      std::vector<String> actions = { "Edit Name", "Clear Location", "Cancel" };
      size_t choice = sketch.arduino.promptTouchOption(menuTitle.c_str(), actions, 0);
      if (choice == 1)
      {
         saveCtLocation(ch, "");
         return;
      }
      if (choice == 2)
      {
         return;
      }
   }

   String kbTitle = "Location " + String(ch + 1) + " (- to clear)";
   String entered = sketch.arduino.promptTouchText(kbTitle.c_str(), String(ctLocations[ch]));
   entered.trim();
   bool clear = (entered == "-" || entered.equalsIgnoreCase("none") || entered.equalsIgnoreCase("off"));
   saveCtLocation(ch, clear ? "" : entered.c_str());
}

///
/// <summary>
/// Opens the on-screen touch option list to select the CT amp rating for a channel.
/// </summary>
/// <param name="ch">Channel index (0 to 7).</param>
///
void editChannelAmpsTouch(size_t ch)
{
   std::vector<String> options;
   size_t defaultIdx = 2; // 30A default
   for (size_t i = 0; i < AMP_RATING_OPTIONS.size(); i++)
   {
      int ratingInt = static_cast<int>(AMP_RATING_OPTIONS[i] + 0.5f);
      options.push_back(String(ratingInt) + " A");
      if (std::fabs(ctAmps[ch] - AMP_RATING_OPTIONS[i]) < 0.5f)
      {
         defaultIdx = i;
      }
   }

   String title = "Amps Ch " + String(ch + 1);
   if (ctLocations[ch][0] != '\0')
   {
      title += " (" + String(ctLocations[ch]) + ")";
   }

   size_t chosen = sketch.arduino.promptTouchOption(title.c_str(), options, defaultIdx);
   if (chosen < AMP_RATING_OPTIONS.size())
   {
      saveCtAmps(ch, AMP_RATING_OPTIONS[chosen]);
   }
}
#endif

///
/// <summary>
/// Renders a 4-column table of all 8 AD7606 inputs on the display:
/// Column 1: Channel number (1-8)
/// Column 2: Configured location name
/// Column 3: RMS current in Amps (or gray dashes if unplugged)
/// Column 4: Apparent power in Watts (or gray dashes if unplugged)
/// </summary>
///
void updateDisplay()
{
   if (!displayReady)
   {
      return;
   }

#ifdef ARDUINO_TOUCH_SUPPORTED
   if (showingSettings)
   {
      return;
   }
#endif

   sketch.arduino.setCursorY(valueStartY);
   sketch.arduino.setTextSize(TABLE_TEXT_SIZE);

   for (size_t i = 0; i < NUM_CT_INPUTS; i++)
   {
      bool connected = powerSensor.isConnected(i);

      sketch.arduino.print(static_cast<int>(i + 1), rowIndexFormat, Color::SUB_LABEL);
      sketch.arduino.print(ctLocations[i], rowLocationFormat, Color::LABEL);

      if (connected)
      {
         sketch.arduino.print(powerSensor.amps(i), rowAmpsFormat, Color::VALUE2);
         sketch.arduino.printlnR(powerSensor.watts(i), rowWattsFormat, Color::VALUE);
      }
      else
      {
         sketch.arduino.print(rowAmpsFormat.toNoValueString().c_str(), Color::GRAY);
         sketch.arduino.printlnR(rowWattsFormat.toNoValueString().c_str(), Color::GRAY);
      }
   }
}

#ifdef ARDUINO_TOUCH_SUPPORTED
///
/// <summary>
/// Polls capacitive touch input to open/close the gear settings view and edit any
/// channel's location or CT amp rating on-screen.
/// </summary>
///
void handleTouch()
{
   if (!displayReady)
   {
      return;
   }

   lgfx::touch_point_t tp;
   bool touched = sketch.arduino.display.getTouch(&tp) > 0;
   bool tapped = touched && !wasTouching;
   wasTouching = touched;

   if (!tapped)
   {
      return;
   }

   if (!showingSettings)
   {
      if (onGear(tp.x, tp.y))
      {
         showingSettings = true;
         drawSettingsView();
      }
      return;
   }

   if (onGear(tp.x, tp.y))
   {
      showingSettings = false;
      drawMainFrame();
      updateDisplay();
      return;
   }

   int16_t rowH = sketch.arduino.charH(TABLE_TEXT_SIZE);
   if (tp.y >= valueStartY && tp.y < valueStartY + static_cast<int16_t>(NUM_CT_INPUTS) * rowH)
   {
      size_t row = static_cast<size_t>((tp.y - valueStartY) / rowH);
      if (row < NUM_CT_INPUTS)
      {
         int16_t ampsColX = sketch.arduino.width() * 2 / 3;
         if (tp.x >= ampsColX)
         {
            editChannelAmpsTouch(row);
         }
         else
         {
            editChannelLocationTouch(row);
         }

         while (sketch.arduino.display.getTouch(&tp) > 0)
         {
            delay(10);
         }
         wasTouching = false;
         drawSettingsView();
      }
   }
}
#endif

void setup()
{
   loadChannelConfig();

   sketch.addSensor("AD7606", []() { return powerSensor.begin(); });
   sketch.setValueSource([]() { return powerSensor.watts(primaryTelemetryChannel); });
   sketch.onSiteResolved(onSiteResolved);
   sketch.onExtraConfig(getExtraConfigText, trySetExtraConfig, startInteractiveChannelPrompts, true);
   sketch.onReadings(
      []() { return powerSensor.formatReadings(); },
      "Reports the latest power (W), RMS current (A), and sensor RMS voltage (V)");
   sketch.arduino.setRotation(DisplayRotation::PORTRAIT_FLIP);

   sketch.begin();
   sketch.completeInitialization();

   // Draw static heading and footer after completeInitialization() clears the startup screen
   sketch.arduino.setTextSize(HEADER_SIZE);
   sketch.arduino.setCursor(0, 0);
   sketch.arduino.println("Power", Color::HEADING);
#ifdef ARDUINO_TOUCH_SUPPORTED
   drawGear();
#endif
   int16_t headingHeight = sketch.arduino.charH();
   int16_t footerHeight = sketch.arduino.charH(FOOTER_SIZE);
   int16_t valuesHeight = static_cast<int16_t>(NUM_CT_INPUTS) * sketch.arduino.charH(TABLE_TEXT_SIZE);
   int16_t availableHeight = sketch.arduino.height() - headingHeight - footerHeight;
   valueStartY = headingHeight + (availableHeight - valuesHeight) / 2;
   if (valueStartY < headingHeight)
   {
      valueStartY = headingHeight;
   }

   sketch.arduino.setTextSize(FOOTER_SIZE);
   sketch.arduino.setCursor(0, sketch.arduino.height() - footerHeight);
   sketch.arduino.printR(VERSION, Color::LIGHTGRAY);

   displayReady = true;
   updateDisplay();
}

void loop()
{
#ifdef ARDUINO_TOUCH_SUPPORTED
   handleTouch();
#endif

   if (updateTimer.ready())
   {
      updateDisplay();

      for (size_t i = 0; i < NUM_CT_INPUTS; i++)
      {
         if (i != primaryTelemetryChannel && channelTopics[i][0] != '\0')
         {
            sketch.publish(channelTopics[i], powerSensor.watts(i), POWER_DECIMALS);
         }
      }
   }

   sketch.loop();
}
