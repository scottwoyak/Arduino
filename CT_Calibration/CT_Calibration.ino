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
// - Tap any connected row to designate it as the Reference CT (*); all other rows immediately
//   show their phase offset relative to that reference.
// - Tap "Capture All" to run a 20-slice (~1.1 s per CT) high-precision capture across every
//   connected CT, freeze the results on screen, and print a summary table plus ready-to-paste
//   CT_TYPE_TABLE and DEFAULT_PHASE_SHIFT_DEG entries to Serial.
//

#include <Arduino.h>
#include <Preferences.h>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iomanip>
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

#include "LibraryVersion.h"
#include "MuxAdcPowerSensor.h"
#include "SerialTable.h"
#include "SerialX.h"
#include "Timer.h"
#include "WiFiSettings.h"

#include "DeviceSketch.h"

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
};

// ----------- Runtime State
CalibrationMode mode = CalibrationMode::LIVE;
Timer uiUpdateTimer(UI_UPDATE_INTERVAL_MS);
bool wasTouching = false;
float knownCurrentAmps = 0.0f;
int8_t referenceChannel = -1;
std::string statusMessage = "Plug in CTs, set Load A, then Capture All";

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
std::string lastLineVText;
std::string lastLoadAText;
std::string lastFooterLine1;
std::string lastFooterLine2;

Rect16 voltageBoxRect;
Rect16 currentBoxRect;
Rect16 resumeButtonRect;
Rect16 captureButtonRect;

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
   const float boardPhaseDeg = powerSensor.phaseShiftDeg();
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
         float sliceTotalPhaseDeg = boardPhaseDeg + (std::atan(q / p) * RAD_TO_DEG_F);
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
   sketch.arduino.println("CT Cal", Color::HEADING);

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

   if (force || vText != lastLineVText)
   {
      lastLineVText = vText;
      sketch.arduino.display.fillRoundRect(voltageBoxRect.left(), voltageBoxRect.top(), voltageBoxRect.width, voltageBoxRect.height, CORNER_R, static_cast<uint16_t>(Color::DARKBLUE));
      sketch.arduino.display.drawRoundRect(voltageBoxRect.left(), voltageBoxRect.top(), voltageBoxRect.width, voltageBoxRect.height, CORNER_R, static_cast<uint16_t>(Color::WHITE));
      sketch.arduino.setTextSize(FOOTER_SIZE);
      sketch.arduino.display.setTextColor(static_cast<uint16_t>(Color::SUB_LABEL), static_cast<uint16_t>(Color::DARKBLUE));
      sketch.arduino.display.setCursor(voltageBoxRect.left() + 10, voltageBoxRect.top() + 8);
      sketch.arduino.display.print("Line V (tap)");
      sketch.arduino.setTextSize(TABLE_TEXT_SIZE);
      sketch.arduino.display.setTextColor(static_cast<uint16_t>(vColor), static_cast<uint16_t>(Color::DARKBLUE));
      sketch.arduino.display.setCursor(voltageBoxRect.left() + 10, voltageBoxRect.top() + 34);
      sketch.arduino.display.print(vText.c_str());
   }

   std::ostringstream aStream;
   Color aColor = Color::VALUE2;
   if (knownCurrentAmps > 0.0f)
   {
      aStream << std::fixed << std::setprecision(2) << knownCurrentAmps << " A";
      aColor = Color::VALUE;
   }
   else
   {
      aStream << "Tap to set";
   }
   std::string aText = aStream.str();

   if (force || aText != lastLoadAText)
   {
      lastLoadAText = aText;
      const Color curBorder = knownCurrentAmps > 0.0f ? Color::WHITE : Color::VALUE2;
      sketch.arduino.display.fillRoundRect(currentBoxRect.left(), currentBoxRect.top(), currentBoxRect.width, currentBoxRect.height, CORNER_R, static_cast<uint16_t>(Color::DARKBLUE));
      sketch.arduino.display.drawRoundRect(currentBoxRect.left(), currentBoxRect.top(), currentBoxRect.width, currentBoxRect.height, CORNER_R, static_cast<uint16_t>(curBorder));
      sketch.arduino.setTextSize(FOOTER_SIZE);
      sketch.arduino.display.setTextColor(static_cast<uint16_t>(Color::SUB_LABEL), static_cast<uint16_t>(Color::DARKBLUE));
      sketch.arduino.display.setCursor(currentBoxRect.left() + 10, currentBoxRect.top() + 8);
      sketch.arduino.display.print("Load A (tap)");
      sketch.arduino.setTextSize(TABLE_TEXT_SIZE);
      sketch.arduino.display.setTextColor(static_cast<uint16_t>(aColor), static_cast<uint16_t>(Color::DARKBLUE));
      sketch.arduino.display.setCursor(currentBoxRect.left() + 10, currentBoxRect.top() + 34);
      sketch.arduino.display.print(aText.c_str());
   }
}

///
/// <summary>
/// Draws the static hint, table column header, and divider line.
/// </summary>
///
void drawTableHeader()
{
   const int16_t contentW = sketch.arduino.width() - 2 * MARGIN_X;
   sketch.arduino.setTextSize(FOOTER_SIZE);
   sketch.arduino.setCursor(MARGIN_X, HINT_Y);
   sketch.arduino.println("Tap a row to select Reference CT (*)", Color::SUB_LABEL);

   sketch.arduino.setTextSize(TABLE_TEXT_SIZE);
   sketch.arduino.setCursor(MARGIN_X + 7, TABLE_HEADER_Y);
   sketch.arduino.println("Ch     A/V  Phase  Offset", Color::SUB_LABEL);
   sketch.arduino.display.drawFastHLine(MARGIN_X, TABLE_DIVIDER_Y, contentW, static_cast<uint16_t>(Color::GRAY));
}

///
/// <summary>
/// Formats one 25-character fixed-width row string for the CT table.
/// </summary>
/// <param name="ch">Zero-based channel index.</param>
/// <param name="isRef">True if this channel is the selected reference CT.</param>
/// <param name="colAv">Right-aligned 7-char A/V or status text.</param>
/// <param name="colPhase">Right-aligned 7-char total phase text.</param>
/// <param name="colOffset">Right-aligned 8-char phase offset text.</param>
/// <returns>Formatted 25-character row string.</returns>
///
std::string formatTableRowText(size_t ch, bool isRef, const std::string& colAv, const std::string& colPhase, const std::string& colOffset)
{
   std::ostringstream row;
   if (isRef)
   {
      row << '*';
      if (ch + 1 < 10)
      {
         row << (ch + 1) << ' ';
      }
      else
      {
         row << (ch + 1);
      }
   }
   else
   {
      row << std::setw(2) << (ch + 1) << ' ';
   }

   row << std::setw(7) << colAv
       << std::setw(7) << colPhase
       << std::setw(8) << colOffset;
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

   bool structureChanged = force || (count != lastDrawnRowCount) || (referenceChannel != lastDrawnRefChannel);
   if (!structureChanged)
   {
      for (size_t i = 0; i < count; i++)
      {
         if (activeChannels[i] != lastDrawnChannels[i])
         {
            structureChanged = true;
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
      if (structureChanged)
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
   const uint8_t rowTextSize = rowH >= 32 ? TABLE_TEXT_SIZE : FOOTER_SIZE;
   const int16_t textH = sketch.arduino.charH(rowTextSize);

   for (size_t idx = 0; idx < count; idx++)
   {
      const size_t ch = activeChannels[idx];
      lastDrawnChannels[idx] = ch;
      const int16_t rowY = TABLE_ROWS_TOP_Y + static_cast<int16_t>(idx) * rowStep;
      const Rect16 rect(MARGIN_X, rowY, contentW, rowH);
      visibleRows[idx] = TableRowSlot{ ch, rect };

      const bool isRef = (static_cast<int8_t>(ch) == referenceChannel);
      const Color bg = isRef ? Color::DARKGREEN : Color::DARKBLUE;
      const Color border = isRef ? Color::WHITE : Color::GRAY;
      Color textColor = Color::WHITE;

      std::string colAv = "---";
      std::string colPhase = "---";
      std::string colOffset = "---";

      if (mode == CalibrationMode::FROZEN)
      {
         const MuxAdcPowerSensor::CalibrationResult& res = capturedResults[ch];
         if (res.failure != MuxAdcPowerSensor::CalibrationFailure::NONE)
         {
            colAv = failureShortText(res.failure);
            textColor = Color::VALUE2;
         }
         else
         {
            std::ostringstream avS;
            avS << std::fixed << std::setprecision(2) << res.ampsPerVolt;
            colAv = avS.str();

            std::ostringstream phS;
            phS << std::fixed << std::setprecision(2) << res.totalPhaseDeg;
            colPhase = phS.str();

            if (hasRefPhase)
            {
               float offsetDeg = isRef ? 0.0f : (res.totalPhaseDeg - refPhaseDeg);
               std::ostringstream offS;
               offS << std::fixed << std::showpos << std::setprecision(2) << offsetDeg;
               colOffset = offS.str();
            }
         }
      }
      else
      {
         const LiveChannelState& st = liveChannels[ch];
         if (!st.hasSignal)
         {
            colAv = "low";
            textColor = Color::LIGHTGRAY;
         }
         else
         {
            if (knownCurrentAmps > 0.0f)
            {
               std::ostringstream avS;
               avS << std::fixed << std::setprecision(2) << (knownCurrentAmps / st.emaRmsVolts);
               colAv = avS.str();
            }
            else
            {
               colAv = "set A";
            }

            if (st.hasPhase)
            {
               std::ostringstream phS;
               phS << std::fixed << std::setprecision(2) << st.emaTotalPhaseDeg;
               colPhase = phS.str();

               if (hasRefPhase)
               {
                  float offsetDeg = isRef ? 0.0f : (st.emaTotalPhaseDeg - refPhaseDeg);
                  std::ostringstream offS;
                  offS << std::fixed << std::showpos << std::setprecision(2) << offsetDeg;
                  colOffset = offS.str();
               }
            }
         }
      }

      if (structureChanged)
      {
         sketch.arduino.display.fillRoundRect(rect.left(), rect.top(), rect.width, rect.height, CORNER_R, static_cast<uint16_t>(bg));
         sketch.arduino.display.drawRoundRect(rect.left(), rect.top(), rect.width, rect.height, CORNER_R, static_cast<uint16_t>(border));
         if (isRef)
         {
            sketch.arduino.display.drawRoundRect(rect.left() + 1, rect.top() + 1, rect.width - 2, rect.height - 2, CORNER_R, static_cast<uint16_t>(border));
         }
      }

      std::string rowText = formatTableRowText(ch, isRef, colAv, colPhase, colOffset);
      if (structureChanged || rowText != lastDrawnRowTexts[idx])
      {
         lastDrawnRowTexts[idx] = rowText;
         sketch.arduino.setTextSize(rowTextSize);
         sketch.arduino.display.setTextColor(static_cast<uint16_t>(textColor), static_cast<uint16_t>(bg));
         sketch.arduino.display.setCursor(rect.left() + 7, rect.top() + (rect.height - textH) / 2);
         sketch.arduino.display.print(rowText.c_str());
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

   if (force || line1 != lastFooterLine1)
   {
      lastFooterLine1 = line1;
      sketch.arduino.display.fillRect(0, FOOTER_LINE1_Y, sketch.arduino.width(), FOOTER_LINE2_Y - FOOTER_LINE1_Y, static_cast<uint16_t>(Color::BLACK));
      sketch.arduino.setTextSize(FOOTER_SIZE);
      sketch.arduino.setCursor(MARGIN_X, FOOTER_LINE1_Y);
      sketch.arduino.println(line1.c_str(), line1Color);
   }

   if (force || statusMessage != lastFooterLine2)
   {
      lastFooterLine2 = statusMessage;
      sketch.arduino.display.fillRect(0, FOOTER_LINE2_Y, sketch.arduino.width(), FOOTER_BOTTOM_Y - FOOTER_LINE2_Y, static_cast<uint16_t>(Color::BLACK));
      sketch.arduino.setTextSize(FOOTER_SIZE);
      sketch.arduino.setCursor(MARGIN_X, FOOTER_LINE2_Y);
      sketch.arduino.println(statusMessage.c_str(), Color::SUB_LABEL);
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
   lastLoadAText.clear();
   lastFooterLine1.clear();
   lastFooterLine2.clear();
   for (std::string& s : lastDrawnRowTexts)
   {
      s.clear();
   }
   drawHeader();
   drawParameterBoxes(true);
   drawTableHeader();
   drawTableRows(true);
   drawStatusFooter(true);
   drawActionButtons();
}

///
/// <summary>
/// Prints the captured multi-CT calibration table and copyable CT_TYPE_TABLE snippet to Serial.
/// </summary>
///
void printCapturedResultsToSerial()
{
   float refPhaseDeg = 0.0f;
   bool hasRefPhase = getReferencePhaseDeg(&refPhaseDeg);

   static constexpr std::array<SerialTable::Column, 6> SERIAL_COLUMNS = { {
      { "Ch", 6 },
      { "mV RMS", 9 },
      { "A/V", 9 },
      { "Phase", 9 },
      { "Offset", 9 },
      { "Spread", 9 },
   } };

   SerialTable table("CT Calibration Results", SERIAL_COLUMNS);
   table.printHeader(false);

   for (size_t i = 0; i < NUM_CT_INPUTS; i++)
   {
      if (!capturedPresent[i])
      {
         continue;
      }

      const bool isRef = (static_cast<int8_t>(i) == referenceChannel);
      std::string chLabel = (isRef ? "*" : " ") + std::to_string(i + 1);
      const MuxAdcPowerSensor::CalibrationResult& res = capturedResults[i];

      if (res.failure != MuxAdcPowerSensor::CalibrationFailure::NONE)
      {
         table.printRow(false, chLabel.c_str(), failureShortText(res.failure), "---", "---", "---", "---");
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

      table.printRow(
         false,
         chLabel.c_str(),
         SerialTable::fixed(res.sensorRmsVolts * 1000.0f, 2),
         SerialTable::fixed(res.ampsPerVolt, 2),
         SerialTable::fixed(res.totalPhaseDeg, 2),
         offStr.str().c_str(),
         spreadStr.str().c_str());
   }

   SerialX::println();
   if (hasRefPhase)
   {
      std::ostringstream refLine;
      refLine << std::fixed << std::setprecision(2)
              << "DEFAULT_PHASE_SHIFT_DEG = " << refPhaseDeg << "f;  // Ch " << (referenceChannel + 1);
      SerialX::println(refLine.str().c_str());
   }

   SerialX::println("// Copy into Power_Publisher CT_TYPE_TABLE:");
   for (size_t i = 0; i < NUM_CT_INPUTS; i++)
   {
      if (!capturedPresent[i] || capturedResults[i].failure != MuxAdcPowerSensor::CalibrationFailure::NONE)
      {
         continue;
      }

      const bool isRef = (static_cast<int8_t>(i) == referenceChannel);
      float offsetDeg = hasRefPhase ? (isRef ? 0.0f : (capturedResults[i].totalPhaseDeg - refPhaseDeg)) : 0.0f;

      std::ostringstream entry;
      entry << std::fixed << std::setprecision(2)
            << "   { \"Ch" << (i + 1) << "\", 100, "
            << capturedResults[i].ampsPerVolt << "f, "
            << std::showpos << offsetDeg << "f },";
      SerialX::println(entry.str().c_str());
   }
   SerialX::println();
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
            printCapturedResultsToSerial();
            statusMessage = "Updated A/V from new Load A (see Serial)";
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
/// Starts the next queued channel capture, or finalizes the batch when all channels finish.
/// </summary>
///
void startNextQueuedCapture()
{
   while (captureQueueIndex < captureQueueCount)
   {
      size_t ch = captureQueue[captureQueueIndex];
      std::ostringstream msg;
      msg << "Capturing Ch " << (ch + 1) << " (" << (captureQueueIndex + 1) << "/" << captureQueueCount << ")...";
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
   statusMessage = "Captured! Results printed to Serial";
   drawFullScreen();
   printCapturedResultsToSerial();
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
         if (referenceChannel != static_cast<int8_t>(visibleRows[i].channel))
         {
            referenceChannel = static_cast<int8_t>(visibleRows[i].channel);
            drawTableRows();
            drawStatusFooter();
            if (mode == CalibrationMode::FROZEN)
            {
               printCapturedResultsToSerial();
            }
         }
         return;
      }
   }
}

void setup()
{
   loadVoltageScale();

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
