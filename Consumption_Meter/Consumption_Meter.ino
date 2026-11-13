//
// Consumption Meter
//
// Measures AC current across up to 16 split-core current transformer (CT) sensors.
// Displays RMS current (A) and estimated apparent power (VA) on the touch display and
// publishes RMS current to Current/<location>/<circuit>. Apparent power uses nominal
// voltage; real watts and power-flow direction are not measured.
//
// The ADC hardware is selected with POWER_SENSOR_TYPE (both derive from PowerSensorBase):
//   POWER_SENSOR_MUX_ADS - 74HC4067 16:1 mux + ADS1015 (I2C), channels sampled in turn
//   POWER_SENSOR_AD7606  - AD7606 simultaneous-sampling ADC (on hold; BUSY was stuck high
//                          on both boards tested)
//
// Mux + ADS1015 wiring (Waveshare ESP32-P4 4.3" Touch LCD, 40-pin header):
//     Wire color   Waveshare pin          Connects to
//     ----------   --------------------   ---------------------------------------------
//     blue         GPIO29                 ADS1015 SDA (dedicated Wire1 bus)
//     yellow       GPIO30                 ADS1015 SCL
//     purple       GPIO2                  74HC4067 S0
//     orange       GPIO3                  74HC4067 S1
//     brown        GPIO4                  74HC4067 S2
//     white        GPIO28                 74HC4067 S3
//     red          3V3                    74HC4067 VCC, ADS1015 VDD, bias divider
//     black        GND                    74HC4067 GND and EN, ADS1015 GND and ADDR (and 1M/470k resistor from A0)
//     green        (module to module)     74HC4067 SIG -> ADS1015 A0 (with 1M/470k pull-down to GND for unplug detect)
//     (jumper)     (bias to ADS1015)      1.65V bias junction -> ADS1015 A3 (differential ref)
//   CT cables go to mux C0..C15. Each CT cable has its own burden resistor. All channels
//   share one 1.65V bias connected to ADS1015 A3 (DIFF_A0_A3): CT S2 -> bias/A3, CT S1 -> mux.
//
// AD7606 serial mode wiring (when POWER_SENSOR_AD7606 is selected):
//     GPIO28 (white) CS, GPIO30 (orange) RST, GPIO4 (blue) DB7/DOUTA, GPIO3 (yellow) RD/SCLK,
//     GPIO29 (gray) BUSY, GPIO2 (purple) CONVSTA+CONVSTB, GND (black), 3V3 (red) VIO,
//     5V (red) AVCC. RANGE, OS0-OS2 and DB15 to GND. CT S2 to AGND, burden across Vx and AGND.
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
#define TEXT_SIZE_5
#include "ArduinoBoard.h"

#ifndef ARDUINO_WAVESHARE_ESP32P4_TOUCH_LCD_43
#error "This sketch requires the Waveshare ESP32-P4 4.3 Touch LCD board."
#include "WrongBoard.h"
#endif

#include "AD7606PowerSensor.h"
#include "MuxADSPowerSensor.h"
#include "Format.h"
#include "LibraryVersion.h"
#include "PublisherSketch.h"
#include "Timer.h"
#include "WiFiSettings.h"

const auto VERSION = MakeVersion("1.0");
constexpr auto SKETCH_NAME = "Consumption_Meter";
constexpr auto PREF_NAMESPACE = "ConsMeter";

// Select the ADC hardware: 1 = 74HC4067 mux + ADS1015, 2 = AD7606
#define POWER_SENSOR_MUX_ADS 1
#define POWER_SENSOR_AD7606 2
#define POWER_SENSOR_TYPE POWER_SENSOR_MUX_ADS

// ----------- 16-Channel Configuration
#if POWER_SENSOR_TYPE == POWER_SENSOR_AD7606
constexpr size_t NUM_CT_INPUTS = AD7606::NUM_CHANNELS;
#else
constexpr size_t NUM_CT_INPUTS = PowerSensorBase::MAX_CHANNELS;
#endif
constexpr size_t MAX_LOCATION_LENGTH = 32;

constexpr uint8_t PIN_MUX_S0 = 2;
constexpr uint8_t PIN_MUX_S1 = 3;
constexpr uint8_t PIN_MUX_S2 = 4;
constexpr uint8_t PIN_MUX_S3 = 28;
constexpr uint8_t PIN_ADS_SDA = 29;
constexpr uint8_t PIN_ADS_SCL = 30;

constexpr uint8_t PIN_AD7606_CONVST = 2;
constexpr uint8_t PIN_AD7606_SCLK = 3;
constexpr uint8_t PIN_AD7606_MISO = 4;
constexpr uint8_t PIN_AD7606_CS = 28;
constexpr int8_t PIN_AD7606_BUSY = 29;
constexpr int8_t PIN_AD7606_RESET = 30;

constexpr std::array<const char*, PowerSensorBase::MAX_CHANNELS> DEFAULT_CT_LOCATIONS = {
   "1", "", "", "", "", "", "", "",
   "",  "", "", "", "", "", "", "",
};

constexpr std::array<float, PowerSensorBase::MAX_CHANNELS> DEFAULT_CT_AMPS = {
   PowerSensorBase::CORE_30A,  PowerSensorBase::CORE_150A, PowerSensorBase::CORE_100A, PowerSensorBase::CORE_100A,
   PowerSensorBase::CORE_30A,  PowerSensorBase::CORE_30A,  PowerSensorBase::CORE_30A,  PowerSensorBase::CORE_30A,
   PowerSensorBase::CORE_30A,  PowerSensorBase::CORE_30A,  PowerSensorBase::CORE_30A,  PowerSensorBase::CORE_30A,
   PowerSensorBase::CORE_30A,  PowerSensorBase::CORE_30A,  PowerSensorBase::CORE_30A,  PowerSensorBase::CORE_30A,
};

constexpr std::array<const char*, PowerSensorBase::MAX_CHANNELS> PREF_KEY_CT_LOCATION = {
   "loc1", "loc2",  "loc3",  "loc4",  "loc5",  "loc6",  "loc7",  "loc8",
   "loc9", "loc10", "loc11", "loc12", "loc13", "loc14", "loc15", "loc16",
};

constexpr std::array<const char*, PowerSensorBase::MAX_CHANNELS> PREF_KEY_CT_AMPS = {
   "amps1", "amps2",  "amps3",  "amps4",  "amps5",  "amps6",  "amps7",  "amps8",
   "amps9", "amps10", "amps11", "amps12", "amps13", "amps14", "amps15", "amps16",
};

constexpr uint16_t DISPLAY_UPDATE_INTERVAL_MS = 200; // 5 updates per second

// ----------- Telemetry Configuration
constexpr auto CURRENT_TOPIC_PREFIX = "Current";
constexpr size_t MAX_TOPIC_LENGTH = 64;
constexpr uint8_t CURRENT_DECIMALS = 2;
constexpr uint16_t TELEMETRY_PUBLISH_INTERVAL_MS = DISPLAY_UPDATE_INTERVAL_MS;

// ----------- Display Layout
constexpr uint8_t HEADER_SIZE = DEFAULT_HEADING_SIZE;
constexpr uint8_t TABLE_TEXT_SIZE = DEFAULT_TEXT_SIZE;
constexpr uint8_t FOOTER_SIZE = DEFAULT_FOOTER_SIZE;
constexpr size_t LOCATION_COL_WIDTH = 12;

std::array<char[MAX_LOCATION_LENGTH], NUM_CT_INPUTS> ctLocations = {};
std::array<char[8], PowerSensorBase::MAX_CHANNELS> defaultChannelNames = {
   "1", "2",  "3",  "4",  "5",  "6",  "7",  "8",
   "9", "10", "11", "12", "13", "14", "15", "16",
};
std::array<float, NUM_CT_INPUTS> ctAmps = {};
std::array<char[MAX_TOPIC_LENGTH], NUM_CT_INPUTS> channelTopics = {};
char primaryTelemetryTopic[MAX_TOPIC_LENGTH] = "Current/1";
size_t primaryTelemetryChannel = 0;
bool telemetryTopicsResolved = false;
std::array<PowerSensorBase::ChannelConfig, PowerSensorBase::MAX_CHANNELS> initialChannelConfig = {
   PowerSensorBase::ChannelConfig{ "1",  PowerSensorBase::CORE_30A },
   PowerSensorBase::ChannelConfig{ "2",  PowerSensorBase::CORE_150A },
   PowerSensorBase::ChannelConfig{ "3",  PowerSensorBase::CORE_100A },
   PowerSensorBase::ChannelConfig{ "4",  PowerSensorBase::CORE_100A },
   PowerSensorBase::ChannelConfig{ "5",  PowerSensorBase::CORE_30A },
   PowerSensorBase::ChannelConfig{ "6",  PowerSensorBase::CORE_30A },
   PowerSensorBase::ChannelConfig{ "7",  PowerSensorBase::CORE_30A },
   PowerSensorBase::ChannelConfig{ "8",  PowerSensorBase::CORE_30A },
   PowerSensorBase::ChannelConfig{ "9",  PowerSensorBase::CORE_30A },
   PowerSensorBase::ChannelConfig{ "10", PowerSensorBase::CORE_30A },
   PowerSensorBase::ChannelConfig{ "11", PowerSensorBase::CORE_30A },
   PowerSensorBase::ChannelConfig{ "12", PowerSensorBase::CORE_30A },
   PowerSensorBase::ChannelConfig{ "13", PowerSensorBase::CORE_30A },
   PowerSensorBase::ChannelConfig{ "14", PowerSensorBase::CORE_30A },
   PowerSensorBase::ChannelConfig{ "15", PowerSensorBase::CORE_30A },
   PowerSensorBase::ChannelConfig{ "16", PowerSensorBase::CORE_30A },
};
#if POWER_SENSOR_TYPE == POWER_SENSOR_AD7606
AD7606PowerSensor powerSensor(
   PIN_AD7606_CONVST,
   PIN_AD7606_SCLK,
   PIN_AD7606_MISO,
   PIN_AD7606_CS,
   initialChannelConfig.data(),
   NUM_CT_INPUTS,
   PowerSensorBase::DEFAULT_LINE_VOLTAGE_V,
   AD7606::Range::BIPOLAR_5V,
   PIN_AD7606_BUSY,
   PIN_AD7606_RESET);
#else
MuxADSPowerSensor powerSensor(
   PIN_MUX_S0,
   PIN_MUX_S1,
   PIN_MUX_S2,
   PIN_MUX_S3,
   PIN_ADS_SDA,
   PIN_ADS_SCL,
   initialChannelConfig.data(),
   NUM_CT_INPUTS,
   PowerSensorBase::DEFAULT_LINE_VOLTAGE_V);
#endif

size_t interactivePromptIndex = 0;
void sendNextLocationPrompt();
void sendNextAmpsPrompt();
void sendSingleChannelLocationPrompt();
void sendSingleChannelAmpsPrompt();

TelemetryConfig TELEMETRY_CONFIG = {
   .topic = primaryTelemetryTopic,
   .decimals = CURRENT_DECIMALS,
   .unit = TelemetryUnits::AMPS,
   .description = "RMS current for this circuit; power-flow direction is not measured",
   .publishIntervalMs = TELEMETRY_PUBLISH_INTERVAL_MS,
};

SketchConfig PUBLISHER_CONFIG = {
   .sketchName = SKETCH_NAME,
   .version = VERSION,
   .preferencesNamespace = PREF_NAMESPACE,
   .cpuFrequencyMhz = 80,
   .enableOTA = true,
};

PublisherSketch sketch(PUBLISHER_CONFIG, TELEMETRY_CONFIG);

///
/// <summary>
/// Rebuilds the per-channel RMS current topics as "Current/<location>/<circuit>" for each
/// configured CT and updates the active TelemetryPublisher topic immediately if already
/// running.
/// </summary>
///
void refreshTelemetryTopics()
{
   std::string deviceLocation = sketch.location().c_str();
   std::string topicPrefix = std::string(CURRENT_TOPIC_PREFIX) + "/";
   if (!deviceLocation.empty())
   {
      topicPrefix += deviceLocation + "/";
   }

   bool foundPrimary = false;
   for (size_t i = 0; i < NUM_CT_INPUTS; i++)
   {
      if (ctLocations[i][0] != '\0')
      {
         std::string fullTopic = topicPrefix + ctLocations[i];
         strlcpy(channelTopics[i], fullTopic.c_str(), sizeof(channelTopics[i]));
         if (telemetryTopicsResolved)
         {
            sketch.createTopic(channelTopics[i], TelemetryTopicMetadata{ .unit = TelemetryUnits::AMPS, .description = "RMS current for this circuit; power-flow direction is not measured", .decimals = CURRENT_DECIMALS });
         }
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
      std::string fallbackTopic = topicPrefix + "1";
      strlcpy(primaryTelemetryTopic, fallbackTopic.c_str(), sizeof(primaryTelemetryTopic));
      if (telemetryTopicsResolved)
      {
         sketch.createTopic(primaryTelemetryTopic, TelemetryTopicMetadata{ .unit = TelemetryUnits::AMPS, .description = "RMS current for this circuit; power-flow direction is not measured", .decimals = CURRENT_DECIMALS });
      }
   }

   if (sketch.client() != nullptr)
   {
      sketch.client()->setTopic(primaryTelemetryTopic);
   }
}

///
/// <summary>
/// Updates all physical CT channels on powerSensor so every channel is
/// continuously sampled for connection status, RMS current, and estimated apparent power
/// regardless of whether a location string is assigned.
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
/// Returns the 0-based channel index that already uses the given circuit name, or -1 if
/// the name is empty or unique among all other channels on this meter.
/// </summary>
/// <param name="inputIndex">Channel index being edited (0 to 7).</param>
/// <param name="loc">Candidate circuit name.</param>
/// <returns>Conflicting channel index (0 to 7), or -1 if unique.</returns>
///
int findDuplicateChannel(size_t inputIndex, const char* loc)
{
   if (loc == nullptr || loc[0] == '\0')
   {
      return -1;
   }

   for (size_t i = 0; i < NUM_CT_INPUTS; i++)
   {
      if (i != inputIndex && ctLocations[i][0] != '\0' && strcasecmp(ctLocations[i], loc) == 0)
      {
         return static_cast<int>(i);
      }
   }

   return -1;
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
      int duplicateChannel = findDuplicateChannel(interactivePromptIndex, newLoc);
      if (duplicateChannel >= 0)
      {
         DeviceServerClient::shell().println(
            ("Error: \"" + String(newLoc) + "\" is already used by channel " + String(duplicateChannel + 1) + ".").c_str());
         sendSingleChannelLocationPrompt();
         return;
      }

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
      int duplicateChannel = findDuplicateChannel(interactivePromptIndex, newLoc);
      if (duplicateChannel >= 0)
      {
         DeviceServerClient::shell().println(
            ("Error: \"" + String(newLoc) + "\" is already used by channel " + String(duplicateChannel + 1) + ".").c_str());
         sendNextLocationPrompt();
         return;
      }

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
         int duplicateChannel = findDuplicateChannel(i, newLoc);
         if (duplicateChannel >= 0)
         {
            result = "Error: \"" + value + "\" is already used by l" + std::to_string(duplicateChannel + 1);
            return true;
         }

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

Format rowIndexFormat("## ", Format::Alignment::RIGHT);
Format rowLocationFormat(LOCATION_COL_WIDTH, Format::Alignment::LEFT);
Format rowAmpsFormat(" ###.# A", Format::Alignment::RIGHT);
Format rowApparentPowerFormat("##### VA", Format::Alignment::RIGHT);

Timer updateTimer(DISPLAY_UPDATE_INTERVAL_MS);
int16_t valueStartY = 0;
bool displayReady = false;

#ifdef ARDUINO_TOUCH_SUPPORTED
// ----------- Gear/close icon & Touch Settings UI
constexpr int16_t GEAR_RADIUS = 16;
constexpr int16_t GEAR_MARGIN = 6;
constexpr int16_t GEAR_TAP_SIZE = 56;
constexpr size_t NUM_AMP_OPTIONS = 9;
constexpr std::array<float, NUM_AMP_OPTIONS> AMP_RATING_OPTIONS = {
   PowerSensorBase::CORE_15A,
   PowerSensorBase::CORE_20A,
   PowerSensorBase::CORE_30A,
   PowerSensorBase::CORE_50A,
   PowerSensorBase::CORE_100A,
   PowerSensorBase::CORE_150A,
   PowerSensorBase::CORE_200A,
   PowerSensorBase::CORE_213A,
   PowerSensorBase::CORE_500A,
};

constexpr int16_t SETTINGS_MARGIN_X = 8;
constexpr int16_t SETTINGS_GAP = 8;
constexpr int16_t SETTINGS_CORNER_RADIUS = 8;
constexpr size_t CHANNEL_GRID_COLS = 4;
constexpr size_t CHANNEL_GRID_ROWS = (NUM_CT_INPUTS + CHANNEL_GRID_COLS - 1) / CHANNEL_GRID_COLS;
constexpr size_t AMP_GRID_COLS = 3;
constexpr size_t AMP_GRID_ROWS = 3;

bool showingSettings = false;
bool wasTouching = false;
size_t selectedSettingsChannel = 0;
std::array<Rect16, NUM_CT_INPUTS> channelBoxRects = {};
Rect16 labelBoxRect;
std::array<Rect16, NUM_AMP_OPTIONS> ampBoxRects = {};

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
/// Returns true if the given touch point falls within rect.
/// </summary>
/// <param name="rect">Tap target to test.</param>
/// <param name="x">Touch x coordinate.</param>
/// <param name="y">Touch y coordinate.</param>
/// <returns>True if the touch point is inside the rect.</returns>
///
bool inRect(const Rect16& rect, int16_t x, int16_t y)
{
   return x >= rect.left() && x < rect.right() && y >= rect.top() && y < rect.bottom();
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
/// Draws a pencil edit icon centered at (cx, cy).
/// </summary>
/// <param name="cx">Center x coordinate.</param>
/// <param name="cy">Center y coordinate.</param>
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
/// Draws the touch configuration view:
/// - Upper portion: 8 channel squares (2x4 grid) with the selected channel highlighted.
/// - Bottom portion: the selected channel's editable location label box (with edit icon)
///   and a 3x3 grid of CT amp rating boxes with the active rating highlighted.
/// </summary>
///
void drawSettingsView()
{
   sketch.arduino.clearDisplay();
   sketch.arduino.setTextSize(HEADER_SIZE);
   sketch.arduino.setCursor(0, 0);
   sketch.arduino.println("Configure", Color::HEADING);
   drawCloseIcon();

   const int16_t screenW = sketch.arduino.width();
   const int16_t screenH = sketch.arduino.height();
   const int16_t contentW = screenW - 2 * SETTINGS_MARGIN_X;
   int16_t y = sketch.arduino.getCursorY() + SETTINGS_GAP;

   // Upper portion: 16 channel boxes (4 columns x 4 rows) for channels 1-16
   sketch.arduino.setTextSize(FOOTER_SIZE);
   sketch.arduino.setCursor(SETTINGS_MARGIN_X, y);
   sketch.arduino.println("Channel", Color::SUB_LABEL);
   y = sketch.arduino.getCursorY() + 4;

   const int16_t chBoxW = (contentW - static_cast<int16_t>(CHANNEL_GRID_COLS - 1) * SETTINGS_GAP) / static_cast<int16_t>(CHANNEL_GRID_COLS);
   const int16_t chBoxH = CHANNEL_GRID_ROWS > 2 ? 68 : chBoxW;
   sketch.arduino.setTextSize(TABLE_TEXT_SIZE);
   const int16_t chNumH = sketch.arduino.charH(TABLE_TEXT_SIZE);

   for (size_t i = 0; i < NUM_CT_INPUTS; i++)
   {
      const size_t col = i % CHANNEL_GRID_COLS;
      const size_t row = i / CHANNEL_GRID_COLS;
      const int16_t bx = SETTINGS_MARGIN_X + static_cast<int16_t>(col) * (chBoxW + SETTINGS_GAP);
      const int16_t by = y + static_cast<int16_t>(row) * (chBoxH + SETTINGS_GAP);
      channelBoxRects[i] = Rect16(bx, by, chBoxW, chBoxH);

      const bool selected = (i == selectedSettingsChannel);
      const bool configured = (ctLocations[i][0] != '\0');
      const Color bg = selected ? Color::DARKGREEN : (configured ? Color::DARKBLUE : Color::BLACK);
      const Color border = selected ? Color::WHITE : (configured ? Color::LIGHTBLUE : Color::GRAY);
      const Color textColor = (selected || configured) ? Color::WHITE : Color::GRAY;

      sketch.arduino.display.fillRoundRect(bx, by, chBoxW, chBoxH, SETTINGS_CORNER_RADIUS, static_cast<uint16_t>(bg));
      sketch.arduino.display.drawRoundRect(bx, by, chBoxW, chBoxH, SETTINGS_CORNER_RADIUS, static_cast<uint16_t>(border));
      if (selected)
      {
         sketch.arduino.display.drawRoundRect(bx + 1, by + 1, chBoxW - 2, chBoxH - 2, SETTINGS_CORNER_RADIUS, static_cast<uint16_t>(border));
      }

      String numStr = String(i + 1);
      sketch.arduino.display.setTextColor(static_cast<uint16_t>(textColor), static_cast<uint16_t>(bg));
      const int16_t tw = sketch.arduino.display.textWidth(numStr.c_str());
      sketch.arduino.display.setCursor(bx + (chBoxW - tw) / 2, by + (chBoxH - chNumH) / 2);
      sketch.arduino.display.print(numStr.c_str());
   }

   y += static_cast<int16_t>(CHANNEL_GRID_ROWS) * chBoxH + static_cast<int16_t>(CHANNEL_GRID_ROWS - 1) * SETTINGS_GAP + 2 * SETTINGS_GAP;

   // Selected channel's name field with inline edit icon
   const int16_t labelBoxH = sketch.arduino.charH(TABLE_TEXT_SIZE) + 28;
   labelBoxRect = Rect16(SETTINGS_MARGIN_X, y, contentW, labelBoxH);
   sketch.arduino.display.fillRoundRect(labelBoxRect.left(), labelBoxRect.top(), labelBoxRect.width, labelBoxRect.height, SETTINGS_CORNER_RADIUS, static_cast<uint16_t>(Color::DARKBLUE));
   sketch.arduino.display.drawRoundRect(labelBoxRect.left(), labelBoxRect.top(), labelBoxRect.width, labelBoxRect.height, SETTINGS_CORNER_RADIUS, static_cast<uint16_t>(Color::WHITE));

   const bool hasLoc = ctLocations[selectedSettingsChannel][0] != '\0';
   const char* locText = hasLoc ? ctLocations[selectedSettingsChannel] : "(tap to set)";
   const Color locColor = hasLoc ? Color::WHITE : Color::LIGHTGRAY;
   sketch.arduino.setTextSize(TABLE_TEXT_SIZE);
   sketch.arduino.display.setTextColor(static_cast<uint16_t>(locColor), static_cast<uint16_t>(Color::DARKBLUE));
   const int16_t locTw = sketch.arduino.display.textWidth(locText);
   int16_t locTx = labelBoxRect.left() + (labelBoxRect.width - locTw) / 2;
   if (locTx < labelBoxRect.left() + 12)
   {
      locTx = labelBoxRect.left() + 12;
   }
   sketch.arduino.display.setCursor(locTx, labelBoxRect.top() + (labelBoxRect.height - sketch.arduino.charH(TABLE_TEXT_SIZE)) / 2);
   sketch.arduino.display.print(locText);
   drawEditIcon(labelBoxRect.right() - 24, labelBoxRect.top() + labelBoxRect.height / 2, Color::LIGHTGRAY);

   y += labelBoxH + 2 * SETTINGS_GAP;

   // Selected channel's amp rating section (3x3 grid of boxes)
   sketch.arduino.setTextSize(FOOTER_SIZE);
   sketch.arduino.setCursor(SETTINGS_MARGIN_X, y);
   sketch.arduino.println("Amp Rating", Color::SUB_LABEL);
   y = sketch.arduino.getCursorY() + 4;

   const int16_t availAmpH = screenH - y - SETTINGS_MARGIN_X;
   int16_t ampBoxH = (availAmpH - static_cast<int16_t>(AMP_GRID_ROWS - 1) * SETTINGS_GAP) / static_cast<int16_t>(AMP_GRID_ROWS);
   if (ampBoxH > 84)
   {
      ampBoxH = 84;
   }
   const int16_t ampBoxW = (contentW - static_cast<int16_t>(AMP_GRID_COLS - 1) * SETTINGS_GAP) / static_cast<int16_t>(AMP_GRID_COLS);

   sketch.arduino.setTextSize(TABLE_TEXT_SIZE);
   const int16_t ampTextH = sketch.arduino.charH(TABLE_TEXT_SIZE);
   const float currentAmps = ctAmps[selectedSettingsChannel];

   for (size_t i = 0; i < NUM_AMP_OPTIONS; i++)
   {
      const size_t col = i % AMP_GRID_COLS;
      const size_t row = i / AMP_GRID_COLS;
      const int16_t bx = SETTINGS_MARGIN_X + static_cast<int16_t>(col) * (ampBoxW + SETTINGS_GAP);
      const int16_t by = y + static_cast<int16_t>(row) * (ampBoxH + SETTINGS_GAP);
      ampBoxRects[i] = Rect16(bx, by, ampBoxW, ampBoxH);

      const bool selected = (std::fabs(currentAmps - AMP_RATING_OPTIONS[i]) < 0.5f);
      const Color bg = selected ? Color::DARKGREEN : Color::BLUE;
      const Color border = selected ? Color::WHITE : Color::GRAY;

      sketch.arduino.display.fillRoundRect(bx, by, ampBoxW, ampBoxH, SETTINGS_CORNER_RADIUS, static_cast<uint16_t>(bg));
      sketch.arduino.display.drawRoundRect(bx, by, ampBoxW, ampBoxH, SETTINGS_CORNER_RADIUS, static_cast<uint16_t>(border));
      if (selected)
      {
         sketch.arduino.display.drawRoundRect(bx + 1, by + 1, ampBoxW - 2, ampBoxH - 2, SETTINGS_CORNER_RADIUS, static_cast<uint16_t>(border));
      }

      String ampStr = String(static_cast<int>(AMP_RATING_OPTIONS[i] + 0.5f)) + " A";
      sketch.arduino.display.setTextColor(static_cast<uint16_t>(Color::WHITE), static_cast<uint16_t>(bg));
      const int16_t tw = sketch.arduino.display.textWidth(ampStr.c_str());
      sketch.arduino.display.setCursor(bx + (ampBoxW - tw) / 2, by + (ampBoxH - ampTextH) / 2);
      sketch.arduino.display.print(ampStr.c_str());
   }
}

///
/// <summary>
/// Opens the on-screen touch keyboard directly to edit the location for a channel.
/// </summary>
/// <param name="ch">Channel index (0 to 7).</param>
///
void editChannelLocationTouch(size_t ch)
{
   String kbTitle = "Location " + String(ch + 1) + " (- to clear)";
   String initialText = String(ctLocations[ch]);

   while (true)
   {
      String entered = sketch.arduino.promptTouchText(kbTitle.c_str(), initialText);
      entered.trim();
      bool clear = isClearLocationValue(entered.c_str());
      const char* newLoc = clear ? "" : entered.c_str();
      int duplicateChannel = findDuplicateChannel(ch, newLoc);
      if (duplicateChannel < 0)
      {
         saveCtLocation(ch, newLoc);
         return;
      }

      kbTitle = "Used by Ch " + String(duplicateChannel + 1) + " - try again";
      initialText = entered;
   }
}
#endif

///
/// <summary>
/// Renders a 4-column table of all 8 AD7606 inputs on the display:
/// Column 1: Channel number (1-8)
/// Column 2: Configured location name
/// Column 3: RMS current in Amps (or gray dashes if unplugged)
/// Column 4: Estimated apparent power in VA (or gray dashes if unplugged)
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
         sketch.arduino.printlnR(powerSensor.apparentPower(i), rowApparentPowerFormat, Color::VALUE);
      }
      else
      {
         sketch.arduino.print(rowAmpsFormat.toNoValueString().c_str(), Color::GRAY);
         sketch.arduino.printlnR(rowApparentPowerFormat.toNoValueString().c_str(), Color::GRAY);
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

   for (size_t i = 0; i < NUM_CT_INPUTS; i++)
   {
      if (inRect(channelBoxRects[i], tp.x, tp.y))
      {
         if (selectedSettingsChannel != i)
         {
            selectedSettingsChannel = i;
            drawSettingsView();
         }
         return;
      }
   }

   if (inRect(labelBoxRect, tp.x, tp.y))
   {
      editChannelLocationTouch(selectedSettingsChannel);
      while (sketch.arduino.display.getTouch(&tp) > 0)
      {
         delay(10);
      }
      wasTouching = false;
      drawSettingsView();
      return;
   }

   for (size_t i = 0; i < NUM_AMP_OPTIONS; i++)
   {
      if (inRect(ampBoxRects[i], tp.x, tp.y))
      {
         if (std::fabs(ctAmps[selectedSettingsChannel] - AMP_RATING_OPTIONS[i]) >= 0.5f)
         {
            saveCtAmps(selectedSettingsChannel, AMP_RATING_OPTIONS[i]);
            drawSettingsView();
         }
         return;
      }
   }
}
#endif

void setup()
{
   loadChannelConfig();

   sketch.addSensor("PowerSensor", []() { return powerSensor.begin(); });
   sketch.setValueSource([]() { return powerSensor.amps(primaryTelemetryChannel); });
   sketch.onSiteResolved([]() {
      telemetryTopicsResolved = true;
      refreshTelemetryTopics();
   });
   sketch.onExtraConfig(getExtraConfigText, trySetExtraConfig, startInteractiveChannelPrompts);
   sketch.onReadings(
      []() { return powerSensor.formatReadings(); },
      "Reports RMS current (A), estimated apparent power (VA), and sensor RMS voltage (V)");
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
            sketch.submit(channelTopics[i], powerSensor.amps(i));
         }
      }
   }

   sketch.loop();
}
