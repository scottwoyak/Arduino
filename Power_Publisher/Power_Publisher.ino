//
// Power Publisher
//
// Measures real AC power (Watts) and RMS current (Amps) across up to 16 split-core current
// transformer (CT) sensors alongside live AC line voltage from a ZMPT101B sensor, with
// automatic quadrature phase compensation and bidirectional polarity on Solar* circuits
// (positive when generating, negative for nighttime standby draw). Supports split-phase
// "<Prefix>Red" / "<Prefix>Black" / "<Prefix>White" panel triplets: when one hot leg and
// White neutral are clamped, the missing hot leg's current and power are synthesized and
// derived automatically; "*White" neutral channels are shown on the display but excluded
// from WebSocket telemetry. Displays live line voltage (V), per-channel RMS current (A),
// and real power (W) on the touch display (dimming to 10% brightness when idle and
// restoring full brightness on touch) and publishes per-channel power readings over
// WebSocket telemetry to Power/<location>/<circuit>.
//
// Mux + ESP32-P4 ADC1 + ZMPT101B wiring (Waveshare ESP32-P4 4.3" Touch LCD, 40-pin header):
//     P4 Pin / GPIO   Wire color   PCB connection
//     -------------   ----------   -----------------------------------------------------------
//     GPIO2           purple       74HC4067 S0
//     GPIO3           orange       74HC4067 S1
//     GPIO4           brown        74HC4067 S2
//     GPIO28          white        74HC4067 S3
//     GPIO22          green        74HC4067 SIG (+ 470k-1M pull-down to GND for unplug detect)
//     GPIO21          blue         ZMPT101B OUT (direct; optional 470k-1M pull-down to GND)
//     5V              red          LM2596HV OUT+ (pre-adjusted to 5.00V DC before connecting)
//     3V3             red          74HC4067 VCC, ZMPT101B VCC, top 10k leg of 1.65V bias
//     GND             black        LM2596HV OUT-, 74HC4067 GND & EN, ZMPT101B GND, bottom 10k
//                                  leg + 10uF (-) cap of 1.65V bias, pull-down resistor(s)
//   On PCB: 9V AC wall-wart feeds LM2596HV AC input and ZMPT101B AC terminals in parallel.
//   CT S2 returns tie to the 1.65V bias node; CT S1 signals go to 74HC4067 C0..C15 (20A
//   option uses CORE_40A for 20A:25mA SCT006 + 20 ohm burden; 500A option uses CORE_213A
//   for 500A:50mA + 47 ohm burden).
//

#include <Arduino.h>
#include <Preferences.h>
#include <array>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <string>

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

#include "CT_Calibration.h"
#include "DisplayValue.h"
#include "Format.h"
#include "LibraryVersion.h"
#include "MuxAdcPowerSensor.h"
#include "PublisherSketch.h"
#include "Timer.h"
#include "WiFiSettings.h"

const auto VERSION = MakeVersion("1.5");
constexpr auto SKETCH_NAME = "Power_Publisher";
constexpr auto PREF_NAMESPACE = SKETCH_NAME;
constexpr auto LEGACY_PREF_NAMESPACE = "ConsMeter";

// ----------- 16-Channel Configuration
constexpr size_t NUM_CT_INPUTS = PowerSensorBase::MAX_CHANNELS;
constexpr size_t MAX_LOCATION_LENGTH = 32;

constexpr uint8_t PIN_MUX_S0 = 2;
constexpr uint8_t PIN_MUX_S1 = 3;
constexpr uint8_t PIN_MUX_S2 = 4;
constexpr uint8_t PIN_MUX_S3 = 28;
constexpr uint8_t PIN_MUX_SIG = 22;
constexpr uint8_t PIN_AC_VOLTAGE = 21;

using CTCalibration::CT_TYPE_TABLE;
constexpr size_t NUM_AMP_OPTIONS = CTCalibration::NUM_CT_TYPES;
constexpr uint8_t CUSTOM_CT_TYPE_INDEX = 0xFF;

constexpr std::array<const char*, NUM_CT_INPUTS> DEFAULT_CT_LOCATIONS = {
   "1", "", "", "", "", "", "", "",
   "",  "", "", "", "", "", "", "",
};

constexpr std::array<float, NUM_CT_INPUTS> DEFAULT_CT_AMPS = {
   PowerSensorBase::CORE_30A,  PowerSensorBase::CORE_30A,  PowerSensorBase::CORE_100A, PowerSensorBase::CORE_100A,
   PowerSensorBase::CORE_30A,  PowerSensorBase::CORE_30A,  PowerSensorBase::CORE_30A,  PowerSensorBase::CORE_30A,
   PowerSensorBase::CORE_30A,  PowerSensorBase::CORE_30A,  PowerSensorBase::CORE_30A,  PowerSensorBase::CORE_30A,
   PowerSensorBase::CORE_30A,  PowerSensorBase::CORE_30A,  PowerSensorBase::CORE_30A,  PowerSensorBase::CORE_30A,
};

constexpr std::array<const char*, NUM_CT_INPUTS> PREF_KEY_CT_LOCATION = {
   "loc1", "loc2",  "loc3",  "loc4",  "loc5",  "loc6",  "loc7",  "loc8",
   "loc9", "loc10", "loc11", "loc12", "loc13", "loc14", "loc15", "loc16",
};

constexpr std::array<const char*, NUM_CT_INPUTS> PREF_KEY_CT_AMPS = {
   "amps1", "amps2",  "amps3",  "amps4",  "amps5",  "amps6",  "amps7",  "amps8",
   "amps9", "amps10", "amps11", "amps12", "amps13", "amps14", "amps15", "amps16",
};
constexpr std::array<const char*, NUM_CT_INPUTS> PREF_KEY_CT_TYPE = {
   "ctType1", "ctType2",  "ctType3",  "ctType4",  "ctType5",  "ctType6",  "ctType7",  "ctType8",
   "ctType9", "ctType10", "ctType11", "ctType12", "ctType13", "ctType14", "ctType15", "ctType16",
};
constexpr std::array<const char*, NUM_CT_INPUTS> PREF_KEY_CT_240V = {
   "v240_1", "v240_2",  "v240_3",  "v240_4",  "v240_5",  "v240_6",  "v240_7",  "v240_8",
   "v240_9", "v240_10", "v240_11", "v240_12", "v240_13", "v240_14", "v240_15", "v240_16",
};
constexpr std::array<const char*, NUM_CT_INPUTS> PREF_KEY_CH_PHASE_DEG = {
   "phDeg1", "phDeg2",  "phDeg3",  "phDeg4",  "phDeg5",  "phDeg6",  "phDeg7",  "phDeg8",
   "phDeg9", "phDeg10", "phDeg11", "phDeg12", "phDeg13", "phDeg14", "phDeg15", "phDeg16",
};
constexpr std::array<const char*, NUM_CT_INPUTS> PREF_KEY_CH_PHASE_MODE = {
   "phMod1", "phMod2",  "phMod3",  "phMod4",  "phMod5",  "phMod6",  "phMod7",  "phMod8",
   "phMod9", "phMod10", "phMod11", "phMod12", "phMod13", "phMod14", "phMod15", "phMod16",
};
constexpr std::array<const char*, NUM_CT_INPUTS> PREF_KEY_CH_POLARITY = {
   "chPol1", "chPol2",  "chPol3",  "chPol4",  "chPol5",  "chPol6",  "chPol7",  "chPol8",
   "chPol9", "chPol10", "chPol11", "chPol12", "chPol13", "chPol14", "chPol15", "chPol16",
};
constexpr auto PREF_KEY_VOLTAGE_SCALE = "vScale";
constexpr auto PREF_KEY_BOARD_PHASE_DEG = "phBoard";
constexpr auto PREF_KEY_BOARD_PHASE_MODE = "phMode";
constexpr auto PREF_KEY_TELEMETRY_ENABLED = "telEn";

constexpr uint16_t DISPLAY_UPDATE_INTERVAL_MS = 200; // 5 updates per second
constexpr uint16_t AUTO_CAL_SAVE_INTERVAL_M = 30;    // Throttle gradual phase-drift NVS saves to 30 minutes

// ----------- Telemetry Configuration
constexpr auto POWER_TOPIC_PREFIX = "Power";
constexpr size_t MAX_TOPIC_LENGTH = 64;
constexpr uint8_t POWER_DECIMALS = 0;
constexpr uint16_t TELEMETRY_PUBLISH_INTERVAL_MS = DISPLAY_UPDATE_INTERVAL_MS;

// ----------- Display Layout
constexpr uint8_t HEADER_SIZE = DEFAULT_HEADING_SIZE;
constexpr uint8_t TABLE_TEXT_SIZE = DEFAULT_TEXT_SIZE;
constexpr uint8_t FOOTER_SIZE = DEFAULT_FOOTER_SIZE;
constexpr size_t LOCATION_COL_WIDTH = 12;

std::array<char[MAX_LOCATION_LENGTH], NUM_CT_INPUTS> ctLocations = {};
std::array<char[8], NUM_CT_INPUTS> defaultChannelNames = {
   "1", "2",  "3",  "4",  "5",  "6",  "7",  "8",
   "9", "10", "11", "12", "13", "14", "15", "16",
};
std::array<float, NUM_CT_INPUTS> ctAmps = {};
std::array<uint8_t, NUM_CT_INPUTS> ctTypeIndices = {};
std::array<bool, NUM_CT_INPUTS> ct240V = {};
std::array<char[MAX_TOPIC_LENGTH], NUM_CT_INPUTS> channelTopics = {};
std::array<char[MAX_TOPIC_LENGTH], PowerSensorBase::MAX_DERIVED_CHANNELS> derivedPowerTopics = {};
char primaryTelemetryTopic[MAX_TOPIC_LENGTH] = "Power/1";
size_t primaryTelemetryChannel = 0;
bool telemetryTopicsResolved = false;
bool telemetryEnabled = true;
std::array<PowerSensorBase::ChannelConfig, NUM_CT_INPUTS> initialChannelConfig = {
   PowerSensorBase::ChannelConfig{ "1",  PowerSensorBase::CORE_30A },
   PowerSensorBase::ChannelConfig{ "2",  PowerSensorBase::CORE_30A },
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
MuxAdcPowerSensor powerSensor(
   PIN_MUX_S0,
   PIN_MUX_S1,
   PIN_MUX_S2,
   PIN_MUX_S3,
   PIN_MUX_SIG,
   PIN_AC_VOLTAGE,
   initialChannelConfig.data());

size_t interactivePromptIndex = 0;
void sendNextLocationPrompt();
void sendNextAmpsPrompt();
void sendSingleChannelLocationPrompt();
void sendSingleChannelAmpsPrompt();
void sendSingleChannelVoltagePrompt();

TelemetryConfig TELEMETRY_CONFIG = {
   .topic = primaryTelemetryTopic,
   .decimals = POWER_DECIMALS,
   .unit = TelemetryUnits::WATTS,
   .description = "Electrical power for this circuit",
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
/// Rebuilds the per-channel telemetry topics as "Power/<location>/<circuit>" for each
/// configured non-neutral CT (excluding "*White" neutral channels), registers any
/// auto-synthesized split-phase derived channels (such as "<Prefix>Black" from
/// "<Prefix>Red" + "<Prefix>White"), and updates the active TelemetryPublisher topic
/// immediately if running.
/// </summary>
///
void refreshTelemetryTopics()
{
   std::string deviceLocation = sketch.location().c_str();
   std::string topicPrefix = std::string(POWER_TOPIC_PREFIX) + "/";
   if (!deviceLocation.empty())
   {
      topicPrefix += deviceLocation + "/";
   }

   bool foundPrimary = false;
   for (size_t i = 0; i < NUM_CT_INPUTS; i++)
   {
      if (ctLocations[i][0] != '\0' && !powerSensor.isWhiteChannel(i))
      {
         std::string fullTopic = topicPrefix + ctLocations[i];
         strlcpy(channelTopics[i], fullTopic.c_str(), sizeof(channelTopics[i]));
         if (telemetryTopicsResolved && telemetryEnabled)
         {
            sketch.createTopic(channelTopics[i], TelemetryTopicMetadata{ .unit = TelemetryUnits::WATTS, .description = "Electrical power for this circuit", .decimals = POWER_DECIMALS });
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

   size_t numDerived = powerSensor.derivedChannelCount();
   for (size_t d = 0; d < PowerSensorBase::MAX_DERIVED_CHANNELS; d++)
   {
      if (d < numDerived && powerSensor.derivedChannelName(d)[0] != '\0')
      {
         std::string fullTopic = topicPrefix + powerSensor.derivedChannelName(d);
         strlcpy(derivedPowerTopics[d], fullTopic.c_str(), sizeof(derivedPowerTopics[d]));
         if (telemetryTopicsResolved && telemetryEnabled)
         {
            sketch.createTopic(derivedPowerTopics[d], TelemetryTopicMetadata{ .unit = TelemetryUnits::WATTS, .description = "Electrical power for this circuit", .decimals = POWER_DECIMALS });
         }
      }
      else
      {
         derivedPowerTopics[d][0] = '\0';
      }
   }

   if (!foundPrimary)
   {
      primaryTelemetryChannel = 0;
      std::string fallbackTopic = topicPrefix + "1";
      strlcpy(primaryTelemetryTopic, fallbackTopic.c_str(), sizeof(primaryTelemetryTopic));
      if (telemetryTopicsResolved && telemetryEnabled)
      {
         sketch.createTopic(primaryTelemetryTopic, TelemetryTopicMetadata{ .unit = TelemetryUnits::WATTS, .description = "Electrical power for this circuit", .decimals = POWER_DECIMALS });
      }
   }

   if (sketch.client() != nullptr && telemetryEnabled)
   {
      sketch.client()->setTopic(primaryTelemetryTopic);
   }
}

///
/// <summary>
/// Updates all physical CT channels on powerSensor so every channel is continuously sampled
/// for connection status, RMS current, and real power regardless of whether a location string
/// is assigned.
/// </summary>
///
void applyChannelConfigToSensor()
{
   for (size_t i = 0; i < NUM_CT_INPUTS; i++)
   {
      const char* name = ctLocations[i][0] != '\0' ? ctLocations[i] : defaultChannelNames[i];
      powerSensor.setChannelName(i, name);
      powerSensor.setCoreRating(ctAmps[i], i);
      powerSensor.setChannelPhaseOffsetDeg(i, ctTypeIndices[i] < NUM_AMP_OPTIONS ? CT_TYPE_TABLE[ctTypeIndices[i]].phaseOffsetDeg : 0.0f);
      powerSensor.setChannel240V(i, ct240V[i]);
   }

   refreshTelemetryTopics();
}

Timer autoCalSaveTimer(AUTO_CAL_SAVE_INTERVAL_M * 60UL * 1000UL);

///
/// <summary>
/// Persists auto-learned voltage-to-CT phase shift and solar channel polarities in Preferences.
/// </summary>
///
void saveAutoCalibration()
{
   Preferences prefs;
   prefs.begin(PREF_NAMESPACE, false);
   prefs.putFloat(PREF_KEY_BOARD_PHASE_DEG, powerSensor.phaseShiftDeg());
   prefs.putUChar(PREF_KEY_BOARD_PHASE_MODE, static_cast<uint8_t>(powerSensor.boardPhaseCalMode()));

   for (size_t i = 0; i < NUM_CT_INPUTS; i++)
   {
      if (powerSensor.isSolarChannel(i))
      {
         prefs.putFloat(PREF_KEY_CH_PHASE_DEG[i], powerSensor.channelPhaseShiftDeg(i));
         prefs.putUChar(PREF_KEY_CH_PHASE_MODE[i], static_cast<uint8_t>(powerSensor.solarChannelCalMode(i)));
         prefs.putChar(PREF_KEY_CH_POLARITY[i], powerSensor.channelPolarity(i));
      }
      else if (powerSensor.isWhiteChannel(i))
      {
         if (prefs.isKey(PREF_KEY_CH_PHASE_DEG[i]))
         {
            prefs.remove(PREF_KEY_CH_PHASE_DEG[i]);
         }
         if (prefs.isKey(PREF_KEY_CH_PHASE_MODE[i]))
         {
            prefs.remove(PREF_KEY_CH_PHASE_MODE[i]);
         }
         prefs.putChar(PREF_KEY_CH_POLARITY[i], powerSensor.channelPolarity(i));
      }
      else
      {
         if (prefs.isKey(PREF_KEY_CH_PHASE_DEG[i]))
         {
            prefs.remove(PREF_KEY_CH_PHASE_DEG[i]);
         }
         if (prefs.isKey(PREF_KEY_CH_PHASE_MODE[i]))
         {
            prefs.remove(PREF_KEY_CH_PHASE_MODE[i]);
         }
         if (prefs.isKey(PREF_KEY_CH_POLARITY[i]))
         {
            prefs.remove(PREF_KEY_CH_POLARITY[i]);
         }
      }
   }

   prefs.end();
   powerSensor.markCalibrationSaved();
   autoCalSaveTimer.reset();
}

///
/// <summary>
/// Migrates saved channel names, CT ratings, voltage calibration, and auto-learned phase/polarity
/// settings from the legacy "ConsMeter" Preferences namespace into "Power_Publisher" on first boot
/// after renaming the sketch.
/// </summary>
///
void migrateLegacyPreferencesIfNeeded()
{
   Preferences currentPrefs;
   if (currentPrefs.begin(PREF_NAMESPACE, true))
   {
      bool hasSaved = currentPrefs.isKey(PREF_KEY_VOLTAGE_SCALE) ||
                      currentPrefs.isKey(PREF_KEY_BOARD_PHASE_MODE);
      for (size_t i = 0; i < NUM_CT_INPUTS && !hasSaved; i++)
      {
         hasSaved = currentPrefs.isKey(PREF_KEY_CT_LOCATION[i]) ||
                    currentPrefs.isKey(PREF_KEY_CT_AMPS[i]);
      }
      currentPrefs.end();
      if (hasSaved)
      {
         return;
      }
   }

   Preferences legacyPrefs;
   if (!legacyPrefs.begin(LEGACY_PREF_NAMESPACE, true))
   {
      return;
   }

   Preferences dstPrefs;
   if (dstPrefs.begin(PREF_NAMESPACE, false))
   {
      if (legacyPrefs.isKey(PREF_KEY_VOLTAGE_SCALE))
      {
         dstPrefs.putFloat(PREF_KEY_VOLTAGE_SCALE, legacyPrefs.getFloat(PREF_KEY_VOLTAGE_SCALE));
      }
      if (legacyPrefs.isKey(PREF_KEY_BOARD_PHASE_DEG))
      {
         dstPrefs.putFloat(PREF_KEY_BOARD_PHASE_DEG, legacyPrefs.getFloat(PREF_KEY_BOARD_PHASE_DEG));
      }
      if (legacyPrefs.isKey(PREF_KEY_BOARD_PHASE_MODE))
      {
         dstPrefs.putUChar(PREF_KEY_BOARD_PHASE_MODE, legacyPrefs.getUChar(PREF_KEY_BOARD_PHASE_MODE));
      }

      for (size_t i = 0; i < NUM_CT_INPUTS; i++)
      {
         if (legacyPrefs.isKey(PREF_KEY_CT_LOCATION[i]))
         {
            dstPrefs.putString(PREF_KEY_CT_LOCATION[i], legacyPrefs.getString(PREF_KEY_CT_LOCATION[i]));
         }
         if (legacyPrefs.isKey(PREF_KEY_CT_AMPS[i]))
         {
            dstPrefs.putFloat(PREF_KEY_CT_AMPS[i], legacyPrefs.getFloat(PREF_KEY_CT_AMPS[i]));
         }
         if (legacyPrefs.isKey(PREF_KEY_CH_PHASE_DEG[i]))
         {
            dstPrefs.putFloat(PREF_KEY_CH_PHASE_DEG[i], legacyPrefs.getFloat(PREF_KEY_CH_PHASE_DEG[i]));
         }
         if (legacyPrefs.isKey(PREF_KEY_CH_PHASE_MODE[i]))
         {
            dstPrefs.putUChar(PREF_KEY_CH_PHASE_MODE[i], legacyPrefs.getUChar(PREF_KEY_CH_PHASE_MODE[i]));
         }
         if (legacyPrefs.isKey(PREF_KEY_CH_POLARITY[i]))
         {
            dstPrefs.putChar(PREF_KEY_CH_POLARITY[i], legacyPrefs.getChar(PREF_KEY_CH_POLARITY[i]));
         }
      }

      dstPrefs.end();
   }

   legacyPrefs.end();
}

///
/// <summary>
/// Loads persisted per-input locations, CT current ratings, AC voltage calibration scale,
/// and auto-learned phase/solar polarity from Preferences and configures powerSensor.
/// </summary>
///
void loadChannelConfig()
{
   migrateLegacyPreferencesIfNeeded();

   Preferences prefs;
   bool hasPrefs = prefs.begin(PREF_NAMESPACE, true);

   for (size_t i = 0; i < NUM_CT_INPUTS; i++)
   {
      String savedLoc = (hasPrefs && prefs.isKey(PREF_KEY_CT_LOCATION[i]))
         ? prefs.getString(PREF_KEY_CT_LOCATION[i], DEFAULT_CT_LOCATIONS[i])
         : String(DEFAULT_CT_LOCATIONS[i]);
      strlcpy(ctLocations[i], savedLoc.c_str(), sizeof(ctLocations[i]));

      float savedAmps = (hasPrefs && prefs.isKey(PREF_KEY_CT_AMPS[i]))
         ? prefs.getFloat(PREF_KEY_CT_AMPS[i], DEFAULT_CT_AMPS[i])
         : DEFAULT_CT_AMPS[i];
      ctAmps[i] = savedAmps > 0.0f ? savedAmps : DEFAULT_CT_AMPS[i];
      ctTypeIndices[i] = CUSTOM_CT_TYPE_INDEX;
      if (hasPrefs && prefs.isKey(PREF_KEY_CT_TYPE[i]))
      {
         uint8_t savedType = prefs.getUChar(PREF_KEY_CT_TYPE[i], CUSTOM_CT_TYPE_INDEX);
         if (savedType < NUM_AMP_OPTIONS)
         {
            ctTypeIndices[i] = savedType;
            ctAmps[i] = CT_TYPE_TABLE[savedType].ampsPerVolt;
         }
      }
      if (ctTypeIndices[i] == CUSTOM_CT_TYPE_INDEX)
      {
         for (size_t type = 0; type < NUM_AMP_OPTIONS; type++)
         {
            if (std::fabs(CT_TYPE_TABLE[type].ampsPerVolt - ctAmps[i]) < 0.01f)
            {
               ctTypeIndices[i] = static_cast<uint8_t>(type);
               break;
            }
         }
      }

      ct240V[i] = (hasPrefs && prefs.isKey(PREF_KEY_CT_240V[i]))
         ? prefs.getBool(PREF_KEY_CT_240V[i], false)
         : false;
   }

   if (hasPrefs && prefs.isKey(PREF_KEY_VOLTAGE_SCALE))
   {
      float savedVScale = prefs.getFloat(PREF_KEY_VOLTAGE_SCALE, MuxAdcPowerSensor::DEFAULT_VOLTAGE_SCALE);
      if (savedVScale > 0.0f)
      {
         powerSensor.setVoltageScale(savedVScale);
      }
   }

   telemetryEnabled = (hasPrefs && prefs.isKey(PREF_KEY_TELEMETRY_ENABLED))
      ? prefs.getBool(PREF_KEY_TELEMETRY_ENABLED, true)
      : true;
   sketch.setTelemetryEnabled(telemetryEnabled);

   applyChannelConfigToSensor();

   if (hasPrefs && prefs.isKey(PREF_KEY_BOARD_PHASE_DEG) && prefs.isKey(PREF_KEY_BOARD_PHASE_MODE))
   {
      float savedBoardPhase = prefs.getFloat(PREF_KEY_BOARD_PHASE_DEG, MuxAdcPowerSensor::DEFAULT_PHASE_SHIFT_DEG);
      uint8_t savedBoardModeRaw = prefs.getUChar(PREF_KEY_BOARD_PHASE_MODE, 0);
      MuxAdcPowerSensor::PhaseCalMode savedBoardMode = savedBoardModeRaw == 1
         ? MuxAdcPowerSensor::PhaseCalMode::NIGHT_AUTO
         : MuxAdcPowerSensor::PhaseCalMode::NOMINAL;
      powerSensor.restoreBoardPhaseCalibration(savedBoardPhase, savedBoardMode);
   }

   for (size_t i = 0; i < NUM_CT_INPUTS; i++)
   {
      if (!powerSensor.isSolarChannel(i) && !powerSensor.isWhiteChannel(i))
      {
         continue;
      }

      float savedChPhase = (hasPrefs && prefs.isKey(PREF_KEY_CH_PHASE_DEG[i]))
         ? prefs.getFloat(PREF_KEY_CH_PHASE_DEG[i], MuxAdcPowerSensor::DEFAULT_PHASE_SHIFT_DEG)
         : MuxAdcPowerSensor::DEFAULT_PHASE_SHIFT_DEG;
      uint8_t savedChModeRaw = (hasPrefs && prefs.isKey(PREF_KEY_CH_PHASE_MODE[i]))
         ? prefs.getUChar(PREF_KEY_CH_PHASE_MODE[i], 0)
         : 0;
      MuxAdcPowerSensor::PhaseCalMode savedChMode = savedChModeRaw == 1
         ? MuxAdcPowerSensor::PhaseCalMode::NIGHT_AUTO
         : MuxAdcPowerSensor::PhaseCalMode::NOMINAL;
      int8_t savedPolarity = (hasPrefs && prefs.isKey(PREF_KEY_CH_POLARITY[i]))
         ? prefs.getChar(PREF_KEY_CH_POLARITY[i], 0)
         : 0;
      powerSensor.restoreChannelAutoCalibration(i, savedChPhase, savedChMode, savedPolarity);
   }

   if (hasPrefs)
   {
      prefs.end();
   }
}

///
/// <summary>
/// Persists a single CT input's location string (empty string disables telemetry for that channel).
/// </summary>
/// <param name="inputIndex">Channel index (0 to 15).</param>
/// <param name="loc">Location string (or empty to disable).</param>
///
void saveCtLocation(size_t inputIndex, const char* loc)
{
   bool changed = strcmp(ctLocations[inputIndex], loc) != 0;

   Preferences prefs;
   prefs.begin(PREF_NAMESPACE, false);
   prefs.putString(PREF_KEY_CT_LOCATION[inputIndex], loc);
   prefs.end();

   if (changed)
   {
      powerSensor.resetChannelAutoCalibration(inputIndex);
   }

   strlcpy(ctLocations[inputIndex], loc, sizeof(ctLocations[inputIndex]));
   applyChannelConfigToSensor();

   if (changed)
   {
      powerSensor.resetChannelAutoCalibration(inputIndex);
      saveAutoCalibration();
   }
}

///
/// <summary>
/// Converts a user-entered nominal CT amp rating (20, 30, 100, 100R, 500) into its hardware
/// Amps-per-Volt scale, or passes custom calibration values through unchanged.
/// </summary>
/// <param name="enteredAmps">Nominal CT rating or custom Amps-per-Volt value.</param>
/// <returns>Calibration scale in Amps per 1.0V RMS.</returns>
///
float resolveCtAmpsPerVolt(float enteredAmps)
{
   for (const auto& option : CT_TYPE_TABLE)
   {
      if (std::fabs(enteredAmps - option.ratedAmps) < 0.05f)
      {
         return option.ampsPerVolt;
      }
   }

   return enteredAmps;
}

///
/// <summary>
/// Converts a stored Amps-per-Volt scale back to its nominal CT amp label (20, 30, 100, 100R, 500)
/// for GetConfig and shell prompts, or rounds custom calibration values to the nearest integer.
/// </summary>
/// <param name="ampsPerVolt">Stored Amps-per-Volt scale.</param>
/// <returns>Display integer current rating in Amps.</returns>
///
int displayAmpsForScale(float ampsPerVolt)
{
   for (const auto& option : CT_TYPE_TABLE)
   {
      if (std::fabs(ampsPerVolt - option.ampsPerVolt) < 0.5f)
      {
         return option.ratedAmps;
      }
   }

   return static_cast<int>(ampsPerVolt + 0.5f);
}

///
/// <summary>
/// Persists a single CT input's current rating (Amps per 1.0V RMS) in Preferences.
/// </summary>
/// <param name="inputIndex">Channel index (0 to 15).</param>
/// <param name="amps">Calibration scale in Amps per 1.0V RMS.</param>
///
void saveCtAmps(size_t inputIndex, float amps)
{
   bool changed = std::fabs(ctAmps[inputIndex] - amps) >= 0.01f;

   Preferences prefs;
   prefs.begin(PREF_NAMESPACE, false);
   prefs.putFloat(PREF_KEY_CT_AMPS[inputIndex], amps);
   uint8_t matchedType = CUSTOM_CT_TYPE_INDEX;
   for (size_t type = 0; type < NUM_AMP_OPTIONS; type++)
   {
      if (std::fabs(CT_TYPE_TABLE[type].ampsPerVolt - amps) < 0.01f)
      {
         matchedType = static_cast<uint8_t>(type);
         break;
      }
   }
   prefs.putUChar(PREF_KEY_CT_TYPE[inputIndex], matchedType);
   prefs.end();

   ctAmps[inputIndex] = amps;
   ctTypeIndices[inputIndex] = matchedType;
   applyChannelConfigToSensor();

   if (changed)
   {
      powerSensor.resetChannelAutoCalibration(inputIndex);
      saveAutoCalibration();
   }
}

///
/// <summary>
/// Persists and applies one CT type table entry to a channel.
/// </summary>
/// <param name="inputIndex">Channel index (0 to 15).</param>
/// <param name="typeIndex">Index in CT_TYPE_TABLE.</param>
///
void saveCtPreset(size_t inputIndex, size_t typeIndex)
{
   bool changed = ctTypeIndices[inputIndex] != typeIndex;

   Preferences prefs;
   prefs.begin(PREF_NAMESPACE, false);
   prefs.putFloat(PREF_KEY_CT_AMPS[inputIndex], CT_TYPE_TABLE[typeIndex].ampsPerVolt);
   prefs.putUChar(PREF_KEY_CT_TYPE[inputIndex], static_cast<uint8_t>(typeIndex));
   prefs.end();

   ctAmps[inputIndex] = CT_TYPE_TABLE[typeIndex].ampsPerVolt;
   ctTypeIndices[inputIndex] = static_cast<uint8_t>(typeIndex);
   applyChannelConfigToSensor();

   if (changed)
   {
      powerSensor.resetChannelAutoCalibration(inputIndex);
      saveAutoCalibration();
   }
}

///
/// <summary>
/// Persists a single CT input's 120V/240V circuit voltage mode in Preferences and applies it immediately.
/// </summary>
/// <param name="inputIndex">Channel index (0 to 15).</param>
/// <param name="is240V">True for a single-CT 240V circuit (2x power scaling); false for 120V.</param>
///
void saveCt240V(size_t inputIndex, bool is240V)
{
   Preferences prefs;
   prefs.begin(PREF_NAMESPACE, false);
   prefs.putBool(PREF_KEY_CT_240V[inputIndex], is240V);
   prefs.end();

   ct240V[inputIndex] = is240V;
   powerSensor.setChannel240V(inputIndex, is240V);
}

///
/// <summary>
/// Parses a channel voltage mode string ("120", "120V", "240", or "240V", case-insensitive).
/// </summary>
/// <param name="text">Trimmed input string.</param>
/// <param name="outIs240V">Receives true for 240V or false for 120V when parsing succeeds.</param>
/// <returns>True if the text was a recognized voltage mode; false otherwise.</returns>
///
bool tryParseChannelVoltage(const char* text, bool* outIs240V)
{
   ASSERT(outIs240V != nullptr);

   if (strcasecmp(text, "120") == 0 || strcasecmp(text, "120v") == 0)
   {
      *outIs240V = false;
      return true;
   }
   if (strcasecmp(text, "240") == 0 || strcasecmp(text, "240v") == 0)
   {
      *outIs240V = true;
      return true;
   }
   return false;
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
/// <param name="inputIndex">Channel index being edited (0 to 15).</param>
/// <param name="loc">Candidate circuit name.</param>
/// <returns>Conflicting channel index (0 to 15), or -1 if unique.</returns>
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
/// Handles the shell answer for a single-channel location prompt ("sc <1-16>").
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
/// Sends the interactive shell prompt for a single channel's location ("sc <1-16>").
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
/// Handles the shell answer for a single-channel CT amp rating prompt ("sc <1-16>").
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
      rating = resolveCtAmpsPerVolt(parsed);
      saveCtAmps(interactivePromptIndex, rating);
   }

   sendSingleChannelVoltagePrompt();
}

///
/// <summary>
/// Handles the shell answer for a single-channel 120V/240V prompt ("sc <1-16>").
/// Pressing Return (empty input) keeps the previous voltage setting.
/// </summary>
/// <param name="line">User-entered voltage line from the shell ("120" or "240").</param>
///
void onSingleChannelVoltageAnswer(const char* line)
{
   String input = String(line);
   input.trim();

   if (input.length() > 0)
   {
      bool is240V = false;
      if (!tryParseChannelVoltage(input.c_str(), &is240V))
      {
         DeviceServerClient::shell().println("Invalid voltage (must be 120 or 240).");
         sendSingleChannelVoltagePrompt();
         return;
      }
      saveCt240V(interactivePromptIndex, is240V);
   }

   std::string locSummary = ctLocations[interactivePromptIndex][0] != '\0' ? ctLocations[interactivePromptIndex] : "(unused)";
   std::string summary =
      "l" + std::to_string(interactivePromptIndex + 1) + "=" + locSummary +
      ", a" + std::to_string(interactivePromptIndex + 1) + "=" + std::to_string(displayAmpsForScale(ctAmps[interactivePromptIndex])) +
      ", v" + std::to_string(interactivePromptIndex + 1) + "=" + (ct240V[interactivePromptIndex] ? "240V" : "120V");
   DeviceServerClient::respond(summary.c_str());
}

///
/// <summary>
/// Sends the interactive shell prompt for a single channel's 120V/240V circuit voltage ("sc <1-16>").
/// </summary>
///
void sendSingleChannelVoltagePrompt()
{
   const char* currentVolts = ct240V[interactivePromptIndex] ? "240" : "120";
   String label = "Enter v" + String(interactivePromptIndex + 1);
   if (ctLocations[interactivePromptIndex][0] != '\0')
   {
      label += " (" + String(ctLocations[interactivePromptIndex]) + ")";
   }
   label += " (120/240) [" + String(currentVolts) + "]: ";
   DeviceServerClient::shell().print(label);
   DeviceServerClient::shell().flushOutput();
   DeviceServerClient::promptShellLine(onSingleChannelVoltageAnswer);
}

///
/// <summary>
/// Sends the interactive shell prompt for a single channel's CT amp rating ("sc <1-16>").
/// </summary>
///
void sendSingleChannelAmpsPrompt()
{
   int currentAmps = displayAmpsForScale(ctAmps[interactivePromptIndex]);
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
/// Handles the shell answer for one of the 16 location prompts during interactive SetConfig.
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
/// Sends the interactive shell prompt for CT location 1..16.
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
/// Handles the shell answer for one of the 16 CT amp rating prompts during interactive SetConfig.
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
      rating = resolveCtAmpsPerVolt(parsed);
   }

   saveCtAmps(interactivePromptIndex, rating);
   sketch.addInteractiveSummaryLine(
      "a" + std::to_string(interactivePromptIndex + 1),
      std::to_string(displayAmpsForScale(rating)));

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
/// Sends the interactive shell prompt for CT amp rating 1..16 (skipping unused inputs).
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

   int currentAmps = displayAmpsForScale(ctAmps[interactivePromptIndex]);
   String label = "Enter a" + String(interactivePromptIndex + 1) + " (" + String(ctLocations[interactivePromptIndex]) + ") [" + String(currentAmps) + "]: ";
   DeviceServerClient::shell().print(label);
   DeviceServerClient::shell().flushOutput();
   DeviceServerClient::promptShellLine(onInteractiveAmpsItemAnswer);
}

///
/// <summary>
/// Starts the 16-item location and amps prompt sequence during interactive SetConfig.
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
/// the 16 CT locations (l1..l16), 16 CT amp ratings (a1..a16), 16 circuit voltages
/// (v1..v16), vScale, phase calibration, and telemetry state.
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
          << "\na" << (i + 1) << "=" << displayAmpsForScale(ctAmps[i])
          << "\nv" << (i + 1) << "=" << (ct240V[i] ? "240V" : "120V");
   }
   out << std::fixed;
   out.precision(1);
   out << "\nvScale=" << powerSensor.voltageScale();
   out.precision(2);
   out << "\nphase=" << powerSensor.phaseShiftDeg() << " deg (" << powerSensor.phaseCalibrationMode() << ")";
   out << "\ntelemetry=" << (telemetryEnabled ? "on" : "off");
   return out.str();
}

void refreshTelemetryButtonDisplay();

///
/// <summary>
/// Persists the telemetry enabled/disabled state in Preferences and applies it immediately.
/// When disabled, no topic creations or samples are sent to the telemetry server; when
/// re-enabled, unsent stale topics are cleared and the current channel topics are registered.
/// </summary>
/// <param name="enabled">True to enable telemetry publishing; false to suppress it.</param>
///
void saveTelemetryEnabled(bool enabled)
{
   Preferences prefs;
   prefs.begin(PREF_NAMESPACE, false);
   prefs.putBool(PREF_KEY_TELEMETRY_ENABLED, enabled);
   prefs.end();

   telemetryEnabled = enabled;
   if (enabled)
   {
      if (sketch.client() != nullptr)
      {
         sketch.client()->clearUnsentTopics();
      }
      refreshTelemetryTopics();
      sketch.setTelemetryEnabled(true);
   }
   else
   {
      sketch.setTelemetryEnabled(false);
   }

   refreshTelemetryButtonDisplay();
}

///
/// <summary>
/// Persists the ZMPT101B AC voltage calibration scale in Preferences and applies it immediately.
/// </summary>
/// <param name="scale">AC line volts per 1.0V RMS at ZMPT101B OUT.</param>
///
void saveVoltageScale(float scale)
{
   Preferences prefs;
   prefs.begin(PREF_NAMESPACE, false);
   prefs.putFloat(PREF_KEY_VOLTAGE_SCALE, scale);
   prefs.end();

   powerSensor.setVoltageScale(scale);
}

///
/// <summary>
/// Handles extra SetConfig keys (applied immediately without requiring a restart):
/// - Channel number "1".."16" with no value: interactively prompts for that channel's location, amps, and voltage
/// - Direct keys "l1".."l16" (or "location1".."location16"; use "-", "none", or "off" to clear)
/// - Direct keys "a1".."a16" (or "amps1".."amps16")
/// - Direct keys "v1".."v16" (or "volts1".."volts16"; "120" or "240" for single-CT 240V circuits)
/// - "vcal" with actual AC line voltage (e.g. "vcal=120.1") or "vscale" with raw V/V scale
/// - "phase=auto" to clear saved phase/polarity learning and immediately re-learn automatically
/// - "telemetry=on|off" to enable or disable telemetry publishing
/// </summary>
/// <param name="key">Config key passed to SetConfig.</param>
/// <param name="value">New value string.</param>
/// <param name="result">Receives the confirmation or error message.</param>
/// <returns>True if the key was recognized; false otherwise.</returns>
///
bool trySetExtraConfig(const std::string& key, const std::string& value, std::string& result)
{
   if (strcasecmp(key.c_str(), "telemetry") == 0)
   {
      if (strcasecmp(value.c_str(), "on") == 0 || strcasecmp(value.c_str(), "true") == 0 || value == "1")
      {
         saveTelemetryEnabled(true);
         result = "telemetry=on";
         return true;
      }
      if (strcasecmp(value.c_str(), "off") == 0 || strcasecmp(value.c_str(), "false") == 0 || value == "0")
      {
         saveTelemetryEnabled(false);
         result = "telemetry=off";
         return true;
      }

      result = "Error: telemetry must be on or off";
      return true;
   }

   if (strcasecmp(key.c_str(), "phase") == 0)
   {
      if (strcasecmp(value.c_str(), "auto") != 0 && strcasecmp(value.c_str(), "reset") != 0)
      {
         result = "Error: phase calibration is automatic; use phase=auto to reset and re-learn";
         return true;
      }

      powerSensor.resetAllAutoCalibration();
      saveAutoCalibration();
      result = "Reset phase and CT polarity to automatic learning";
      return true;
   }

   if (strcasecmp(key.c_str(), "vcal") == 0)
   {
      float actualVolts = std::strtof(value.c_str(), nullptr);
      if (actualVolts <= 0.0f)
      {
         result = "Error: vcal must be the measured AC line voltage in volts (e.g. 120.1)";
         return true;
      }
      if (!powerSensor.voltageConnected() || powerSensor.voltageSensorRmsVolts() <= 0.0f)
      {
         result = "Error: AC voltage sensor is not currently detecting an AC signal";
         return true;
      }

      float newScale = actualVolts / powerSensor.voltageSensorRmsVolts();
      saveVoltageScale(newScale);

      std::ostringstream msg;
      msg << std::fixed;
      msg.precision(1);
      msg << "Calibrated AC voltage to " << actualVolts << " V (vScale=" << newScale << ")";
      result = msg.str();
      return true;
   }

   if (strcasecmp(key.c_str(), "vscale") == 0)
   {
      float newScale = strcasecmp(value.c_str(), "default") == 0
         ? MuxAdcPowerSensor::DEFAULT_VOLTAGE_SCALE
         : std::strtof(value.c_str(), nullptr);
      if (newScale <= 0.0f)
      {
         result = "Error: vscale must be > 0 (or 'default' for 1200.0)";
         return true;
      }

      saveVoltageScale(newScale);
      std::ostringstream msg;
      msg << std::fixed;
      msg.precision(1);
      msg << "vScale=" << newScale;
      result = msg.str();
      return true;
   }

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

         float entered = std::strtof(value.c_str(), nullptr);
         if (entered <= 0.0f)
         {
            result = "Error: " + shortAmpsKey + " must be a positive CT rating (e.g. 20, 30, 100, 100R, 500)";
            return true;
         }

         float rating = resolveCtAmpsPerVolt(entered);
         saveCtAmps(i, rating);
         result = shortAmpsKey + "=" + std::to_string(displayAmpsForScale(rating));
         return true;
      }

      std::string shortVoltsKey = "v" + std::to_string(i + 1);
      std::string longVoltsKey = "volts" + std::to_string(i + 1);
      if (strcasecmp(key.c_str(), shortVoltsKey.c_str()) == 0 || strcasecmp(key.c_str(), longVoltsKey.c_str()) == 0)
      {
         bool is240V = false;
         if (!tryParseChannelVoltage(value.c_str(), &is240V))
         {
            result = "Error: " + shortVoltsKey + " must be 120 or 240";
            return true;
         }

         saveCt240V(i, is240V);
         result = shortVoltsKey + "=" + (is240V ? "240V" : "120V");
         return true;
      }
   }

   return false;
}

constexpr float LINE_VOLTAGE_DISPLAY_HYSTERESIS_V = 0.08f;
Format rowIndexFormat("## ", Format::Alignment::RIGHT);
Format rowLocationFormat(LOCATION_COL_WIDTH, Format::Alignment::LEFT);
Format rowAmpsFormat(" ###.# A", Format::Alignment::RIGHT);
Format rowWattsFormat(" ##### W", Format::Alignment::RIGHT);
Format lineVoltageFormat("###.# V", Format::Alignment::RIGHT);
DisplayValue lineVoltageField(
   &sketch.arduino,
   lineVoltageFormat,
   TABLE_TEXT_SIZE,
   DisplayValue::Alignment::RIGHT);
float displayedLineVoltage = NAN;

constexpr int16_t TELEMETRY_BUTTON_HEIGHT = 52;
constexpr int16_t TELEMETRY_BUTTON_GAP = 10;
constexpr int16_t TELEMETRY_BUTTON_MARGIN_X = 8;
constexpr int16_t TELEMETRY_BUTTON_CORNER_RADIUS = 8;

Timer updateTimer(DISPLAY_UPDATE_INTERVAL_MS);
int16_t valueStartY = 0;
size_t displayedDerivedCount = 0;
bool displayReady = false;
Rect16 telemetryButtonRect;

///
/// <summary>
/// Computes the rectangle for the telemetry enable/disable toggle button sitting
/// directly above the bottom location/version footer.
/// </summary>
/// <returns>Bounding rectangle of the telemetry toggle button.</returns>
///
Rect16 computeTelemetryButtonRect()
{
   int16_t footerHeight = sketch.arduino.charH(FOOTER_SIZE);
   int16_t top = sketch.arduino.height() - footerHeight - TELEMETRY_BUTTON_GAP - TELEMETRY_BUTTON_HEIGHT;
   return Rect16(
      TELEMETRY_BUTTON_MARGIN_X,
      top,
      sketch.arduino.width() - 2 * TELEMETRY_BUTTON_MARGIN_X,
      TELEMETRY_BUTTON_HEIGHT);
}

///
/// <summary>
/// Draws the telemetry enable/disable toggle button at the bottom of the main display.
/// </summary>
///
void drawTelemetryButton()
{
   telemetryButtonRect = computeTelemetryButtonRect();
   const Color bg = telemetryEnabled ? Color::DARKGREEN : Color::DARKRED;
   const char* label = telemetryEnabled ? "Telemetry: ON" : "Telemetry: OFF";

   sketch.arduino.display.fillRoundRect(
      telemetryButtonRect.left(),
      telemetryButtonRect.top(),
      telemetryButtonRect.width,
      telemetryButtonRect.height,
      TELEMETRY_BUTTON_CORNER_RADIUS,
      static_cast<uint16_t>(bg));
   sketch.arduino.display.drawRoundRect(
      telemetryButtonRect.left(),
      telemetryButtonRect.top(),
      telemetryButtonRect.width,
      telemetryButtonRect.height,
      TELEMETRY_BUTTON_CORNER_RADIUS,
      static_cast<uint16_t>(Color::WHITE));

   sketch.arduino.setTextSize(TABLE_TEXT_SIZE);
   const int16_t tw = sketch.arduino.display.textWidth(label);
   const int16_t th = sketch.arduino.charH(TABLE_TEXT_SIZE);
   sketch.arduino.display.setTextColor(static_cast<uint16_t>(Color::WHITE), static_cast<uint16_t>(bg));
   sketch.arduino.display.setCursor(
      telemetryButtonRect.left() + (telemetryButtonRect.width - tw) / 2,
      telemetryButtonRect.top() + (telemetryButtonRect.height - th) / 2);
   sketch.arduino.display.print(label);
}

///
/// <summary>
/// Computes the vertical start coordinate to center the physical + derived channel rows
/// between the top heading and the bottom telemetry toggle button.
/// </summary>
/// <returns>Top Y pixel coordinate for the first table row.</returns>
///
int16_t computeTableStartY()
{
   int16_t headingHeight = sketch.arduino.charH(HEADER_SIZE);
   int16_t bottomLimit = computeTelemetryButtonRect().top() - TELEMETRY_BUTTON_GAP;
   size_t totalRows = NUM_CT_INPUTS + powerSensor.derivedChannelCount();
   int16_t valuesHeight = static_cast<int16_t>(totalRows) * sketch.arduino.charH(TABLE_TEXT_SIZE);
   int16_t availableHeight = bottomLimit - headingHeight;
   int16_t startY = headingHeight + (availableHeight - valuesHeight) / 2;
   return startY < headingHeight ? headingHeight : startY;
}

///
/// <summary>
/// Draws the footer row with the device location in the lower left and firmware version in the lower right.
/// </summary>
///
void drawFooter()
{
   sketch.arduino.setTextSize(FOOTER_SIZE);
   sketch.arduino.setCursor(0, sketch.arduino.height() - sketch.arduino.charH());
   sketch.arduino.print(sketch.location(), Color::CYAN);
   sketch.arduino.printR(VERSION, Color::LIGHTGRAY);
}

// ----------- Gear icon & Touch Settings UI
constexpr uint16_t DISPLAY_DIM_TIMEOUT_S = 60;
constexpr float DISPLAY_DIM_LEVEL = 0.10f;
constexpr uint16_t SETTINGS_PROMPT_TIMEOUT_M = 10;

constexpr int16_t GEAR_RADIUS = 16;
constexpr int16_t GEAR_MARGIN = 6;
constexpr int16_t GEAR_TAP_SIZE = 56;

constexpr int16_t SETTINGS_MARGIN_X = 8;
constexpr int16_t SETTINGS_GAP = 8;
constexpr int16_t SETTINGS_CORNER_RADIUS = 8;
constexpr int16_t SETTINGS_CH_BOX_HEIGHT = 68;
constexpr size_t CHANNEL_GRID_COLS = 4;
constexpr size_t CHANNEL_GRID_ROWS = NUM_CT_INPUTS / CHANNEL_GRID_COLS;
constexpr size_t AMP_GRID_COLS = 3;
constexpr size_t AMP_GRID_ROWS = (NUM_AMP_OPTIONS + AMP_GRID_COLS - 1) / AMP_GRID_COLS;
constexpr size_t NUM_VOLTAGE_OPTIONS = 2;

Timer displayDimTimer(DISPLAY_DIM_TIMEOUT_S * 1000UL);
bool displayDimmed = false;
bool showingSettings = false;
bool wasTouching = false;
size_t selectedSettingsChannel = 0;
std::array<Rect16, NUM_CT_INPUTS> channelBoxRects = {};
std::array<bool, NUM_CT_INPUTS> settingsChannelConnected = {};
Rect16 labelBoxRect;
std::array<Rect16, NUM_AMP_OPTIONS> ampBoxRects = {};
std::array<Rect16, NUM_VOLTAGE_OPTIONS> voltageBoxRects = {};
Rect16 deviceLocationBoxRect;
Rect16 okBoxRect;

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
/// Determines whether a touch is within the upper-right gear icon tap target.
/// </summary>
/// <param name="x">Touch x coordinate.</param>
/// <param name="y">Touch y coordinate.</param>
/// <returns>True if the touch is within the gear tap area.</returns>
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
/// Redraws the telemetry toggle button if the main display is currently visible.
/// </summary>
///
void refreshTelemetryButtonDisplay()
{
   if (displayReady && !showingSettings)
   {
      drawTelemetryButton();
   }
}

///
/// <summary>
/// Draws the static frame (heading, gear icon, telemetry toggle button, and location/version footer)
/// for the main power view.
/// </summary>
///
void drawMainFrame()
{
   sketch.arduino.clearDisplay();
   sketch.arduino.setTextSize(HEADER_SIZE);
   sketch.arduino.setCursor(0, 0);
   sketch.arduino.println("Power", Color::HEADING);
   drawGear();
   drawTelemetryButton();
   drawFooter();
   displayedLineVoltage = NAN;
   displayedDerivedCount = powerSensor.derivedChannelCount();
   valueStartY = computeTableStartY();
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
/// Draws a single channel box in the settings 4x4 grid. Border and channel number are white
/// when a CT is physically plugged in, or gray when unplugged, regardless of label assignment.
/// </summary>
/// <param name="index">Channel index (0 to 15).</param>
///
void drawSettingsChannelBox(size_t index)
{
   const Rect16& rect = channelBoxRects[index];
   const bool selected = (index == selectedSettingsChannel);
   const bool configured = (ctLocations[index][0] != '\0');
   const bool connected = powerSensor.isPhysicallyConnected(index);
   settingsChannelConnected[index] = connected;

   const Color bg = selected ? Color::DARKGREEN : (configured ? Color::DARKBLUE : Color::BLACK);
   const Color fg = connected ? Color::WHITE : Color::GRAY;

   sketch.arduino.display.fillRoundRect(rect.left(), rect.top(), rect.width, rect.height, SETTINGS_CORNER_RADIUS, static_cast<uint16_t>(bg));
   sketch.arduino.display.drawRoundRect(rect.left(), rect.top(), rect.width, rect.height, SETTINGS_CORNER_RADIUS, static_cast<uint16_t>(fg));
   if (selected)
   {
      sketch.arduino.display.drawRoundRect(rect.left() + 1, rect.top() + 1, rect.width - 2, rect.height - 2, SETTINGS_CORNER_RADIUS, static_cast<uint16_t>(fg));
   }

   sketch.arduino.setTextSize(TABLE_TEXT_SIZE);
   const int16_t chNumH = sketch.arduino.charH(TABLE_TEXT_SIZE);
   String numStr = String(index + 1);
   sketch.arduino.display.setTextColor(static_cast<uint16_t>(fg), static_cast<uint16_t>(bg));
   const int16_t tw = sketch.arduino.display.textWidth(numStr.c_str());
   sketch.arduino.display.setCursor(rect.left() + (rect.width - tw) / 2, rect.top() + (rect.height - chNumH) / 2);
   sketch.arduino.display.print(numStr.c_str());
}

///
/// <summary>
/// Draws the touch configuration view:
/// - Upper portion: 16 channel squares (4x4 grid) with plugged-in CTs shown in white and
///   the selected channel highlighted.
/// - Middle portion: the selected channel's editable location label, CT type preset buttons,
///   and 120V/240V circuit voltage toggle buttons.
/// - Lower portion: device location box and an "OK" button to return to the main power view.
/// </summary>
///
void drawSettingsView()
{
   sketch.arduino.clearDisplay();
   sketch.arduino.setTextSize(HEADER_SIZE);
   sketch.arduino.setCursor(0, 0);
   sketch.arduino.println("Configure", Color::HEADING);

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
   const int16_t chBoxH = SETTINGS_CH_BOX_HEIGHT;

   for (size_t i = 0; i < NUM_CT_INPUTS; i++)
   {
      const size_t col = i % CHANNEL_GRID_COLS;
      const size_t row = i / CHANNEL_GRID_COLS;
      const int16_t bx = SETTINGS_MARGIN_X + static_cast<int16_t>(col) * (chBoxW + SETTINGS_GAP);
      const int16_t by = y + static_cast<int16_t>(row) * (chBoxH + SETTINGS_GAP);
      channelBoxRects[i] = Rect16(bx, by, chBoxW, chBoxH);
      drawSettingsChannelBox(i);
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

   // Lower-right OK button and lower-left editable device location box
   const int16_t okBoxW = chBoxW;
   const int16_t okBoxH = chBoxH;
   okBoxRect = Rect16(screenW - SETTINGS_MARGIN_X - okBoxW, screenH - SETTINGS_MARGIN_X - okBoxH, okBoxW, okBoxH);
   sketch.arduino.display.fillRoundRect(okBoxRect.left(), okBoxRect.top(), okBoxRect.width, okBoxRect.height, SETTINGS_CORNER_RADIUS, static_cast<uint16_t>(Color::DARKGREEN));
   sketch.arduino.display.drawRoundRect(okBoxRect.left(), okBoxRect.top(), okBoxRect.width, okBoxRect.height, SETTINGS_CORNER_RADIUS, static_cast<uint16_t>(Color::WHITE));

   const int16_t deviceLocBoxW = okBoxRect.left() - SETTINGS_MARGIN_X - 2 * SETTINGS_GAP;
   deviceLocationBoxRect = Rect16(SETTINGS_MARGIN_X, okBoxRect.top(), deviceLocBoxW, okBoxH);
   const int16_t deviceLocLabelY = deviceLocationBoxRect.top() - 4 - sketch.arduino.charH(FOOTER_SIZE);

   sketch.arduino.setTextSize(FOOTER_SIZE);
   sketch.arduino.setCursor(SETTINGS_MARGIN_X, deviceLocLabelY);
   sketch.arduino.println("Location", Color::SUB_LABEL);

   sketch.arduino.display.fillRoundRect(deviceLocationBoxRect.left(), deviceLocationBoxRect.top(), deviceLocationBoxRect.width, deviceLocationBoxRect.height, SETTINGS_CORNER_RADIUS, static_cast<uint16_t>(Color::DARKBLUE));
   sketch.arduino.display.drawRoundRect(deviceLocationBoxRect.left(), deviceLocationBoxRect.top(), deviceLocationBoxRect.width, deviceLocationBoxRect.height, SETTINGS_CORNER_RADIUS, static_cast<uint16_t>(Color::WHITE));

   const bool hasDeviceLoc = sketch.location().length() > 0;
   const char* deviceLocText = hasDeviceLoc ? sketch.location().c_str() : "(tap to set)";
   const Color deviceLocColor = hasDeviceLoc ? Color::WHITE : Color::LIGHTGRAY;
   sketch.arduino.setTextSize(TABLE_TEXT_SIZE);
   sketch.arduino.display.setTextColor(static_cast<uint16_t>(deviceLocColor), static_cast<uint16_t>(Color::DARKBLUE));
   const int16_t devLocTw = sketch.arduino.display.textWidth(deviceLocText);
   int16_t devLocTx = deviceLocationBoxRect.left() + (deviceLocationBoxRect.width - devLocTw) / 2;
   if (devLocTx < deviceLocationBoxRect.left() + 12)
   {
      devLocTx = deviceLocationBoxRect.left() + 12;
   }
   sketch.arduino.display.setCursor(devLocTx, deviceLocationBoxRect.top() + (deviceLocationBoxRect.height - sketch.arduino.charH(TABLE_TEXT_SIZE)) / 2);
   sketch.arduino.display.print(deviceLocText);
   drawEditIcon(deviceLocationBoxRect.right() - 24, deviceLocationBoxRect.top() + deviceLocationBoxRect.height / 2, Color::LIGHTGRAY);

   // Selected channel's CT type and 120V/240V voltage sections
   const int16_t footerH = sketch.arduino.charH(FOOTER_SIZE);
   const int16_t sectionOverhead =
      (footerH + 4) +
      static_cast<int16_t>(AMP_GRID_ROWS - 1) * SETTINGS_GAP +
      2 * SETTINGS_GAP +
      (footerH + 4) +
      2 * SETTINGS_GAP;
   const int16_t availButtonRowsH = deviceLocLabelY - y - sectionOverhead;
   int16_t optionBoxH = availButtonRowsH / static_cast<int16_t>(AMP_GRID_ROWS + 1);
   if (optionBoxH > SETTINGS_CH_BOX_HEIGHT)
   {
      optionBoxH = SETTINGS_CH_BOX_HEIGHT;
   }
   if (optionBoxH < 32)
   {
      optionBoxH = 32;
   }

   sketch.arduino.setTextSize(FOOTER_SIZE);
   sketch.arduino.setCursor(SETTINGS_MARGIN_X, y);
   sketch.arduino.println("CT Type", Color::SUB_LABEL);
   y = sketch.arduino.getCursorY() + 4;

   const int16_t ampBoxH = optionBoxH;
   const int16_t ampBoxW = (contentW - static_cast<int16_t>(AMP_GRID_COLS - 1) * SETTINGS_GAP) / static_cast<int16_t>(AMP_GRID_COLS);

   for (size_t i = 0; i < NUM_AMP_OPTIONS; i++)
   {
      const size_t col = i % AMP_GRID_COLS;
      const size_t row = i / AMP_GRID_COLS;
      const int16_t bx = SETTINGS_MARGIN_X + static_cast<int16_t>(col) * (ampBoxW + SETTINGS_GAP);
      const int16_t by = y + static_cast<int16_t>(row) * (ampBoxH + SETTINGS_GAP);
      ampBoxRects[i] = Rect16(bx, by, ampBoxW, ampBoxH);

      const bool selected = (ctTypeIndices[selectedSettingsChannel] == i);
      const Color bg = selected ? Color::DARKGREEN : Color::BLUE;
      const Color border = selected ? Color::WHITE : Color::GRAY;

      sketch.arduino.display.fillRoundRect(bx, by, ampBoxW, ampBoxH, SETTINGS_CORNER_RADIUS, static_cast<uint16_t>(bg));
      sketch.arduino.display.drawRoundRect(bx, by, ampBoxW, ampBoxH, SETTINGS_CORNER_RADIUS, static_cast<uint16_t>(border));
      if (selected)
      {
         sketch.arduino.display.drawRoundRect(bx + 1, by + 1, ampBoxW - 2, ampBoxH - 2, SETTINGS_CORNER_RADIUS, static_cast<uint16_t>(border));
      }

      const char* label = CT_TYPE_TABLE[i].displayName;
      uint8_t labelTextSize = TABLE_TEXT_SIZE;
      sketch.arduino.setTextSize(labelTextSize);
      int16_t tw = sketch.arduino.display.textWidth(label);
      if (tw > ampBoxW - 8)
      {
         labelTextSize = FOOTER_SIZE;
         sketch.arduino.setTextSize(labelTextSize);
         tw = sketch.arduino.display.textWidth(label);
      }

      const int16_t labelTextH = sketch.arduino.charH(labelTextSize);
      sketch.arduino.display.setTextColor(static_cast<uint16_t>(Color::WHITE), static_cast<uint16_t>(bg));
      sketch.arduino.display.setCursor(bx + (ampBoxW - tw) / 2, by + (ampBoxH - labelTextH) / 2);
      sketch.arduino.display.print(label);
   }

   y += static_cast<int16_t>(AMP_GRID_ROWS) * ampBoxH + static_cast<int16_t>(AMP_GRID_ROWS - 1) * SETTINGS_GAP + 2 * SETTINGS_GAP;

   sketch.arduino.setTextSize(FOOTER_SIZE);
   sketch.arduino.setCursor(SETTINGS_MARGIN_X, y);
   sketch.arduino.println("Voltage", Color::SUB_LABEL);
   y = sketch.arduino.getCursorY() + 4;

   constexpr std::array<const char*, NUM_VOLTAGE_OPTIONS> VOLTAGE_LABELS = { "120V", "240V" };
   const int16_t voltBoxW = (contentW - static_cast<int16_t>(NUM_VOLTAGE_OPTIONS - 1) * SETTINGS_GAP) / static_cast<int16_t>(NUM_VOLTAGE_OPTIONS);
   const int16_t voltBoxH = optionBoxH;
   sketch.arduino.setTextSize(TABLE_TEXT_SIZE);
   const int16_t voltTextH = sketch.arduino.charH(TABLE_TEXT_SIZE);

   for (size_t i = 0; i < NUM_VOLTAGE_OPTIONS; i++)
   {
      const int16_t bx = SETTINGS_MARGIN_X + static_cast<int16_t>(i) * (voltBoxW + SETTINGS_GAP);
      voltageBoxRects[i] = Rect16(bx, y, voltBoxW, voltBoxH);

      const bool selected = (ct240V[selectedSettingsChannel] == (i == 1));
      const Color bg = selected ? Color::DARKGREEN : Color::BLUE;
      const Color border = selected ? Color::WHITE : Color::GRAY;

      sketch.arduino.display.fillRoundRect(bx, y, voltBoxW, voltBoxH, SETTINGS_CORNER_RADIUS, static_cast<uint16_t>(bg));
      sketch.arduino.display.drawRoundRect(bx, y, voltBoxW, voltBoxH, SETTINGS_CORNER_RADIUS, static_cast<uint16_t>(border));
      if (selected)
      {
         sketch.arduino.display.drawRoundRect(bx + 1, y + 1, voltBoxW - 2, voltBoxH - 2, SETTINGS_CORNER_RADIUS, static_cast<uint16_t>(border));
      }

      const char* label = VOLTAGE_LABELS[i];
      const int16_t tw = sketch.arduino.display.textWidth(label);
      sketch.arduino.display.setTextColor(static_cast<uint16_t>(Color::WHITE), static_cast<uint16_t>(bg));
      sketch.arduino.display.setCursor(bx + (voltBoxW - tw) / 2, y + (voltBoxH - voltTextH) / 2);
      sketch.arduino.display.print(label);
   }

   sketch.arduino.setTextSize(TABLE_TEXT_SIZE);
   const int16_t buttonTextH = sketch.arduino.charH(TABLE_TEXT_SIZE);
   sketch.arduino.display.setTextColor(static_cast<uint16_t>(Color::WHITE), static_cast<uint16_t>(Color::DARKGREEN));
   const int16_t okTw = sketch.arduino.display.textWidth("OK");
   sketch.arduino.display.setCursor(okBoxRect.left() + (okBoxRect.width - okTw) / 2, okBoxRect.top() + (okBoxRect.height - buttonTextH) / 2);
   sketch.arduino.display.print("OK");
}

///
/// <summary>
/// Opens the on-screen touch keyboard for runtime Settings prompts with a Cancel key and
/// a long inactivity timeout, returning an empty string on cancel or timeout instead of
/// rebooting into safe mode.
/// </summary>
/// <param name="title">Heading text drawn at the top of the screen.</param>
/// <param name="initialValue">Optional initial text pre-populated into the input box.</param>
/// <param name="allowEmpty">True to allow submitting an empty box via OK.</param>
/// <param name="outCancelled">Optional pointer set to true if the user cancelled or timed out.</param>
/// <returns>The trimmed string entered by the user, or an empty string if empty, cancelled, or timed out.</returns>
///
String promptSettingsTouchText(const char* title, const String& initialValue, bool allowEmpty = false, bool* outCancelled = nullptr)
{
   return sketch.arduino.promptTouchText(title, initialValue, SETTINGS_PROMPT_TIMEOUT_M * 60UL * 1000UL, true, allowEmpty, outCancelled);
}

///
/// <summary>
/// Opens the on-screen touch keyboard to edit the device location, persists the new value,
/// and refreshes telemetry topics immediately.
/// </summary>
///
void editDeviceLocationTouch()
{
   String entered = promptSettingsTouchText("Enter Location", sketch.location());
   entered.trim();
   if (entered.length() > 0 && entered != sketch.location())
   {
      sketch.setLocation(entered.c_str());
      refreshTelemetryTopics();
   }
}

///
/// <summary>
/// Opens the on-screen touch keyboard directly to edit the location for a channel.
/// </summary>
/// <param name="ch">Channel index (0 to 15).</param>
///
void editChannelLocationTouch(size_t ch)
{
   String kbTitle = "Location " + String(ch + 1);
   String initialText = String(ctLocations[ch]);

   while (true)
   {
      bool cancelled = false;
      String entered = promptSettingsTouchText(kbTitle.c_str(), initialText, true, &cancelled);
      if (cancelled)
      {
         return;
      }
      entered.trim();
      bool clear = (entered.length() == 0) || isClearLocationValue(entered.c_str());
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

///
/// <summary>
/// Renders live AC voltage in the header and a 4-column table of all 16 CT inputs on the display:
/// Column 1: Channel number (1-16)
/// Column 2: Configured location name
/// Column 3: RMS current in Amps (or gray dashes if unplugged)
/// Column 4: Real power in Watts (or gray dashes if unplugged)
/// </summary>
///
void updateDisplay()
{
   if (!displayReady)
   {
      return;
   }

   if (showingSettings)
   {
      for (size_t i = 0; i < NUM_CT_INPUTS; i++)
      {
         if (powerSensor.isPhysicallyConnected(i) != settingsChannelConnected[i])
         {
            drawSettingsChannelBox(i);
         }
      }
      return;
   }

   if (powerSensor.derivedChannelCount() != displayedDerivedCount)
   {
      int16_t headingHeight = sketch.arduino.charH(HEADER_SIZE);
      int16_t clearBottom = computeTelemetryButtonRect().top();
      sketch.arduino.display.fillRect(
         0,
         headingHeight,
         sketch.arduino.width(),
         clearBottom - headingHeight,
         static_cast<uint16_t>(Color::BLACK));
      displayedDerivedCount = powerSensor.derivedChannelCount();
      valueStartY = computeTableStartY();
   }

   if (powerSensor.voltageConnected())
   {
      float liveVolts = powerSensor.lineVoltage();
      if (std::isnan(displayedLineVoltage) ||
          std::fabs(liveVolts - displayedLineVoltage) >= LINE_VOLTAGE_DISPLAY_HYSTERESIS_V)
      {
         displayedLineVoltage = liveVolts;
      }
      lineVoltageField.draw(displayedLineVoltage, Color::VALUE);
   }
   else
   {
      displayedLineVoltage = NAN;
      lineVoltageField.draw(lineVoltageFormat.toNoValueString(), Color::GRAY);
   }

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

   size_t numDerived = powerSensor.derivedChannelCount();
   for (size_t d = 0; d < numDerived; d++)
   {
      sketch.arduino.print(" + ", Color::SUB_LABEL);
      sketch.arduino.print(powerSensor.derivedChannelName(d), rowLocationFormat, Color::LABEL);

      if (powerSensor.isDerivedConnected(d))
      {
         sketch.arduino.print(powerSensor.derivedAmps(d), rowAmpsFormat, Color::VALUE2);
         sketch.arduino.printlnR(powerSensor.derivedWatts(d), rowWattsFormat, Color::VALUE);
      }
      else
      {
         sketch.arduino.print(rowAmpsFormat.toNoValueString().c_str(), Color::GRAY);
         sketch.arduino.printlnR(rowWattsFormat.toNoValueString().c_str(), Color::GRAY);
      }
   }
}

///
/// <summary>
/// Polls capacitive touch input to dim the backlight after inactivity, wake to full
/// brightness on touch, and open/close the gear settings view to edit channel settings.
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

   if (touched)
   {
      displayDimTimer.reset();
   }

   if (!tapped)
   {
      if (!displayDimmed && displayDimTimer.ready())
      {
         sketch.arduino.displayLevel(DISPLAY_DIM_LEVEL);
         displayDimmed = true;
      }
      return;
   }

   if (displayDimmed)
   {
      sketch.arduino.displayOn();
      displayDimmed = false;
      return;
   }

   if (!showingSettings)
   {
      if (onGear(tp.x, tp.y))
      {
         showingSettings = true;
         drawSettingsView();
      }
      else if (inRect(telemetryButtonRect, tp.x, tp.y))
      {
         saveTelemetryEnabled(!telemetryEnabled);
      }
      return;
   }

   if (inRect(okBoxRect, tp.x, tp.y))
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
      displayDimTimer.reset();
      drawSettingsView();
      return;
   }

   if (inRect(deviceLocationBoxRect, tp.x, tp.y))
   {
      editDeviceLocationTouch();
      while (sketch.arduino.display.getTouch(&tp) > 0)
      {
         delay(10);
      }
      wasTouching = false;
      displayDimTimer.reset();
      drawSettingsView();
      return;
   }

   for (size_t i = 0; i < NUM_AMP_OPTIONS; i++)
   {
      if (inRect(ampBoxRects[i], tp.x, tp.y))
      {
         if (ctTypeIndices[selectedSettingsChannel] != i)
         {
            saveCtPreset(selectedSettingsChannel, i);
            drawSettingsView();
         }
         return;
      }
   }

   for (size_t i = 0; i < NUM_VOLTAGE_OPTIONS; i++)
   {
      if (inRect(voltageBoxRects[i], tp.x, tp.y))
      {
         const bool want240V = (i == 1);
         if (ct240V[selectedSettingsChannel] != want240V)
         {
            saveCt240V(selectedSettingsChannel, want240V);
            drawSettingsView();
         }
         return;
      }
   }
}

void setup()
{
   loadChannelConfig();

   sketch.addSensor("PowerSensor", []() { return powerSensor.begin(); });
   sketch.setValueSource([]() { return powerSensor.watts(primaryTelemetryChannel); });
   sketch.onSiteResolved([]() {
      telemetryTopicsResolved = true;
      refreshTelemetryTopics();
   });
   sketch.onExtraConfig(getExtraConfigText, trySetExtraConfig, startInteractiveChannelPrompts);
   sketch.onReadings(
      []() { return powerSensor.formatReadings(); },
      "Reports real power (W), RMS current (A), power factor, and AC line voltage (V)");
   sketch.arduino.setRotation(DisplayRotation::PORTRAIT_FLIP);

   sketch.begin();
   sketch.completeInitialization();

   // Draw static heading and footer after completeInitialization() clears the startup screen
   sketch.arduino.setTextSize(HEADER_SIZE);
   sketch.arduino.setCursor(0, 0);
   sketch.arduino.println("Power", Color::HEADING);
   drawGear();
   int16_t voltageRightX = sketch.arduino.width() - GEAR_TAP_SIZE - 8;
   int16_t headingHeight = sketch.arduino.charH();
   lineVoltageField.setPosition(voltageRightX, headingHeight / 2, VerticalAnchor::MIDDLE);

   displayedDerivedCount = powerSensor.derivedChannelCount();
   valueStartY = computeTableStartY();

   drawTelemetryButton();
   drawFooter();

   displayReady = true;
   updateDisplay();
   displayDimTimer.reset();
}

void loop()
{
   handleTouch();

   if (updateTimer.ready())
   {
      updateDisplay();

      if (telemetryEnabled)
      {
         for (size_t i = 0; i < NUM_CT_INPUTS; i++)
         {
            if (i != primaryTelemetryChannel && channelTopics[i][0] != '\0')
            {
               sketch.submit(channelTopics[i], powerSensor.watts(i));
            }
         }

         size_t numDerived = powerSensor.derivedChannelCount();
         for (size_t d = 0; d < numDerived; d++)
         {
            if (derivedPowerTopics[d][0] != '\0')
            {
               sketch.submit(derivedPowerTopics[d], powerSensor.derivedWatts(d));
            }
         }
      }
   }

   if (powerSensor.hasUrgentCalibrationUpdate() ||
       (powerSensor.hasCalibrationDriftUpdate() && autoCalSaveTimer.ready()))
   {
      saveAutoCalibration();
   }

   sketch.loop();
}
