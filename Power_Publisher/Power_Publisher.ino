//
// Power Publisher
//
// Measures AC current and apparent power across up to 3 split-core current transformer (CT)
// sensors (e.g. SCT-013-030 30A/1V, SCT-013-100 100A/1V, or SCT023R 100A:50mA with 20 ohm
// burden resistor) connected to a single ADS1115 16-bit ADC over I2C using AIN3 as the
// shared 1.65V mid-supply differential reference (AIN0-AIN3, AIN1-AIN3, AIN2-AIN3).
// Displays live power (Watts) and RMS current (Amps) on the Feather TFT display and
// publishes per-channel power readings over WebSocket telemetry to Power/<location>/<channel>
// (or Power/<location> when a single channel with an empty suffix is configured).
//
// Hardware & Shared-A3 Differential Wiring (up to 3 CTs on 1 ADS1115):
// - Feather ESP32-S3 TFT connected to the ADS1115 over I2C (STEMMA QT / SDA, SCL, 3.3V, GND)
//   at default address 0x48 (ADDR tied to GND).
// - Common 1.65V reference on ADS1115 A3:
//     * Connect two equal 10k ohm resistors: one from A3 to 3.3V (VDD), and one from A3 to GND.
//     * Connect the Sleeve / Black (S2) return wire of all CTs to A3.
// - Per-channel signal inputs:
//     * CT 1 Tip / White (S1) -> ADS1115 A0 (measured as DIFF_A0_A3)
//     * CT 2 Tip / White (S1) -> ADS1115 A1 (measured as DIFF_A1_A3)
//     * CT 3 Tip / White (S1) -> ADS1115 A2 (measured as DIFF_A2_A3)
// - Burden resistor note: If using a voltage-output CT (30A:1V or 100A:1V), the burden resistor
//   is built into the sensor. If using a 50mA current-output CT (e.g. SCT023R 100A:50mA),
//   place a 20 ohm resistor from that channel's input (A0, A1, or A2) to A3.
//

#include <Arduino.h>
#include <Preferences.h>
#include <array>
#include <cstdlib>
#include <sstream>
#include <string>

#define TEXT_SIZE_3
#define TEXT_SIZE_4
#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 TFT)."
#include "WrongBoard.h"
#endif

#include "CTPowerSensor.h"
#include "Format.h"
#include "LibraryVersion.h"
#include "PublisherSketch.h"
#include "Timer.h"
#include "WiFiSettings.h"

const auto VERSION = MakeVersion("1.0");
constexpr auto SKETCH_NAME = "Power_Publisher";

// ----------- Default CT Inputs Configuration (3 differential inputs sharing A3 reference)
constexpr size_t MAX_LOCATION_LENGTH = 32;

constexpr std::array<ADS1115::Mux, CTPowerSensor::MAX_CHANNELS> CT_INPUT_MUXES = {
   ADS1115::Mux::DIFF_A0_A3,
   ADS1115::Mux::DIFF_A1_A3,
   ADS1115::Mux::DIFF_A2_A3,
};

constexpr std::array<const char*, CTPowerSensor::MAX_CHANNELS> DEFAULT_CT_LOCATIONS = { "1", "", "" };
constexpr std::array<float, CTPowerSensor::MAX_CHANNELS> DEFAULT_CT_AMPS = {
   CTPowerSensor::CORE_30A,
   CTPowerSensor::CORE_100A,
   CTPowerSensor::CORE_100A,
};

constexpr std::array<const char*, CTPowerSensor::MAX_CHANNELS> PREF_KEY_CT_LOCATION = { "loc1", "loc2", "loc3" };
constexpr std::array<const char*, CTPowerSensor::MAX_CHANNELS> PREF_KEY_CT_AMPS = { "amps1", "amps2", "amps3" };

constexpr uint16_t DISPLAY_UPDATE_INTERVAL_MS = 200; // 5 updates per second

// ----------- Telemetry Configuration
constexpr auto POWER_TOPIC_PREFIX = "Power";
constexpr size_t MAX_TOPIC_LENGTH = 64;
constexpr uint8_t POWER_DECIMALS = 0;
constexpr uint16_t TELEMETRY_PUBLISH_INTERVAL_MS = DISPLAY_UPDATE_INTERVAL_MS;

// ----------- Display Layout
constexpr uint8_t HEADER_SIZE = DEFAULT_HEADING_SIZE;
constexpr uint8_t SINGLE_VALUE_SIZE = 4;
constexpr uint8_t MULTI_VALUE_SIZE = 3;
constexpr uint8_t FOOTER_SIZE = DEFAULT_FOOTER_SIZE;

std::array<char[MAX_LOCATION_LENGTH], CTPowerSensor::MAX_CHANNELS> ctLocations = {};
std::array<float, CTPowerSensor::MAX_CHANNELS> ctAmps = DEFAULT_CT_AMPS;
std::array<char[MAX_TOPIC_LENGTH], CTPowerSensor::MAX_CHANNELS> channelTopics = {};
CTPowerSensor powerSensor;

size_t interactivePromptIndex = 0;
void sendNextLocationPrompt();
void sendNextAmpsPrompt();

TelemetryConfig TELEMETRY_CONFIG = {
   .topic = channelTopics[0],
   .decimals = POWER_DECIMALS,
   .publishIntervalMs = TELEMETRY_PUBLISH_INTERVAL_MS,
};

SketchConfig PUBLISHER_CONFIG = {
   .sketchName = SKETCH_NAME,
   .version = VERSION,
   .preferencesNamespace = SKETCH_NAME,
   .cpuFrequencyMhz = 80,
   .enableOTA = true,
};

PublisherSketch sketch(PUBLISHER_CONFIG, TELEMETRY_CONFIG);

///
/// <summary>
/// Rebuilds the active CTPowerSensor channel list from the 3 physical CT inputs (A0, A1, A2).
/// Any input with a non-empty location string is enabled; inputs left blank are disabled.
/// If all 3 inputs are blank, input 1 defaults to enabled so at least one channel is active.
/// </summary>
///
void applyChannelConfigToSensor()
{
   size_t activeCount = 0;
   for (size_t i = 0; i < CTPowerSensor::MAX_CHANNELS; i++)
   {
      if (ctLocations[i][0] != '\0')
      {
         powerSensor.setChannelName(activeCount, ctLocations[i]);
         powerSensor.setCoreRating(ctAmps[i], activeCount);
         powerSensor.setChannelMux(activeCount, CT_INPUT_MUXES[i]);
         activeCount++;
      }
   }

   if (activeCount == 0)
   {
      powerSensor.setChannelName(0, "1");
      powerSensor.setCoreRating(ctAmps[0], 0);
      powerSensor.setChannelMux(0, CT_INPUT_MUXES[0]);
      activeCount = 1;
   }

   powerSensor.setChannelCount(activeCount);
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
   prefs.begin(SKETCH_NAME, true);

   for (size_t i = 0; i < CTPowerSensor::MAX_CHANNELS; i++)
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
/// Persists a single CT input's location string (empty string disables that input).
/// </summary>
/// <param name="inputIndex">Physical CT input index (0 to 2).</param>
/// <param name="loc">Location string (or empty to disable).</param>
///
void saveCtLocation(size_t inputIndex, const char* loc)
{
   Preferences prefs;
   prefs.begin(SKETCH_NAME, false);
   prefs.putString(PREF_KEY_CT_LOCATION[inputIndex], loc);
   prefs.end();

   strlcpy(ctLocations[inputIndex], loc, sizeof(ctLocations[inputIndex]));
}

///
/// <summary>
/// Persists a single CT input's current rating in Amps.
/// </summary>
/// <param name="inputIndex">Physical CT input index (0 to 2).</param>
/// <param name="amps">CT full-scale current rating in Amps (e.g. 30, 50, 100, 200).</param>
///
void saveCtAmps(size_t inputIndex, float amps)
{
   Preferences prefs;
   prefs.begin(SKETCH_NAME, false);
   prefs.putFloat(PREF_KEY_CT_AMPS[inputIndex], amps);
   prefs.end();

   ctAmps[inputIndex] = amps;
   applyChannelConfigToSensor();
}

///
/// <summary>
/// Handles the shell answer for one of the 3 location prompts during interactive SetConfig.
/// Pressing Return (empty input) disables/clears that CT input.
/// </summary>
/// <param name="line">User-entered location line from the shell.</param>
///
void onInteractiveLocationItemAnswer(const char* line)
{
   String input = String(line);
   input.trim();

   saveCtLocation(interactivePromptIndex, input.c_str());
   sketch.addInteractiveSummaryLine(
      "location" + std::to_string(interactivePromptIndex + 1),
      input.length() > 0 ? input.c_str() : "(unused)");

   interactivePromptIndex++;
   if (interactivePromptIndex < CTPowerSensor::MAX_CHANNELS)
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
/// Sends the interactive shell prompt for CT location 1, 2, or 3.
/// </summary>
///
void sendNextLocationPrompt()
{
   String label = "Enter location " + String(interactivePromptIndex + 1) + " (blank = unused)";
   if (ctLocations[interactivePromptIndex][0] != '\0')
   {
      label += " [current: " + String(ctLocations[interactivePromptIndex]) + "]";
   }
   label += ": ";
   DeviceServerClient::shell().print(label);
   DeviceServerClient::shell().flushOutput();
   DeviceServerClient::promptShellLine(onInteractiveLocationItemAnswer);
}

///
/// <summary>
/// Handles the shell answer for one of the 3 CT amp rating prompts during interactive SetConfig.
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
      "amps" + std::to_string(interactivePromptIndex + 1),
      std::to_string(static_cast<int>(rating + 0.5f)));

   interactivePromptIndex++;
   if (interactivePromptIndex < CTPowerSensor::MAX_CHANNELS)
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
/// Sends the interactive shell prompt for CT amp rating 1, 2, or 3 (skipping unused inputs).
/// </summary>
///
void sendNextAmpsPrompt()
{
   while (interactivePromptIndex < CTPowerSensor::MAX_CHANNELS && ctLocations[interactivePromptIndex][0] == '\0')
   {
      interactivePromptIndex++;
   }

   if (interactivePromptIndex >= CTPowerSensor::MAX_CHANNELS)
   {
      sketch.finishInteractiveConfig();
      return;
   }

   int currentAmps = static_cast<int>(ctAmps[interactivePromptIndex] + 0.5f);
   String label = "Enter amps " + String(interactivePromptIndex + 1) + " (" + String(ctLocations[interactivePromptIndex]) + ") [" + String(currentAmps) + "]: ";
   DeviceServerClient::shell().print(label);
   DeviceServerClient::shell().flushOutput();
   DeviceServerClient::promptShellLine(onInteractiveAmpsItemAnswer);
}

///
/// <summary>
/// Starts the 3-item location and 3-item amps prompt sequence during interactive SetConfig.
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
/// the 3 CT locations and 3 CT amp ratings.
/// </summary>
/// <returns>Multi-line key=value string for GetConfig.</returns>
///
std::string getExtraConfigText()
{
   std::ostringstream out;
   for (size_t i = 0; i < CTPowerSensor::MAX_CHANNELS; i++)
   {
      if (i > 0)
      {
         out << "\n";
      }
      out << "location" << (i + 1) << "=" << ctLocations[i]
          << "\namps" << (i + 1) << "=" << static_cast<int>(ctAmps[i] + 0.5f);
   }
   return out.str();
}

///
/// <summary>
/// Handles extra SetConfig keys:
/// - "SetConfig locations" (prompts for 3 locations in shell; blank = unused)
/// - "SetConfig amps" (prompts for 3 CT amp ratings in shell; blank = keep default)
/// - Direct keys "location1".."location3" (use "-" or "none" to clear) and "amps1".."amps3".
/// </summary>
/// <param name="key">Config key passed to SetConfig.</param>
/// <param name="value">New value string.</param>
/// <param name="result">Receives the confirmation or error message.</param>
/// <returns>True if the key was recognized; false otherwise.</returns>
///
bool trySetExtraConfig(const std::string& key, const std::string& value, std::string& result)
{
   for (size_t i = 0; i < CTPowerSensor::MAX_CHANNELS; i++)
   {
      std::string locKey = "location" + std::to_string(i + 1);
      if (strcasecmp(key.c_str(), locKey.c_str()) == 0)
      {
         bool clear = (value == "-" || strcasecmp(value.c_str(), "none") == 0 || strcasecmp(value.c_str(), "off") == 0);
         const char* newLoc = clear ? "" : value.c_str();
         saveCtLocation(i, newLoc);
         result = locKey + "=" + (clear ? "(unused)" : value) + " (restart required to apply)";
         return true;
      }

      std::string ampsKey = "amps" + std::to_string(i + 1);
      if (strcasecmp(key.c_str(), ampsKey.c_str()) == 0)
      {
         float rating = std::strtof(value.c_str(), nullptr);
         if (rating <= 0.0f)
         {
            result = "Error: " + ampsKey + " must be a positive current rating (e.g. 30, 50, 100, 200)";
            return true;
         }

         saveCtAmps(i, rating);
         result = ampsKey + "=" + std::to_string(static_cast<int>(rating + 0.5f));
         return true;
      }
   }

   return false;
}

///
/// <summary>
/// Builds the per-channel telemetry topics as "Power/<channelLocation>" for each enabled CT.
/// </summary>
///
void onSiteResolved()
{
   size_t count = powerSensor.channelCount();
   for (size_t i = 0; i < count; i++)
   {
      const char* loc = powerSensor.channelName(i);
      std::string fullTopic = std::string(POWER_TOPIC_PREFIX) + "/" + (loc != nullptr && loc[0] != '\0' ? loc : "1");
      strlcpy(channelTopics[i], fullTopic.c_str(), sizeof(channelTopics[i]));
   }
}

Format singleWattsFormat("##### W", Format::Alignment::CENTER);
Format singleAmpsFormat("###.# A", Format::Alignment::CENTER);
Format rowWattsFormat("##### W");
Format rowAmpsFormat(" ###.# A");

Timer updateTimer(DISPLAY_UPDATE_INTERVAL_MS);
int16_t valueStartY = 0;
bool displayReady = false;

///
/// <summary>
/// Renders the current power (Watts) and RMS current (Amps) on the TFT display.
/// In single-channel mode, renders large centered Watts and Amps. In multi-channel mode,
/// renders one row per enabled CT channel with its configured name, Watts, and Amps.
/// </summary>
///
void updateDisplay()
{
   if (!displayReady)
   {
      return;
   }

   sketch.arduino.setCursorY(valueStartY);

   if (powerSensor.channelCount() == 1)
   {
      sketch.arduino.setTextSize(SINGLE_VALUE_SIZE);
      sketch.arduino.printlnC(powerSensor.watts(0), singleWattsFormat, Color::VALUE);
      sketch.arduino.printlnC(powerSensor.amps(0), singleAmpsFormat, Color::VALUE2);
   }
   else
   {
      sketch.arduino.setTextSize(MULTI_VALUE_SIZE);
      for (size_t i = 0; i < powerSensor.channelCount(); i++)
      {
         sketch.arduino.print(std::string(powerSensor.channelName(i)) + ": ", Color::LABEL);
         sketch.arduino.print(powerSensor.watts(i), rowWattsFormat, Color::VALUE);
         sketch.arduino.printlnR(powerSensor.amps(i), rowAmpsFormat, Color::VALUE2);
      }
   }
}

void setup()
{
   loadChannelConfig();

   sketch.addSensor("ADS1115", []() { return powerSensor.begin(); });
   sketch.setValueSource([]() { return powerSensor.watts(0); });
   sketch.onSiteResolved(onSiteResolved);
   sketch.onExtraConfig(getExtraConfigText, trySetExtraConfig, startInteractiveChannelPrompts, true);
   sketch.onReadings(
      []() { return powerSensor.formatReadings(); },
      "Reports the latest power (W), RMS current (A), and sensor RMS voltage (V)");

   sketch.begin();
   sketch.completeInitialization();

   // Draw static heading and footer after completeInitialization() clears the startup screen
   sketch.arduino.setTextSize(HEADER_SIZE);
   sketch.arduino.setCursor(0, 0);
   sketch.arduino.println("Power", Color::HEADING);
   int16_t headingHeight = sketch.arduino.charH();
   int16_t footerHeight = sketch.arduino.charH(FOOTER_SIZE);
   int16_t valuesHeight = powerSensor.channelCount() == 1
      ? (2 * sketch.arduino.charH(SINGLE_VALUE_SIZE))
      : (static_cast<int16_t>(powerSensor.channelCount()) * sketch.arduino.charH(MULTI_VALUE_SIZE));
   int16_t availableHeight = sketch.arduino.height() - headingHeight - footerHeight;
   valueStartY = headingHeight + (availableHeight - valuesHeight) / 2;

   sketch.arduino.setTextSize(FOOTER_SIZE);
   sketch.arduino.setCursor(0, sketch.arduino.height() - footerHeight);
   if (powerSensor.channelCount() == 1)
   {
      sketch.arduino.print(std::string(powerSensor.channelName(0)), Color::CYAN);
   }
   sketch.arduino.printR(VERSION, Color::LIGHTGRAY);

   displayReady = true;
   updateDisplay();
}

void loop()
{
   if (updateTimer.ready())
   {
      updateDisplay();

      for (size_t i = 1; i < powerSensor.channelCount(); i++)
      {
         sketch.publish(channelTopics[i], powerSensor.watts(i), POWER_DECIMALS);
      }
   }

   sketch.loop();
}
