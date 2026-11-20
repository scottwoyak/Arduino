//
// CT Calibration
//
// Dedicated multi-channel CT calibration utility for the Waveshare ESP32-P4 4.3" Touch LCD
// power monitor PCB. Plug in one or more CT types on channels 1-16, clamp them onto the same
// steady resistive load conductor, and view all connected CTs simultaneously in a live table.
//
// Features:
// - Tap "Line V" to calibrate AC line voltage (persisted directly to Power_Publisher's NVS
//   vScale key so the production sketch inherits it automatically).
// - Tap "Load A" to set the known RMS current seen by the CTs (including any splitter turns).
// - Tap a row's left CT label cell to name that CT (persisted across reboots and used in
//   the Serial CT_TYPE_TABLE snippet); tap the right values cell to select Reference CT (*).
// - Tap "Capture All" to run a 20-slice (~1.1 s per CT) high-precision capture across every
//   connected CT, freeze the results on screen, and log a summary table plus a ready-to-paste
//   CT_Calibration.h block (DEFAULT_PHASE_SHIFT_DEG and merged CT_TYPE_TABLE) to the Device
//   Hub logs and Serial.
//

#include <Arduino.h>
#include <Preferences.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iomanip>
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

#include "CT_Calibration.h"
#include "DeviceSketch.h"
#include "LibraryVersion.h"
#include "Logger.h"
#include "MuxAdcPowerSensor.h"
#include "SerialTable.h"
#include "SerialX.h"
#include "Timer.h"
#include "WiFiSettings.h"

const auto VERSION = MakeVersion("1.0");
constexpr auto SKETCH_NAME = "CT_Calibration";

// ----------- Hardware & Persistence Configuration
constexpr size_t NUM_CT_INPUTS = PowerSensorBase::MAX_CHANNELS;
constexpr uint8_t PIN_MUX_S0 = 2;
constexpr uint8_t PIN_MUX_S1 = 3;
constexpr uint8_t PIN_MUX_S2 = 4;
constexpr uint8_t PIN_MUX_S3 = 28;
constexpr uint8_t PIN_MUX_SIG = 22;
constexpr uint8_t PIN_AC_VOLTAGE = 21;

constexpr auto PREF_NAMESPACE = "Power_Publisher";
constexpr auto PREF_KEY_VOLTAGE_SCALE = "vScale";
constexpr auto CAL_PREF_NAMESPACE = SKETCH_NAME;
constexpr size_t MAX_CT_NAME_LENGTH = 16;
constexpr size_t NAME_COL_CHARS = 10;

constexpr std::array<const char*, NUM_CT_INPUTS> PREF_KEY_CT_NAME = {
   "name1", "name2",  "name3",  "name4",  "name5",  "name6",  "name7",  "name8",
   "name9", "name10", "name11", "name12", "name13", "name14", "name15", "name16",
};

// ----------- Timing & Measurement Constants
constexpr uint16_t UI_UPDATE_INTERVAL_MS = 250;
constexpr uint16_t PROMPT_TIMEOUT_M = 10;
constexpr uint16_t CAPTURE_SLICES_PER_CT = 20;
constexpr float MIN_SIGNAL_RMS_V = 0.015f;
constexpr float LIVE_EMA_ALPHA = 0.25f;
constexpr float RAD_TO_DEG_F = 57.2957795f;

// ----------- Display Layout Constants
constexpr uint8_t HEADER_SIZE = DEFAULT_HEADING_SIZE;
constexpr uint8_t TABLE_TEXT_SIZE = DEFAULT_TEXT_SIZE;
constexpr uint8_t FOOTER_SIZE = DEFAULT_FOOTER_SIZE;
constexpr int16_t MARGIN_X = 8;
constexpr int16_t GAP = 8;
constexpr int16_t CORNER_R = 8;
constexpr int16_t PARAM_BOX_Y = 54;
constexpr int16_t PARAM_BOX_H = 68;
constexpr int16_t HINT_Y = 128;
constexpr int16_t TABLE_HEADER_Y = 152;
constexpr int16_t TABLE_DIVIDER_Y = 179;
constexpr int16_t TABLE_ROWS_TOP_Y = 184;
constexpr int16_t FOOTER_LINE1_Y = 680;
constexpr int16_t FOOTER_LINE2_Y = 702;
constexpr int16_t FOOTER_BOTTOM_Y = 724;
constexpr int16_t BUTTON_H = 58;
constexpr int16_t MAX_ROW_STEP = 50;
constexpr int16_t ROW_GAP = 4;

// ----------- Board & Sensor
SketchConfig SKETCH_CONFIG = {
   .sketchName = SKETCH_NAME,
   .version = VERSION,
   .cpuFrequencyMhz = 80,
   .enableOTA = true,
};

DeviceSketch sketch(SKETCH_CONFIG);

std::array<PowerSensorBase::ChannelConfig, NUM_CT_INPUTS> channelConfigs = {
   PowerSensorBase::ChannelConfig{ "1",  PowerSensorBase::CORE_100A },
   PowerSensorBase::ChannelConfig{ "2",  PowerSensorBase::CORE_100A },
   PowerSensorBase::ChannelConfig{ "3",  PowerSensorBase::CORE_100A },
   PowerSensorBase::ChannelConfig{ "4",  PowerSensorBase::CORE_100A },
   PowerSensorBase::ChannelConfig{ "5",  PowerSensorBase::CORE_100A },
   PowerSensorBase::ChannelConfig{ "6",  PowerSensorBase::CORE_100A },
   PowerSensorBase::ChannelConfig{ "7",  PowerSensorBase::CORE_100A },
   PowerSensorBase::ChannelConfig{ "8",  PowerSensorBase::CORE_100A },
   PowerSensorBase::ChannelConfig{ "9",  PowerSensorBase::CORE_100A },
   PowerSensorBase::ChannelConfig{ "10", PowerSensorBase::CORE_100A },
   PowerSensorBase::ChannelConfig{ "11", PowerSensorBase::CORE_100A },
   PowerSensorBase::ChannelConfig{ "12", PowerSensorBase::CORE_100A },
   PowerSensorBase::ChannelConfig{ "13", PowerSensorBase::CORE_100A },
   PowerSensorBase::ChannelConfig{ "14", PowerSensorBase::CORE_100A },
   PowerSensorBase::ChannelConfig{ "15", PowerSensorBase::CORE_100A },
   PowerSensorBase::ChannelConfig{ "16", PowerSensorBase::CORE_100A },
};

MuxAdcPowerSensor powerSensor(
   PIN_MUX_S0,
   PIN_MUX_S1,
   PIN_MUX_S2,
   PIN_MUX_S3,
   PIN_MUX_SIG,
   PIN_AC_VOLTAGE,
   channelConfigs.data(),
   NUM_CT_INPUTS);

///
/// <summary>
/// Operating mode for the calibration display.
/// </summary>
///
enum class CalibrationMode : uint8_t
{
   LIVE,
   CAPTURING,
   FROZEN
};

///
/// <summary>
/// Smoothed live measurements for a single CT channel.
/// </summary>
///
struct LiveChannelState
{
   bool connected = false;
   bool hasSignal = false;
   bool hasPhase = false;
   float emaRmsVolts = 0.0f;
   float emaTotalPhaseDeg = 0.0f;
};

///
/// <summary>
/// Screen tap target and channel index for one visible row in the CT table.
/// </summary>
///
struct TableRowSlot
{
   size_t channel = 0;
   Rect16 rect;
   int16_t nameSplitX = 0;
};

// ----------- Runtime State
CalibrationMode mode = CalibrationMode::LIVE;
Timer uiUpdateTimer(UI_UPDATE_INTERVAL_MS);
bool wasTouching = false;
float knownCurrentAmps = 0.0f;
int8_t referenceChannel = -1;
std::string statusMessage = "Plug in CTs, set Load A, then Capture All";

std::array<char[MAX_CT_NAME_LENGTH], NUM_CT_INPUTS> channelNames = {};
std::array<bool, NUM_CT_INPUTS> channelCalibrated = {};
std::array<LiveChannelState, NUM_CT_INPUTS> liveChannels = {};
std::array<MuxAdcPowerSensor::CalibrationResult, NUM_CT_INPUTS> capturedResults = {};
std::array<bool, NUM_CT_INPUTS> capturedPresent = {};

std::array<size_t, NUM_CT_INPUTS> captureQueue = {};
size_t captureQueueCount = 0;
size_t captureQueueIndex = 0;

std::array<TableRowSlot, NUM_CT_INPUTS> visibleRows = {};
size_t visibleRowCount = 0;
size_t lastDrawnRowCount = 0;
int8_t lastDrawnRefChannel = -2;
std::array<size_t, NUM_CT_INPUTS> lastDrawnChannels = {};
std::array<std::string, NUM_CT_INPUTS> lastDrawnRowTexts = {};
std::array<Color, NUM_CT_INPUTS> lastDrawnRowColors = {};
std::string lastLineVText;
Color lastLineVColor = Color::BLACK;
std::string lastLoadAText;
Color lastLoadAColor = Color::BLACK;
bool lastLoadASet = false;
std::string lastFooterLine1;
Color lastFooterLine1Color = Color::BLACK;
std::string lastFooterLine2;

LGFX_Sprite paramValueSprite(&sketch.arduino.display);
bool paramValueSpriteCreated = false;
LGFX_Sprite tableRowSprite(&sketch.arduino.display);
bool tableRowSpriteCreated = false;
LGFX_Sprite compactRowSprite(&sketch.arduino.display);
bool compactRowSpriteCreated = false;
LGFX_Sprite footerLineSprite(&sketch.arduino.display);
bool footerLineSpriteCreated = false;

Rect16 voltageBoxRect;
Rect16 currentBoxRect;
Rect16 resumeButtonRect;
Rect16 captureButtonRect;

///
/// <summary>
/// Renders a single line of text into an off-screen sprite and pushes it atomically
/// to the display to eliminate clear-then-draw flicker on live updates.
/// </summary>
/// <param name="sprite">Reusable sprite buffer for this text region.</param>
/// <param name="created">Tracks whether the sprite has already been allocated.</param>
/// <param name="x">Left screen coordinate.</param>
/// <param name="y">Top screen coordinate.</param>
/// <param name="width">Sprite width in pixels.</param>
/// <param name="textSize">Font size index.</param>
/// <param name="text">Text string to render.</param>
/// <param name="fg">Foreground text color.</param>
/// <param name="bg">Background fill color.</param>
/// <param name="dividerX">Optional sprite-relative X coordinate for a 1-px vertical divider, or -1 for none.</param>
/// <param name="dividerColor">Color of the optional vertical divider.</param>
///
void drawSpriteText(
   LGFX_Sprite* sprite,
   bool* created,
   int16_t x,
   int16_t y,
   int16_t width,
   uint8_t textSize,
   const char* text,
   Color fg,
   Color bg,
   int16_t dividerX = -1,
   Color dividerColor = Color::GRAY)
{
   ASSERT(sprite != nullptr);
   ASSERT(created != nullptr);
   ASSERT(text != nullptr);

   if (!*created)
   {
      sketch.arduino.createSprite(*sprite, width, sketch.arduino.charH(textSize), textSize);
      *created = true;
   }

   sprite->fillScreen(static_cast<uint16_t>(bg));
   sprite->setTextColor(static_cast<uint16_t>(fg), static_cast<uint16_t>(bg));
   sprite->setCursor(0, 0);
   sprite->print(text);
   if (dividerX >= 0 && dividerX < width)
   {
      sprite->drawFastVLine(dividerX, 0, sprite->height(), static_cast<uint16_t>(dividerColor));
   }
   sprite->pushSprite(x, y);
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
   return rect.width > 0 && rect.height > 0 &&
          x >= rect.left() && x < rect.right() &&
          y >= rect.top() && y < rect.bottom();
}

///
/// <summary>
/// Loads the saved AC voltage calibration scale from Power_Publisher's Preferences namespace.
/// </summary>
///
void loadVoltageScale()
{
   Preferences prefs;
   if (prefs.begin(PREF_NAMESPACE, true))
   {
      if (prefs.isKey(PREF_KEY_VOLTAGE_SCALE))
      {
         float savedScale = prefs.getFloat(PREF_KEY_VOLTAGE_SCALE, MuxAdcPowerSensor::DEFAULT_VOLTAGE_SCALE);
         if (savedScale > 0.0f)
         {
            powerSensor.setVoltageScale(savedScale);
         }
      }
      prefs.end();
   }
}

///
/// <summary>
/// Saves the AC voltage calibration scale to Power_Publisher's Preferences namespace and applies it live.
/// </summary>
/// <param name="scale">Line volts per 1.0V RMS at ZMPT101B OUT.</param>
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
/// Loads saved per-channel CT names from CT_Calibration's Preferences namespace.
/// </summary>
///
void loadChannelNames()
{
   Preferences prefs;
   if (prefs.begin(CAL_PREF_NAMESPACE, true))
   {
      for (size_t i = 0; i < NUM_CT_INPUTS; i++)
      {
         if (prefs.isKey(PREF_KEY_CT_NAME[i]))
         {
            String saved = prefs.getString(PREF_KEY_CT_NAME[i], "");
            saved.trim();
            strlcpy(channelNames[i], saved.c_str(), sizeof(channelNames[i]));
         }
         else
         {
            channelNames[i][0] = '\0';
         }
      }
      prefs.end();
   }
}

///
/// <summary>
/// Saves or clears a custom CT name for a channel in Preferences.
/// </summary>
/// <param name="ch">Zero-based channel index.</param>
/// <param name="name">New name string, or empty to clear.</param>
///
void saveChannelName(size_t ch, const char* name)
{
   ASSERT(ch < NUM_CT_INPUTS);
   ASSERT(name != nullptr);

   strlcpy(channelNames[ch], name, sizeof(channelNames[ch]));

   Preferences prefs;
   prefs.begin(CAL_PREF_NAMESPACE, false);
   if (channelNames[ch][0] != '\0')
   {
      prefs.putString(PREF_KEY_CT_NAME[ch], channelNames[ch]);
   }
   else
   {
      prefs.remove(PREF_KEY_CT_NAME[ch]);
   }
   prefs.end();
}

///
/// <summary>
/// Compares two C strings case-insensitively.
/// </summary>
/// <param name="a">First string.</param>
/// <param name="b">Second string.</param>
/// <returns>True if both strings match ignoring ASCII letter case.</returns>
///
bool equalsIgnoreCase(const char* a, const char* b)
{
   ASSERT(a != nullptr);
   ASSERT(b != nullptr);

   while (*a != '\0' && *b != '\0')
   {
      if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b)))
      {
         return false;
      }
      a++;
      b++;
   }
   return *a == '\0' && *b == '\0';
}

///
/// <summary>
/// Applies the latest available calibration values for a channel (current-session capture if
/// available, otherwise matching CT_Calibration.h preset by channel name) to powerSensor so
/// live Amps and Watts readings reflect the CT's calibration.
/// </summary>
/// <param name="ch">Zero-based channel index.</param>
///
void applyChannelCalibration(size_t ch)
{
   ASSERT(ch < NUM_CT_INPUTS);

   if (capturedPresent[ch] && capturedResults[ch].failure == MuxAdcPowerSensor::CalibrationFailure::NONE)
   {
      powerSensor.setCoreRating(capturedResults[ch].ampsPerVolt, ch);
      powerSensor.setChannelPhaseOffsetDeg(ch, capturedResults[ch].totalPhaseDeg - powerSensor.phaseShiftDeg());
      channelCalibrated[ch] = true;
      return;
   }

   if (channelNames[ch][0] != '\0')
   {
      for (const CTCalibration::CtTypePreset& preset : CTCalibration::CT_TYPE_TABLE)
      {
         if (equalsIgnoreCase(preset.displayName, channelNames[ch]))
         {
            powerSensor.setCoreRating(preset.ampsPerVolt, ch);
            powerSensor.setChannelPhaseOffsetDeg(ch, preset.phaseOffsetDeg);
            channelCalibrated[ch] = true;
            return;
         }
      }
   }

   powerSensor.setChannelPhaseOffsetDeg(ch, 0.0f);
   channelCalibrated[ch] = false;
}

///
/// <summary>
/// Applies the latest calibration values to all 16 CT channels.
/// </summary>
///
void applyAllChannelCalibrations()
{
   for (size_t i = 0; i < NUM_CT_INPUTS; i++)
   {
      applyChannelCalibration(i);
   }
}

///
/// <summary>
/// Extracts a leading positive amp rating from a CT name (e.g. "100SC" -> 100, "20A" -> 20),
/// falling back to 100 when the name does not start with a positive integer.
/// </summary>
/// <param name="name">Optional custom CT name.</param>
/// <returns>Estimated rated amp value for the CT_TYPE_TABLE snippet.</returns>
///
uint16_t parseRatedAmpsFromName(const char* name)
{
   if (name == nullptr || name[0] == '\0')
   {
      return 100;
   }

   char* endPtr = nullptr;
   unsigned long parsed = std::strtoul(name, &endPtr, 10);
   if (endPtr != name && parsed > 0 && parsed <= 65535)
   {
      return static_cast<uint16_t>(parsed);
   }
   return 100;
}

///
/// <summary>
/// Returns a short display label for a calibration failure code.
/// </summary>
/// <param name="failure">Failure code reported by MuxAdcPowerSensor.</param>
/// <returns>Short text describing the failure.</returns>
///
const char* failureShortText(MuxAdcPowerSensor::CalibrationFailure failure)
{
   switch (failure)
   {
      case MuxAdcPowerSensor::CalibrationFailure::NO_VOLTAGE: return "no V";
      case MuxAdcPowerSensor::CalibrationFailure::UNPLUGGED: return "unplug";
      case MuxAdcPowerSensor::CalibrationFailure::SIGNAL_TOO_LOW: return "low";
      case MuxAdcPowerSensor::CalibrationFailure::UNSTEADY_LOAD: return "unstdy";
      case MuxAdcPowerSensor::CalibrationFailure::INVALID_POWER: return "bad P";
      case MuxAdcPowerSensor::CalibrationFailure::NO_REFERENCE: return "no ref";
      case MuxAdcPowerSensor::CalibrationFailure::INPUT_INVALID: return "inval";
      case MuxAdcPowerSensor::CalibrationFailure::CAPTURE_UNAVAILABLE: return "busy";
      case MuxAdcPowerSensor::CalibrationFailure::NONE: break;
   }
   return "err";
}

///
/// <summary>
/// Updates smoothed live RMS voltage and total phase readings for all 16 CT channels.
/// </summary>
///
void updateLiveChannelSamples()
{
   const bool hasVoltage = powerSensor.voltageConnected();

   for (size_t i = 0; i < NUM_CT_INPUTS; i++)
   {
      LiveChannelState& st = liveChannels[i];
      bool connected = powerSensor.isPhysicallyConnected(i);
      if (!connected)
      {
         st = LiveChannelState();
         continue;
      }

      float vRms = powerSensor.rmsVolts(i);
      if (!st.connected)
      {
         st.connected = true;
         st.emaRmsVolts = vRms;
         st.hasPhase = false;
      }
      else
      {
         st.emaRmsVolts += LIVE_EMA_ALPHA * (vRms - st.emaRmsVolts);
      }

      st.hasSignal = (st.emaRmsVolts >= MIN_SIGNAL_RMS_V);
      if (!st.hasSignal || !hasVoltage)
      {
         st.hasPhase = false;
         continue;
      }

      float p = powerSensor.signedWatts(i);
      float q = powerSensor.reactivePower(i);
      if (std::fabs(p) >= 0.05f)
      {
         float sliceTotalPhaseDeg = powerSensor.channelPhaseShiftDeg(i) + (std::atan(q / p) * RAD_TO_DEG_F);
         if (!st.hasPhase)
         {
            st.emaTotalPhaseDeg = sliceTotalPhaseDeg;
            st.hasPhase = true;
         }
         else
         {
            st.emaTotalPhaseDeg += LIVE_EMA_ALPHA * (sliceTotalPhaseDeg - st.emaTotalPhaseDeg);
         }
      }
   }
}

///
/// <summary>
/// Ensures referenceChannel points to an active visible channel when at least one exists.
/// </summary>
/// <param name="activeChannels">Array of active channel indices.</param>
/// <param name="count">Number of active channels in activeChannels.</param>
///
void ensureValidReferenceChannel(const std::array<size_t, NUM_CT_INPUTS>& activeChannels, size_t count)
{
   if (count == 0)
   {
      referenceChannel = -1;
      return;
   }

   for (size_t i = 0; i < count; i++)
   {
      if (static_cast<int8_t>(activeChannels[i]) == referenceChannel)
      {
         return;
      }
   }

   referenceChannel = static_cast<int8_t>(activeChannels[0]);
}

///
/// <summary>
/// Gets the active reference CT's total phase in degrees if available.
/// </summary>
/// <param name="outRefPhaseDeg">Receives the reference total phase in degrees.</param>
/// <returns>True if a valid reference phase is available.</returns>
///
bool getReferencePhaseDeg(float* outRefPhaseDeg)
{
   ASSERT(outRefPhaseDeg != nullptr);

   if (referenceChannel < 0 || static_cast<size_t>(referenceChannel) >= NUM_CT_INPUTS)
   {
      return false;
   }

   size_t refIdx = static_cast<size_t>(referenceChannel);
   if (mode == CalibrationMode::FROZEN)
   {
      if (capturedPresent[refIdx] && capturedResults[refIdx].failure == MuxAdcPowerSensor::CalibrationFailure::NONE)
      {
         *outRefPhaseDeg = capturedResults[refIdx].totalPhaseDeg;
         return true;
      }
      return false;
   }

   if (liveChannels[refIdx].connected && liveChannels[refIdx].hasPhase)
   {
      *outRefPhaseDeg = liveChannels[refIdx].emaTotalPhaseDeg;
      return true;
   }
   return false;
}

///
/// <summary>
/// Draws the top heading and current mode badge.
/// </summary>
///
void drawHeader()
{
   const int16_t screenW = sketch.arduino.width();
   sketch.arduino.display.fillRect(0, 0, screenW, PARAM_BOX_Y, static_cast<uint16_t>(Color::BLACK));

   sketch.arduino.setTextSize(HEADER_SIZE);
   sketch.arduino.setCursor(MARGIN_X, 6);
   sketch.arduino.println("CT Calibration", Color::HEADING);

   const char* badgeText = "LIVE";
   Color badgeColor = Color::DARKGREEN;
   if (mode == CalibrationMode::CAPTURING)
   {
      badgeText = "CAPTURING";
      badgeColor = Color::VALUE2;
   }
   else if (mode == CalibrationMode::FROZEN)
   {
      badgeText = "CAPTURED";
      badgeColor = Color::VALUE;
   }

   sketch.arduino.setTextSize(FOOTER_SIZE);
   const int16_t badgeW = sketch.arduino.display.textWidth(badgeText) + 14;
   const int16_t badgeH = sketch.arduino.charH(FOOTER_SIZE) + 10;
   const int16_t badgeX = screenW - MARGIN_X - badgeW;
   const int16_t badgeY = 12;
   sketch.arduino.display.fillRoundRect(badgeX, badgeY, badgeW, badgeH, 6, static_cast<uint16_t>(badgeColor));
   sketch.arduino.display.setTextColor(static_cast<uint16_t>(Color::BLACK), static_cast<uint16_t>(badgeColor));
   sketch.arduino.display.setCursor(badgeX + 7, badgeY + 5);
   sketch.arduino.display.print(badgeText);
}

///
/// <summary>
/// Draws the Line V and Load A tap-to-set parameter boxes at the top of the screen.
/// </summary>
/// <param name="force">True to redraw both boxes unconditionally.</param>
///
void drawParameterBoxes(bool force = false)
{
   const int16_t contentW = sketch.arduino.width() - 2 * MARGIN_X;
   const int16_t halfW = (contentW - GAP) / 2;

   voltageBoxRect = Rect16(MARGIN_X, PARAM_BOX_Y, halfW, PARAM_BOX_H);
   currentBoxRect = Rect16(MARGIN_X + halfW + GAP, PARAM_BOX_Y, contentW - halfW - GAP, PARAM_BOX_H);
   const int16_t valueSpriteW = std::min(voltageBoxRect.width, currentBoxRect.width) - 20;

   std::ostringstream vStream;
   Color vColor = Color::VALUE2;
   if (powerSensor.voltageConnected())
   {
      vStream << std::fixed << std::setprecision(1) << powerSensor.lineVoltage() << " V";
      vColor = Color::VALUE;
   }
   else
   {
      vStream << "No V";
   }
   std::string vText = vStream.str();

   if (force)
   {
      sketch.arduino.display.fillRoundRect(voltageBoxRect.left(), voltageBoxRect.top(), voltageBoxRect.width, voltageBoxRect.height, CORNER_R, static_cast<uint16_t>(Color::DARKBLUE));
      sketch.arduino.display.drawRoundRect(voltageBoxRect.left(), voltageBoxRect.top(), voltageBoxRect.width, voltageBoxRect.height, CORNER_R, static_cast<uint16_t>(Color::WHITE));
      sketch.arduino.setTextSize(FOOTER_SIZE);
      sketch.arduino.display.setTextColor(static_cast<uint16_t>(Color::SUB_LABEL), static_cast<uint16_t>(Color::DARKBLUE));
      sketch.arduino.display.setCursor(voltageBoxRect.left() + 10, voltageBoxRect.top() + 8);
      sketch.arduino.display.print("Line V (tap)");
   }

   if (force || vText != lastLineVText || vColor != lastLineVColor)
   {
      lastLineVText = vText;
      lastLineVColor = vColor;
      drawSpriteText(
         &paramValueSprite,
         &paramValueSpriteCreated,
         voltageBoxRect.left() + 10,
         voltageBoxRect.top() + 34,
         valueSpriteW,
         TABLE_TEXT_SIZE,
         vText.c_str(),
         vColor,
         Color::DARKBLUE);
   }

   const bool loadASet = knownCurrentAmps > 0.0f;
   std::ostringstream aStream;
   Color aColor = Color::VALUE2;
   if (loadASet)
   {
      aStream << std::fixed << std::setprecision(2) << knownCurrentAmps << " A";
      aColor = Color::VALUE;
   }
   else
   {
      aStream << "Tap to set";
   }
   std::string aText = aStream.str();

   const bool loadAChromeChanged = force || (loadASet != lastLoadASet);
   if (loadAChromeChanged)
   {
      lastLoadASet = loadASet;
      const Color curBorder = loadASet ? Color::WHITE : Color::VALUE2;
      sketch.arduino.display.fillRoundRect(currentBoxRect.left(), currentBoxRect.top(), currentBoxRect.width, currentBoxRect.height, CORNER_R, static_cast<uint16_t>(Color::DARKBLUE));
      sketch.arduino.display.drawRoundRect(currentBoxRect.left(), currentBoxRect.top(), currentBoxRect.width, currentBoxRect.height, CORNER_R, static_cast<uint16_t>(curBorder));
      sketch.arduino.setTextSize(FOOTER_SIZE);
      sketch.arduino.display.setTextColor(static_cast<uint16_t>(Color::SUB_LABEL), static_cast<uint16_t>(Color::DARKBLUE));
      sketch.arduino.display.setCursor(currentBoxRect.left() + 10, currentBoxRect.top() + 8);
      sketch.arduino.display.print("Load A (tap)");
   }

   if (loadAChromeChanged || aText != lastLoadAText || aColor != lastLoadAColor)
   {
      lastLoadAText = aText;
      lastLoadAColor = aColor;
      drawSpriteText(
         &paramValueSprite,
         &paramValueSpriteCreated,
         currentBoxRect.left() + 10,
         currentBoxRect.top() + 34,
         valueSpriteW,
         TABLE_TEXT_SIZE,
         aText.c_str(),
         aColor,
         Color::DARKBLUE);
   }
}

///
/// <summary>
/// Draws the static hint, mode-appropriate table column header, and divider line.
/// </summary>
///
void drawTableHeader()
{
   const int16_t contentW = sketch.arduino.width() - 2 * MARGIN_X;
   sketch.arduino.setTextSize(FOOTER_SIZE);
   sketch.arduino.setCursor(MARGIN_X, HINT_Y);
   sketch.arduino.println("Tap CT name to rename | Tap values for Ref (*)", Color::SUB_LABEL);

   sketch.arduino.setTextSize(TABLE_TEXT_SIZE);
   sketch.arduino.setCursor(MARGIN_X + 7, TABLE_HEADER_Y);
   const char* headerText = (mode == CalibrationMode::FROZEN)
      ? "CT              A/V       Offset"
      : "CT             Amps        Watts";
   sketch.arduino.println(headerText, Color::SUB_LABEL);
   sketch.arduino.display.drawFastHLine(MARGIN_X, TABLE_DIVIDER_Y, contentW, static_cast<uint16_t>(Color::GRAY));
}

///
/// <summary>
/// Formats one 32-character fixed-width row string for the CT table.
/// </summary>
/// <param name="ch">Zero-based channel index.</param>
/// <param name="isRef">True if this channel is the selected reference CT.</param>
/// <param name="col1">Right-aligned 11-char first value column text.</param>
/// <param name="col2">Right-aligned 11-char second value column text.</param>
/// <returns>Formatted 32-character row string.</returns>
///
std::string formatTableRowText(size_t ch, bool isRef, const std::string& col1, const std::string& col2)
{
   std::string label;
   label += isRef ? '*' : ' ';
   label += std::to_string(ch + 1);
   if (channelNames[ch][0] != '\0')
   {
      label += ':';
      label += channelNames[ch];
   }
   if (label.length() > NAME_COL_CHARS - 1)
   {
      label.resize(NAME_COL_CHARS - 1);
   }
   label.append(NAME_COL_CHARS - label.length(), ' ');

   std::ostringstream row;
   row << label
       << std::setw(11) << col1
       << std::setw(11) << col2;
   return row.str();
}

///
/// <summary>
/// Draws the list of active (connected or captured) CT rows without full-screen flicker.
/// </summary>
/// <param name="force">True to redraw all row boxes and text unconditionally.</param>
///
void drawTableRows(bool force = false)
{
   const int16_t contentW = sketch.arduino.width() - 2 * MARGIN_X;
   const int16_t tableAreaH = FOOTER_LINE1_Y - TABLE_ROWS_TOP_Y - 6;

   std::array<size_t, NUM_CT_INPUTS> activeChannels = {};
   size_t count = 0;

   if (mode == CalibrationMode::FROZEN)
   {
      for (size_t i = 0; i < NUM_CT_INPUTS; i++)
      {
         if (capturedPresent[i])
         {
            activeChannels[count++] = i;
         }
      }
   }
   else
   {
      for (size_t i = 0; i < NUM_CT_INPUTS; i++)
      {
         if (liveChannels[i].connected)
         {
            activeChannels[count++] = i;
         }
      }
   }

   ensureValidReferenceChannel(activeChannels, count);

   const int8_t prevRefChannel = lastDrawnRefChannel;
   const bool refChanged = (referenceChannel != prevRefChannel);
   bool layoutChanged = force || (count != lastDrawnRowCount);
   if (!layoutChanged)
   {
      for (size_t i = 0; i < count; i++)
      {
         if (activeChannels[i] != lastDrawnChannels[i])
         {
            layoutChanged = true;
            break;
         }
      }
   }

   if (force || count != lastDrawnRowCount)
   {
      sketch.arduino.display.fillRect(0, TABLE_ROWS_TOP_Y, sketch.arduino.width(), tableAreaH, static_cast<uint16_t>(Color::BLACK));
   }

   visibleRowCount = count;
   lastDrawnRowCount = count;
   lastDrawnRefChannel = referenceChannel;

   if (count == 0)
   {
      if (layoutChanged)
      {
         sketch.arduino.setTextSize(TABLE_TEXT_SIZE);
         sketch.arduino.setCursor(MARGIN_X, TABLE_ROWS_TOP_Y + 40);
         sketch.arduino.printlnWrapped("Plug in one or more CTs on channels 1-16.", MARGIN_X, contentW, Color::GRAY);
         sketch.arduino.setCursor(MARGIN_X, sketch.arduino.getCursorY() + GAP);
         sketch.arduino.printlnWrapped("Clamp all CTs onto the same steady resistive load wire.", MARGIN_X, contentW, Color::GRAY);
      }
      return;
   }

   float refPhaseDeg = 0.0f;
   bool hasRefPhase = getReferencePhaseDeg(&refPhaseDeg);

   int16_t rowStep = tableAreaH / static_cast<int16_t>(count);
   if (rowStep > MAX_ROW_STEP)
   {
      rowStep = MAX_ROW_STEP;
   }
   const int16_t rowH = rowStep - ROW_GAP;
   const bool useCompactFont = rowH < 32;
   const uint8_t rowTextSize = useCompactFont ? FOOTER_SIZE : TABLE_TEXT_SIZE;
   const int16_t textH = sketch.arduino.charH(rowTextSize);
   LGFX_Sprite* rowSprite = useCompactFont ? &compactRowSprite : &tableRowSprite;
   bool* rowSpriteCreated = useCompactFont ? &compactRowSpriteCreated : &tableRowSpriteCreated;
   const int16_t rowTextWidth = contentW - 14;
   const int16_t dividerOffsetX = static_cast<int16_t>(NAME_COL_CHARS) * sketch.arduino.charW(rowTextSize) - 4;

   for (size_t idx = 0; idx < count; idx++)
   {
      const size_t ch = activeChannels[idx];
      lastDrawnChannels[idx] = ch;
      const int16_t rowY = TABLE_ROWS_TOP_Y + static_cast<int16_t>(idx) * rowStep;
      const Rect16 rect(MARGIN_X, rowY, contentW, rowH);
      const int16_t dividerScreenX = rect.left() + 7 + dividerOffsetX;
      visibleRows[idx] = TableRowSlot{ ch, rect, dividerScreenX };

      const bool isRef = (static_cast<int8_t>(ch) == referenceChannel);
      const Color bg = isRef ? Color::DARKGREEN : Color::DARKBLUE;
      const Color border = isRef ? Color::WHITE : Color::GRAY;
      Color textColor = Color::WHITE;

      std::string col1 = "---";
      std::string col2 = "---";

      if (mode == CalibrationMode::FROZEN)
      {
         const MuxAdcPowerSensor::CalibrationResult& res = capturedResults[ch];
         if (res.failure != MuxAdcPowerSensor::CalibrationFailure::NONE)
         {
            col1 = failureShortText(res.failure);
            textColor = Color::VALUE2;
         }
         else
         {
            std::ostringstream avS;
            avS << std::fixed << std::setprecision(2) << res.ampsPerVolt;
            col1 = avS.str();

            if (hasRefPhase)
            {
               float offsetDeg = isRef ? 0.0f : (res.totalPhaseDeg - refPhaseDeg);
               std::ostringstream offS;
               offS << std::fixed << std::showpos << std::setprecision(2) << offsetDeg;
               col2 = offS.str();
            }
         }
      }
      else
      {
         if (!channelCalibrated[ch])
         {
            col1 = "uncal";
            textColor = Color::VALUE2;
         }
         else
         {
            float liveAmps = powerSensor.amps(ch);
            std::ostringstream ampsS;
            ampsS << std::fixed << std::setprecision(2) << liveAmps << " A";
            col1 = ampsS.str();

            if (powerSensor.voltageConnected())
            {
               std::ostringstream wattsS;
               wattsS << std::fixed << std::setprecision(1) << powerSensor.watts(ch) << " W";
               col2 = wattsS.str();
            }

            if (liveAmps < 0.01f)
            {
               textColor = Color::LIGHTGRAY;
            }
         }
      }

      const bool rowChromeChanged = layoutChanged ||
         (refChanged && (static_cast<int8_t>(ch) == prevRefChannel || isRef));
      if (rowChromeChanged)
      {
         sketch.arduino.display.fillRoundRect(rect.left(), rect.top(), rect.width, rect.height, CORNER_R, static_cast<uint16_t>(bg));
         sketch.arduino.display.drawRoundRect(rect.left(), rect.top(), rect.width, rect.height, CORNER_R, static_cast<uint16_t>(border));
         if (isRef)
         {
            sketch.arduino.display.drawRoundRect(rect.left() + 1, rect.top() + 1, rect.width - 2, rect.height - 2, CORNER_R, static_cast<uint16_t>(border));
         }
         sketch.arduino.display.drawFastVLine(dividerScreenX, rect.top() + 3, rect.height - 6, static_cast<uint16_t>(Color::GRAY));
      }

      std::string rowText = formatTableRowText(ch, isRef, col1, col2);
      if (rowChromeChanged || rowText != lastDrawnRowTexts[idx] || textColor != lastDrawnRowColors[idx])
      {
         lastDrawnRowTexts[idx] = rowText;
         lastDrawnRowColors[idx] = textColor;
         drawSpriteText(
            rowSprite,
            rowSpriteCreated,
            rect.left() + 7,
            rect.top() + (rect.height - textH) / 2,
            rowTextWidth,
            rowTextSize,
            rowText.c_str(),
            textColor,
            bg,
            dividerOffsetX,
            Color::GRAY);
      }
   }
}

///
/// <summary>
/// Draws the 2-line reference phase summary and live status footer above the action buttons.
/// </summary>
/// <param name="force">True to redraw both footer lines unconditionally.</param>
///
void drawStatusFooter(bool force = false)
{
   float refPhaseDeg = 0.0f;
   std::string line1;
   Color line1Color = Color::GRAY;
   if (getReferencePhaseDeg(&refPhaseDeg))
   {
      std::ostringstream refLine;
      refLine << std::fixed << std::setprecision(2) << "DEFAULT_PHASE_SHIFT_DEG = " << refPhaseDeg << "f";
      line1 = refLine.str();
      line1Color = Color::VALUE;
   }
   else
   {
      line1 = "DEFAULT_PHASE_SHIFT_DEG = ---";
   }

   const int16_t footerWidth = sketch.arduino.width() - 2 * MARGIN_X;

   if (force || line1 != lastFooterLine1 || line1Color != lastFooterLine1Color)
   {
      lastFooterLine1 = line1;
      lastFooterLine1Color = line1Color;
      drawSpriteText(
         &footerLineSprite,
         &footerLineSpriteCreated,
         MARGIN_X,
         FOOTER_LINE1_Y,
         footerWidth,
         FOOTER_SIZE,
         line1.c_str(),
         line1Color,
         Color::BLACK);
   }

   if (force || statusMessage != lastFooterLine2)
   {
      lastFooterLine2 = statusMessage;
      drawSpriteText(
         &footerLineSprite,
         &footerLineSpriteCreated,
         MARGIN_X,
         FOOTER_LINE2_Y,
         footerWidth,
         FOOTER_SIZE,
         statusMessage.c_str(),
         Color::SUB_LABEL,
         Color::BLACK);
   }
}

///
/// <summary>
/// Draws the bottom action buttons for the active mode.
/// </summary>
///
void drawActionButtons()
{
   const int16_t contentW = sketch.arduino.width() - 2 * MARGIN_X;
   const int16_t buttonY = sketch.arduino.height() - MARGIN_X - BUTTON_H;
   const int16_t btnTextH = sketch.arduino.charH(TABLE_TEXT_SIZE);

   sketch.arduino.display.fillRect(0, buttonY - 2, sketch.arduino.width(), BUTTON_H + MARGIN_X + 2, static_cast<uint16_t>(Color::BLACK));
   sketch.arduino.setTextSize(TABLE_TEXT_SIZE);
   sketch.arduino.display.setTextColor(static_cast<uint16_t>(Color::WHITE));

   if (mode == CalibrationMode::FROZEN)
   {
      const int16_t leftW = (contentW - GAP) / 2;
      const int16_t rightW = contentW - leftW - GAP;
      resumeButtonRect = Rect16(MARGIN_X, buttonY, leftW, BUTTON_H);
      captureButtonRect = Rect16(MARGIN_X + leftW + GAP, buttonY, rightW, BUTTON_H);

      sketch.arduino.display.fillRoundRect(resumeButtonRect.left(), resumeButtonRect.top(), resumeButtonRect.width, resumeButtonRect.height, CORNER_R, static_cast<uint16_t>(Color::GRAY));
      sketch.arduino.display.fillRoundRect(captureButtonRect.left(), captureButtonRect.top(), captureButtonRect.width, captureButtonRect.height, CORNER_R, static_cast<uint16_t>(Color::DARKGREEN));

      sketch.arduino.display.setCursor(
         resumeButtonRect.left() + (resumeButtonRect.width - sketch.arduino.display.textWidth("Resume Live")) / 2,
         resumeButtonRect.top() + (resumeButtonRect.height - btnTextH) / 2);
      sketch.arduino.display.print("Resume Live");

      sketch.arduino.display.setCursor(
         captureButtonRect.left() + (captureButtonRect.width - sketch.arduino.display.textWidth("Re-Capture")) / 2,
         captureButtonRect.top() + (captureButtonRect.height - btnTextH) / 2);
      sketch.arduino.display.print("Re-Capture");
   }
   else if (mode == CalibrationMode::CAPTURING)
   {
      resumeButtonRect = Rect16();
      captureButtonRect = Rect16();
      const Rect16 busyRect(MARGIN_X, buttonY, contentW, BUTTON_H);
      sketch.arduino.display.fillRoundRect(busyRect.left(), busyRect.top(), busyRect.width, busyRect.height, CORNER_R, static_cast<uint16_t>(Color::GRAY));
      sketch.arduino.display.setCursor(
         busyRect.left() + (busyRect.width - sketch.arduino.display.textWidth("Capturing...")) / 2,
         busyRect.top() + (busyRect.height - btnTextH) / 2);
      sketch.arduino.display.print("Capturing...");
   }
   else
   {
      resumeButtonRect = Rect16();
      captureButtonRect = Rect16(MARGIN_X, buttonY, contentW, BUTTON_H);
      const Color bg = visibleRowCount > 0 ? Color::DARKGREEN : Color::GRAY;
      sketch.arduino.display.fillRoundRect(captureButtonRect.left(), captureButtonRect.top(), captureButtonRect.width, captureButtonRect.height, CORNER_R, static_cast<uint16_t>(bg));
      sketch.arduino.display.setCursor(
         captureButtonRect.left() + (captureButtonRect.width - sketch.arduino.display.textWidth("Capture All")) / 2,
         captureButtonRect.top() + (captureButtonRect.height - btnTextH) / 2);
      sketch.arduino.display.print("Capture All");
   }
}

///
/// <summary>
/// Redraws the entire calibration screen cleanly.
/// </summary>
///
void drawFullScreen()
{
   sketch.arduino.clearDisplay();
   lastDrawnRowCount = 0;
   lastDrawnRefChannel = -2;
   lastLineVText.clear();
   lastLineVColor = Color::BLACK;
   lastLoadAText.clear();
   lastLoadAColor = Color::BLACK;
   lastFooterLine1.clear();
   lastFooterLine1Color = Color::BLACK;
   lastFooterLine2.clear();
   for (std::string& s : lastDrawnRowTexts)
   {
      s.clear();
   }
   lastDrawnRowColors.fill(Color::BLACK);
   drawHeader();
   drawParameterBoxes(true);
   drawTableHeader();
   drawTableRows(true);
   drawStatusFooter(true);
   drawActionButtons();
}

///
/// <summary>
/// Logs the captured multi-CT calibration table and a ready-to-paste CT_Calibration.h block
/// to the Device Hub log (which also echoes to Serial).
/// </summary>
///
void logCapturedResults()
{
   float refPhaseDeg = 0.0f;
   bool hasRefPhase = getReferencePhaseDeg(&refPhaseDeg);

   static constexpr std::array<SerialTable::Column, 6> SERIAL_COLUMNS = { {
      { "CT", 14 },
      { "mV RMS", 9 },
      { "A/V", 9 },
      { "Phase", 9 },
      { "Offset", 9 },
      { "Spread", 9 },
   } };

   std::ostringstream title;
   title << std::fixed << std::setprecision(1)
         << "CT Calibration Results (Line V: " << powerSensor.lineVoltage() << " V, Load A: "
         << std::setprecision(2) << knownCurrentAmps << " A)";
   std::string titleText = title.str();

   SerialTable table(titleText.c_str(), SERIAL_COLUMNS);
   String tableOutput = table.formatHeader();

   for (size_t i = 0; i < NUM_CT_INPUTS; i++)
   {
      if (!capturedPresent[i])
      {
         continue;
      }

      const bool isRef = (static_cast<int8_t>(i) == referenceChannel);
      std::string chLabel = (isRef ? "*" : " ") + std::to_string(i + 1);
      if (channelNames[i][0] != '\0')
      {
         chLabel += ":";
         chLabel += channelNames[i];
      }
      const MuxAdcPowerSensor::CalibrationResult& res = capturedResults[i];

      if (res.failure != MuxAdcPowerSensor::CalibrationFailure::NONE)
      {
         tableOutput += table.formatRow(chLabel.c_str(), failureShortText(res.failure), "---", "---", "---", "---");
         continue;
      }

      float offsetDeg = hasRefPhase ? (isRef ? 0.0f : (res.totalPhaseDeg - refPhaseDeg)) : 0.0f;
      std::ostringstream offStr;
      if (hasRefPhase)
      {
         offStr << std::fixed << std::showpos << std::setprecision(2) << offsetDeg;
      }
      else
      {
         offStr << "---";
      }

      std::ostringstream spreadStr;
      spreadStr << std::fixed << std::setprecision(1) << (res.currentSpreadRatio * 100.0f) << "%";

      tableOutput += table.formatRow(
         chLabel.c_str(),
         SerialTable::fixed(res.sensorRmsVolts * 1000.0f, 2),
         SerialTable::fixed(res.ampsPerVolt, 2),
         SerialTable::fixed(res.totalPhaseDeg, 2),
         offStr.str().c_str(),
         spreadStr.str().c_str());
   }

   while (tableOutput.endsWith("\n"))
   {
      tableOutput.remove(tableOutput.length() - 1);
   }
   Logger.log(tableOutput, LogSeverity::INFO, "Calibration");

   struct MergedCtEntry
   {
      std::string displayName;
      uint16_t ratedAmps;
      float ampsPerVolt;
      float phaseOffsetDeg;
      bool updated = false;
      bool isNew = false;
   };

   std::vector<MergedCtEntry> mergedTable;
   mergedTable.reserve(CTCalibration::NUM_CT_TYPES + NUM_CT_INPUTS);
   for (const CTCalibration::CtTypePreset& preset : CTCalibration::CT_TYPE_TABLE)
   {
      mergedTable.push_back({ preset.displayName, preset.ratedAmps, preset.ampsPerVolt, preset.phaseOffsetDeg, false, false });
   }

   for (size_t i = 0; i < NUM_CT_INPUTS; i++)
   {
      if (!capturedPresent[i] || capturedResults[i].failure != MuxAdcPowerSensor::CalibrationFailure::NONE)
      {
         continue;
      }

      const bool isRef = (static_cast<int8_t>(i) == referenceChannel);
      float offsetDeg = hasRefPhase ? (isRef ? 0.0f : (capturedResults[i].totalPhaseDeg - refPhaseDeg)) : 0.0f;
      std::string presetName = channelNames[i][0] != '\0' ? std::string(channelNames[i]) : ("Ch" + std::to_string(i + 1));

      bool matchedExisting = false;
      for (MergedCtEntry& existing : mergedTable)
      {
         if (equalsIgnoreCase(existing.displayName.c_str(), presetName.c_str()))
         {
            existing.displayName = presetName;
            existing.ampsPerVolt = capturedResults[i].ampsPerVolt;
            existing.phaseOffsetDeg = offsetDeg;
            existing.updated = true;
            matchedExisting = true;
            break;
         }
      }

      if (!matchedExisting)
      {
         mergedTable.push_back({
            presetName,
            parseRatedAmpsFromName(channelNames[i]),
            capturedResults[i].ampsPerVolt,
            offsetDeg,
            true,
            true
         });
      }
   }

   std::ostringstream snippet;
   snippet << "// Copy into libraries/Woyak/CT_Calibration.h (namespace CTCalibration):\n";
   if (hasRefPhase)
   {
      snippet << std::fixed << std::setprecision(2)
              << "inline constexpr float DEFAULT_PHASE_SHIFT_DEG = " << refPhaseDeg << "f;  // Ch " << (referenceChannel + 1);
      if (referenceChannel >= 0 && channelNames[static_cast<size_t>(referenceChannel)][0] != '\0')
      {
         snippet << " (" << channelNames[static_cast<size_t>(referenceChannel)] << ")";
      }
      snippet << "\n";
   }

   snippet << "inline constexpr size_t NUM_CT_TYPES = " << mergedTable.size() << ";\n";
   snippet << "inline constexpr std::array<CtTypePreset, NUM_CT_TYPES> CT_TYPE_TABLE = { {\n";
   for (const MergedCtEntry& entry : mergedTable)
   {
      std::string quotedName = "\"" + entry.displayName + "\",";
      std::ostringstream ampsCol;
      ampsCol << entry.ratedAmps << ",";
      std::ostringstream avCol;
      avCol << std::fixed << std::setprecision(2) << entry.ampsPerVolt << "f,";
      std::ostringstream offCol;
      offCol << std::fixed << std::showpos << std::setprecision(2) << entry.phaseOffsetDeg << "f";

      snippet << "   { " << std::left << std::setw(8) << quotedName
              << " " << std::setw(4) << ampsCol.str()
              << " " << std::right << std::setw(8) << avCol.str()
              << " " << std::setw(7) << offCol.str() << " },";
      if (entry.isNew)
      {
         snippet << "  // new";
      }
      else if (!entry.updated)
      {
         snippet << "  // unchanged (not captured)";
      }
      snippet << "\n";
   }
   snippet << "} };";

   Logger.log(snippet.str(), LogSeverity::INFO, "Calibration");
}

///
/// <summary>
/// Opens the numeric keypad to calibrate and persist AC line voltage (vScale).
/// </summary>
///
void promptLineVoltage()
{
   std::ostringstream title;
   title << std::fixed << std::setprecision(1) << "AC line voltage (V) - OK keeps " << powerSensor.lineVoltage() << "V";

   bool cancelled = false;
   String entered = sketch.arduino.promptTouchNumber(title.str().c_str(), "", true, PROMPT_TIMEOUT_M * 60UL * 1000UL, true, true, &cancelled);
   entered.trim();

   if (!cancelled && entered.length() > 0)
   {
      char* endPtr = nullptr;
      float actualVolts = std::strtof(entered.c_str(), &endPtr);
      if (endPtr != entered.c_str() && *endPtr == '\0' && std::isfinite(actualVolts) && actualVolts > 0.0f)
      {
         if (powerSensor.voltageConnected() && powerSensor.voltageSensorRmsVolts() > 0.0f)
         {
            saveVoltageScale(actualVolts / powerSensor.voltageSensorRmsVolts());
            statusMessage = "Saved AC line voltage calibration";
         }
         else
         {
            statusMessage = "No valid AC line voltage signal";
         }
      }
      else
      {
         statusMessage = "Invalid voltage entered";
      }
   }

   drawFullScreen();
}

///
/// <summary>
/// Opens the numeric keypad to set the known RMS current through the clamped CTs.
/// </summary>
/// <returns>True if a valid positive current is set afterward.</returns>
///
bool promptKnownCurrent()
{
   bool cancelled = false;
   String entered = sketch.arduino.promptTouchNumber("Known CT current incl. turns (A)", "", true, PROMPT_TIMEOUT_M * 60UL * 1000UL, true, false, &cancelled);
   entered.trim();

   bool updated = false;
   if (!cancelled && entered.length() > 0)
   {
      char* endPtr = nullptr;
      float amps = std::strtof(entered.c_str(), &endPtr);
      if (endPtr != entered.c_str() && *endPtr == '\0' && std::isfinite(amps) && amps > 0.0f)
      {
         knownCurrentAmps = amps;
         updated = true;

         if (mode == CalibrationMode::FROZEN)
         {
            for (size_t i = 0; i < NUM_CT_INPUTS; i++)
            {
               if (capturedPresent[i] &&
                   capturedResults[i].failure == MuxAdcPowerSensor::CalibrationFailure::NONE &&
                   capturedResults[i].sensorRmsVolts > 0.0f)
               {
                  capturedResults[i].knownTrueAmps = knownCurrentAmps;
                  capturedResults[i].ampsPerVolt = knownCurrentAmps / capturedResults[i].sensorRmsVolts;
               }
            }
            applyAllChannelCalibrations();
            logCapturedResults();
            statusMessage = "Updated A/V from new Load A (logged)";
         }
         else
         {
            statusMessage = "Live updating | Tap Capture All when ready";
         }
      }
      else
      {
         statusMessage = "Invalid current entered";
      }
   }

   drawFullScreen();
   return updated || knownCurrentAmps > 0.0f;
}

///
/// <summary>
/// Opens the on-screen touch keyboard to assign or clear a custom name for a CT channel.
/// </summary>
/// <param name="ch">Zero-based channel index.</param>
///
void promptChannelName(size_t ch)
{
   ASSERT(ch < NUM_CT_INPUTS);

   std::ostringstream title;
   title << "Name for Ch " << (ch + 1) << " (empty clears)";

   bool cancelled = false;
   String entered = sketch.arduino.promptTouchText(
      title.str().c_str(),
      String(channelNames[ch]),
      PROMPT_TIMEOUT_M * 60UL * 1000UL,
      true,
      true,
      &cancelled);
   entered.trim();

   if (!cancelled)
   {
      saveChannelName(ch, entered.c_str());
      applyChannelCalibration(ch);
      if (channelNames[ch][0] != '\0')
      {
         statusMessage = "Named Ch " + std::to_string(ch + 1) + " \"" + channelNames[ch] + "\"";
      }
      else
      {
         statusMessage = "Cleared name for Ch " + std::to_string(ch + 1);
      }

      if (mode == CalibrationMode::FROZEN)
      {
         logCapturedResults();
      }
   }

   drawFullScreen();
}

///
/// <summary>
/// Starts the next queued channel capture, or finalizes the batch when all channels finish.
/// </summary>
///
void startNextQueuedCapture()
{
   while (captureQueueIndex < captureQueueCount)
   {
      size_t ch = captureQueue[captureQueueIndex];
      std::ostringstream msg;
      msg << "Capturing Ch " << (ch + 1);
      if (channelNames[ch][0] != '\0')
      {
         msg << " (" << channelNames[ch] << ")";
      }
      msg << " (" << (captureQueueIndex + 1) << "/" << captureQueueCount << ")...";
      statusMessage = msg.str();
      drawStatusFooter();

      if (powerSensor.startCalibrationCapture(ch, knownCurrentAmps, CAPTURE_SLICES_PER_CT))
      {
         return;
      }

      capturedPresent[ch] = true;
      capturedResults[ch] = MuxAdcPowerSensor::CalibrationResult();
      capturedResults[ch].failure = MuxAdcPowerSensor::CalibrationFailure::CAPTURE_UNAVAILABLE;
      captureQueueIndex++;
   }

   mode = CalibrationMode::FROZEN;
   applyAllChannelCalibrations();
   statusMessage = "Captured! Results logged to Hub & Serial";
   drawFullScreen();
   logCapturedResults();
}

///
/// <summary>
/// Queues every currently connected CT channel for sequential high-precision capture.
/// </summary>
///
void beginCaptureAll()
{
   if (knownCurrentAmps <= 0.0f)
   {
      if (!promptKnownCurrent())
      {
         return;
      }
   }

   captureQueueCount = 0;
   captureQueueIndex = 0;
   capturedPresent.fill(false);

   for (size_t i = 0; i < NUM_CT_INPUTS; i++)
   {
      if (powerSensor.isPhysicallyConnected(i))
      {
         captureQueue[captureQueueCount++] = i;
      }
   }

   if (captureQueueCount == 0)
   {
      statusMessage = "No CTs connected to capture";
      drawStatusFooter();
      return;
   }

   mode = CalibrationMode::CAPTURING;
   drawHeader();
   drawActionButtons();
   startNextQueuedCapture();
}

///
/// <summary>
/// Handles capacitive touchscreen taps for parameters, row reference selection, and capture controls.
/// </summary>
///
void handleTouch()
{
   lgfx::touch_point_t tp;
   bool touched = sketch.arduino.display.getTouch(&tp) > 0;
   bool tapped = touched && !wasTouching;
   wasTouching = touched;

   if (!tapped || mode == CalibrationMode::CAPTURING)
   {
      return;
   }

   if (inRect(voltageBoxRect, tp.x, tp.y))
   {
      promptLineVoltage();
      while (sketch.arduino.display.getTouch(&tp) > 0)
      {
         delay(10);
      }
      wasTouching = false;
      return;
   }

   if (inRect(currentBoxRect, tp.x, tp.y))
   {
      promptKnownCurrent();
      while (sketch.arduino.display.getTouch(&tp) > 0)
      {
         delay(10);
      }
      wasTouching = false;
      return;
   }

   if (inRect(captureButtonRect, tp.x, tp.y))
   {
      beginCaptureAll();
      while (sketch.arduino.display.getTouch(&tp) > 0)
      {
         delay(10);
      }
      wasTouching = false;
      return;
   }

   if (mode == CalibrationMode::FROZEN && inRect(resumeButtonRect, tp.x, tp.y))
   {
      mode = CalibrationMode::LIVE;
      statusMessage = "Live updating | Tap Capture All when ready";
      drawFullScreen();
      return;
   }

   for (size_t i = 0; i < visibleRowCount; i++)
   {
      if (inRect(visibleRows[i].rect, tp.x, tp.y))
      {
         if (tp.x < visibleRows[i].nameSplitX)
         {
            promptChannelName(visibleRows[i].channel);
            while (sketch.arduino.display.getTouch(&tp) > 0)
            {
               delay(10);
            }
            wasTouching = false;
            return;
         }

         if (referenceChannel != static_cast<int8_t>(visibleRows[i].channel))
         {
            referenceChannel = static_cast<int8_t>(visibleRows[i].channel);
            drawTableRows();
            drawStatusFooter();
            if (mode == CalibrationMode::FROZEN)
            {
               logCapturedResults();
            }
         }
         return;
      }
   }
}

void setup()
{
   loadVoltageScale();
   loadChannelNames();
   applyAllChannelCalibrations();

   sketch.addSensor("PowerSensor", []() { return powerSensor.begin(); });
   sketch.arduino.setRotation(DisplayRotation::PORTRAIT_FLIP);

   sketch.begin();
   sketch.completeInitialization();

   SerialX::println("CT_Calibration ready.");
   drawFullScreen();
}

void loop()
{
   handleTouch();

   if (mode == CalibrationMode::CAPTURING && powerSensor.isCalibrationCaptureComplete())
   {
      size_t ch = captureQueue[captureQueueIndex];
      powerSensor.consumeCalibrationResult(&capturedResults[ch]);
      capturedPresent[ch] = true;
      captureQueueIndex++;
      startNextQueuedCapture();
   }

   if (uiUpdateTimer.ready())
   {
      updateLiveChannelSamples();
      if (mode == CalibrationMode::LIVE)
      {
         size_t prevCount = visibleRowCount;
         drawParameterBoxes();
         drawTableRows();
         drawStatusFooter();
         if ((prevCount == 0) != (visibleRowCount == 0))
         {
            drawActionButtons();
         }
      }
      else if (mode == CalibrationMode::FROZEN)
      {
         drawParameterBoxes();
      }
   }

   sketch.loop();
}
