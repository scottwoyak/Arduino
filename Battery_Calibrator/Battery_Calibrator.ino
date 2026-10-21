//
// Battery Calibrator
//
// Continuously monitors and logs a fully guided, button-free battery calibration run to
// InfluxDB so empirical voltage-to-percent curves, full-charge plateau voltages, and
// cutoff thresholds can be derived for each board type.
//
// Guided 4-Step Flow (no button presses required):
// 1. Pre-Charge to Full (Phase 1):
//    - Starts here automatically from any initial battery state.
//    - Prompts "Plug In USB" if discharging, then shows "Charging to Full" while charging up
//      to a confirmed full plateau (18 full uploads = 3 minutes at Full).
//    - Once full, plays a tone, saves Phase 2 (Discharge) to NVS, and displays
//      "Unplug USB" in green.
// 2. Full Discharge to Cutoff (Phase 2):
//    - As soon as you unplug power (detected automatically when state becomes Discharging,
//      or after a power-transition reset), advances to Phase 3 in NVS so the subsequent
//      brownout/cutoff reboot after the battery dies will boot straight into Phase 3!
//    - Logs the entire 100% -> 0% discharge curve every 10 seconds until the battery hits
//      cutoff and the screen turns off.
// 3. Full Charge from Cutoff to Full (Phase 3):
//    - When you notice the screen is off and plug power back in, the board boots directly
//      into Phase 3 (Full Charge).
//    - Logs the entire 0% -> 100% charge curve every 10 seconds until the battery reaches
//      Full and holds it for 18 uploads (3 minutes).
// 4. Calibration Complete (Phase 4):
//    - Plays a completion tone and displays "Calibration Complete" ("Cal Complete" on Feather).
//    - (Optional: pressing buttonA or tapping the screen when Done restarts at Phase 1.)
//

#include <Arduino.h>
#include <cmath>
#include <sstream>
#include <string>

#include "ArduinoBoard.h"

#if !defined(ARDUINO_DISPLAY_SUPPORTED) || !defined(ARDUINO_BATTERY_SUPPORTED)
#error "This sketch requires a board with a display and battery support."
#include "WrongBoard.h"
#endif

#include "LibraryVersion.h"
#include "Stopwatch.h"
#include "Table.h"
#include "Timer.h"

#include "WiFiSettings.h"

#include "MonitorSketch.h"

const auto VERSION = MakeVersion("1.1");
constexpr auto SKETCH_NAME = "Battery_Calibrator";
constexpr auto PREFERENCES_NAMESPACE = "BatteryCal";
constexpr auto PHASE_PREF_KEY = "phase";

constexpr float SAMPLE_RATE = 1.0f;
constexpr float SERIAL_LOG_RATE = 1.0f;
constexpr uint8_t INFLUX_INTERVAL_S = 10;
constexpr uint16_t FULL_SOAK_SPAN_S = 180;          // 3 min at Full
constexpr uint8_t DISCHARGE_CONFIRM_UPLOADS = 3;    // 3 uploads * 10s = 30s discharging before arming reboot-into-Charge
constexpr uint8_t VOLTS_DECIMAL_PLACES = 3;
constexpr uint8_t CURRENT_DECIMAL_PLACES = 1;
constexpr uint8_t PERCENT_DECIMAL_PLACES = 1;
constexpr uint8_t RATE_DECIMAL_PLACES = 2;
constexpr float DISPLAY_UPDATE_RATE = 5.0f;
constexpr float PHASE_TONE_FREQ_HZ = 880.0f;
constexpr uint16_t PHASE_TONE_DURATION_MS = 150;

#if defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S3_TFT)
constexpr auto HEADING_TEXT = "Bat Calibrator";
constexpr auto TABLE_TEXT_FORMAT = "###############";
constexpr auto DETAILS_UNPLUG_POWER = "Unplug USB";
constexpr auto DETAILS_CONNECT_BATTERY = "Connect Batt";
constexpr auto DETAILS_PLUG_IN_POWER = "Plug In USB";
constexpr auto DETAILS_CHARGE_TO_FULL = "Charging to Full";
constexpr auto DETAILS_FULL_CONFIRMING_PREFIX = "Confirming ";
constexpr auto DETAILS_DISCHARGING = "Discharging";
constexpr auto DETAILS_CHARGING = "Charging";
constexpr auto DETAILS_DONE = "Cal Complete";
#else
constexpr auto HEADING_TEXT = "Battery Calibrator";
constexpr auto TABLE_TEXT_FORMAT = "#####################";
constexpr auto DETAILS_UNPLUG_POWER = "Unplug USB";
constexpr auto DETAILS_CONNECT_BATTERY = "Connect Battery";
constexpr auto DETAILS_PLUG_IN_POWER = "Plug In USB";
constexpr auto DETAILS_CHARGE_TO_FULL = "Charging to Full";
constexpr auto DETAILS_FULL_CONFIRMING_PREFIX = "Full: Confirming ";
constexpr auto DETAILS_DISCHARGING = "Discharging";
constexpr auto DETAILS_CHARGING = "Charging";
constexpr auto DETAILS_DONE = "Calibration Complete";
#endif

///
/// <summary>
/// Guided calibration phase persisted in NVS and uploaded with each InfluxDB point.
/// </summary>
///
enum class CalibrationPhase : uint8_t
{
   PreCharge = 1,       // Step 1: Charge to Full, then prompt user to unplug
   Discharge = 2,       // Step 2: Unplugged full discharge down to cutoff (screen off)
   Charge = 3,          // Step 3: Plugged back in after cutoff; full charge 0% -> 100%
   Done = 4             // Step 4: Full discharge + full charge calibration complete
};

SketchConfig SKETCH_CONFIG = {
   .sketchName = SKETCH_NAME,
   .version = VERSION,
   .preferencesNamespace = PREFERENCES_NAMESPACE,
   .cpuFrequencyMhz = 80,
   .enableOTA = true,
};

InfluxConfig INFLUX_CONFIG = {
   .context = { INFLUXDB_BUCKET_7_DAY, "Bragg", nullptr },
   .promptForContext = true,
   .includeCpuTemp = true,
};

MonitorSketch sketch(SKETCH_CONFIG, INFLUX_CONFIG);

InfluxField* voltsField = nullptr;
InfluxField* currentMaField = nullptr;
InfluxField* minVoltsField = nullptr;
InfluxField* maxVoltsField = nullptr;
InfluxField* spanVoltsField = nullptr;
InfluxField* rippleVoltsField = nullptr;
InfluxField* riseVoltsField = nullptr;
InfluxField* fallVoltsField = nullptr;
InfluxField* sessionMinVoltsField = nullptr;
InfluxField* sessionMaxVoltsField = nullptr;
InfluxField* percentField = nullptr;
InfluxField* rawPercentField = nullptr;
InfluxField* chargeRateField = nullptr;
InfluxField* connectedField = nullptr;
InfluxField* stateField = nullptr;
InfluxField* phaseField = nullptr;
InfluxField* elapsedSecsField = nullptr;
InfluxField* fullVoltsField = nullptr;

float intervalMinVolts = NAN;
float intervalMaxVolts = NAN;
float intervalMaxSpanVolts = 0.0f;
float intervalMaxRippleVolts = 0.0f;
float intervalMaxRiseVolts = 0.0f;
float intervalMaxFallVolts = 0.0f;
bool intervalAllConnected = true;

float sessionMinVolts = NAN;
float sessionMaxVolts = NAN;
uint32_t phaseUploads = 0;
uint16_t fullSoakSecs = 0;
uint8_t dischargeConfirmUploads = 0;
bool waitingForUnplug = false;
bool armedChargeOnNextBoot = false;
CalibrationPhase currentPhase = CalibrationPhase::PreCharge;
bool touchWasDown = false;

Stopwatch phaseStopwatch;
RateTimer sampleTimer(SAMPLE_RATE);
TimerSecs uploadTimer(INFLUX_INTERVAL_S);
RateTimer displayTimer(DISPLAY_UPDATE_RATE);
RateTimer serialLogTimer(SERIAL_LOG_RATE);
Table table(&sketch.arduino, 0, 0);

///
/// <summary>
/// Resets the per-upload-interval extreme trackers after each InfluxDB post.
/// </summary>
///
void resetIntervalExtremes()
{
   intervalMinVolts = NAN;
   intervalMaxVolts = NAN;
   intervalMaxSpanVolts = 0.0f;
   intervalMaxRippleVolts = 0.0f;
   intervalMaxRiseVolts = 0.0f;
   intervalMaxFallVolts = 0.0f;
   intervalAllConnected = true;
}

///
/// <summary>
/// Resets the session-wide tracking metrics when starting or switching a calibration phase.
/// </summary>
///
void resetPhaseSession()
{
   sessionMinVolts = NAN;
   sessionMaxVolts = NAN;
   phaseUploads = 0;
   fullSoakSecs = 0;
   dischargeConfirmUploads = 0;
   waitingForUnplug = false;
   armedChargeOnNextBoot = false;
   phaseStopwatch.reset();
   uploadTimer.reset();
   resetIntervalExtremes();
}

///
/// <summary>
/// Returns the display/log label for the given calibration phase.
/// </summary>
/// <param name="phase">The calibration phase to format.</param>
/// <returns>Short phase label string.</returns>
///
const char* formatPhase(CalibrationPhase phase)
{
   switch (phase)
   {
      case CalibrationPhase::PreCharge:
         return "1:PreChg";
      case CalibrationPhase::Discharge:
         return "2:Dischg";
      case CalibrationPhase::Charge:
         return "3:Charge";
      case CalibrationPhase::Done:
         return "4:Done";
      default:
         return "Unknown";
   }
}

///
/// <summary>
/// Writes a phase value to Preferences (NVS) without changing the active in-memory phase.
/// Used to pre-arm the next boot phase (e.g. so unplugging power or dying at cutoff boots
/// into the next phase automatically).
/// </summary>
/// <param name="nextBootPhase">The phase to load on the next boot.</param>
///
void saveNextBootPhase(CalibrationPhase nextBootPhase)
{
   sketch.arduino.preferences.begin(PREFERENCES_NAMESPACE, false);
   sketch.arduino.preferences.putUChar(PHASE_PREF_KEY, static_cast<uint8_t>(nextBootPhase));
   sketch.arduino.preferences.end();
}

///
/// <summary>
/// Loads the persisted calibration phase from Preferences so a power-transition or
/// low-voltage cutoff reboot automatically starts in the right phase.
/// </summary>
///
void loadPhase()
{
   sketch.arduino.preferences.begin(PREFERENCES_NAMESPACE, true);
   uint8_t saved = sketch.arduino.preferences.getUChar(
      PHASE_PREF_KEY,
      static_cast<uint8_t>(CalibrationPhase::PreCharge));
   sketch.arduino.preferences.end();

   if (saved < static_cast<uint8_t>(CalibrationPhase::PreCharge) ||
       saved >= static_cast<uint8_t>(CalibrationPhase::Done))
   {
      saved = static_cast<uint8_t>(CalibrationPhase::PreCharge);
      saveNextBootPhase(CalibrationPhase::PreCharge);
   }

   currentPhase = static_cast<CalibrationPhase>(saved);
   resetPhaseSession();
}

///
/// <summary>
/// Transitions immediately to the given calibration phase, persists it to Preferences,
/// and resets the phase session timer and min/max voltage trackers.
/// </summary>
/// <param name="phase">The new calibration phase.</param>
///
void setPhase(CalibrationPhase phase)
{
   currentPhase = phase;
   resetPhaseSession();
   saveNextBootPhase(currentPhase);

   Serial.printf("\n=== CALIBRATION PHASE: %s ===\n", formatPhase(currentPhase));
#ifdef ARDUINO_SOUND_SUPPORTED
   sketch.arduino.sound.playTone(PHASE_TONE_FREQ_HZ, PHASE_TONE_DURATION_MS);
#endif
}

///
/// <summary>
/// Checks whether the user pressed buttonA or tapped the touch screen to toggle the
/// calibration phase between Discharge and Charge.
/// </summary>
/// <returns>True if an input press/tap was detected; otherwise false.</returns>
///
bool checkTogglePressed()
{
#ifdef ARDUINO_BUTTON_A_SUPPORTED
   if (sketch.arduino.buttonA.wasPressed())
   {
      return true;
   }
#endif

#ifdef ARDUINO_TOUCH_SUPPORTED
   lgfx::touch_point_t tp;
   bool touching = (sketch.arduino.display.getTouch(&tp) > 0);
   if (touching && !touchWasDown)
   {
      touchWasDown = true;
      return true;
   }
   if (!touching)
   {
      touchWasDown = false;
   }
#endif

   return false;
}

///
/// <summary>
/// Converts a BatteryChargingState enum value into its display/status label.
/// </summary>
/// <param name="state">The charging state to format.</param>
/// <returns>Human-readable charging state string.</returns>
///
const char* formatChargingState(BatteryChargingState state)
{
   if (state == BatteryChargingState::BadCharger)
   {
      return "Bad Charger";
   }
   if (state == BatteryChargingState::Full)
   {
      return "Full";
   }
   if (state == BatteryChargingState::Yes)
   {
      return "Charging";
   }
   if (state == BatteryChargingState::No)
   {
      return "Discharging";
   }
   return "Unknown";
}

///
/// <summary>
/// Formats an elapsed duration in seconds into a compact "Xh YYm" or "Ym ZZs" string.
/// </summary>
/// <param name="totalSecs">Elapsed time in seconds.</param>
/// <returns>Formatted duration string.</returns>
///
String formatElapsed(uint32_t totalSecs)
{
   uint32_t hours = totalSecs / 3600UL;
   uint32_t mins = (totalSecs % 3600UL) / 60UL;
   uint32_t secs = totalSecs % 60UL;

   if (hours > 0)
   {
      String minsStr = (mins < 10 ? "0" : "") + String(mins);
      return String(hours) + "h" + minsStr + "m";
   }

   String secsStr = (secs < 10 ? "0" : "") + String(secs);
   return String(mins) + "m" + secsStr + "s";
}

///
/// <summary>
/// Formats the latest battery calibration readings for the "GetReadings" DeviceServer command.
/// </summary>
/// <returns>Formatted readings text.</returns>
///
std::string getReadings()
{
   bool connected = sketch.arduino.battery.isConnected();
   float rawVolts = sketch.arduino.battery.readVolts();
   float avgVolts = sketch.arduino.battery.readAverageVolts();
   float avgCurrentMa = sketch.arduino.battery.readAverageCurrentMa();
   float avgPercent = connected ? sketch.arduino.battery.readAveragePercent() : NAN;
   float rippleVolts = sketch.arduino.battery.readRippleVolts();
   float chargeRate = sketch.arduino.battery.readChargeRate();
   BatteryChargingState state = sketch.arduino.battery.readChargingState();
   uint32_t elapsedSecs = static_cast<uint32_t>(phaseStopwatch.elapsedSecs());

   std::ostringstream out;
   out << std::fixed;
   out.precision(3);
   out << "phase=" << formatPhase(currentPhase) << " (" << elapsedSecs << "s, uploads=" << phaseUploads << ")\n";
   out << "connected=" << (connected ? "true" : "false") << "\n";
   out << "volts=" << avgVolts << " V (raw " << rawVolts << " V)\n";
   if (isfinite(sessionMinVolts) && isfinite(sessionMaxVolts))
   {
      out << "sessionRange=" << sessionMinVolts << ".." << sessionMaxVolts << " V\n";
   }
   out << "ripple=" << rippleVolts << " V\n";
   out.precision(1);
   if (isfinite(avgCurrentMa))
   {
      out << "current=" << avgCurrentMa << " mA\n";
   }
   if (isfinite(avgPercent))
   {
      out << "percent=" << avgPercent << " %\n";
   }
   out << "state=" << formatChargingState(state);
   if (isfinite(chargeRate))
   {
      out << "\nchargeRate=" << chargeRate << " %/hr";
   }
   return out.str();
}

///
/// <summary>
/// Positions the battery calibration table below the static heading.
/// </summary>
///
void drawStaticHeading()
{
   sketch.arduino.setCursor(0, 0);
   sketch.arduino.setTextSize(DEFAULT_HEADING_SIZE);
   sketch.arduino.println(HEADING_TEXT, Color::HEADING);
#if !defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S3_TFT)
   sketch.arduino.moveCursorY(sketch.arduino.charH() / 4);
#endif

   table.setPosition(0, sketch.arduino.getCursorY());
   table.invalidate();
}

void setup()
{
   sketch.onReadings(getReadings, "Reports the latest battery calibration voltage, range, percentage, and phase");

   sketch.begin();

   InfluxPoint* point = sketch.addPoint({ { "item", "Battery" } });
   voltsField = point->addTimeAverageField(INFLUX_INTERVAL_S, "batteryVolts", VOLTS_DECIMAL_PLACES);
   currentMaField = point->addTimeAverageField(INFLUX_INTERVAL_S, "batteryCurrentMa", CURRENT_DECIMAL_PLACES);
   minVoltsField = point->addValueField("batteryMinVolts", VOLTS_DECIMAL_PLACES);
   maxVoltsField = point->addValueField("batteryMaxVolts", VOLTS_DECIMAL_PLACES);
   spanVoltsField = point->addValueField("batterySpanVolts", VOLTS_DECIMAL_PLACES);
   rippleVoltsField = point->addValueField("batteryRippleVolts", VOLTS_DECIMAL_PLACES);
   riseVoltsField = point->addValueField("batteryRiseVolts", VOLTS_DECIMAL_PLACES);
   fallVoltsField = point->addValueField("batteryFallVolts", VOLTS_DECIMAL_PLACES);
   sessionMinVoltsField = point->addValueField("batterySessionMinVolts", VOLTS_DECIMAL_PLACES);
   sessionMaxVoltsField = point->addValueField("batterySessionMaxVolts", VOLTS_DECIMAL_PLACES);
   percentField = point->addTimeAverageField(INFLUX_INTERVAL_S, "batteryPercent", PERCENT_DECIMAL_PLACES);
   rawPercentField = point->addTimeAverageField(INFLUX_INTERVAL_S, "batteryRawPercent", PERCENT_DECIMAL_PLACES);
   chargeRateField = point->addTimeAverageField(INFLUX_INTERVAL_S, "batteryChargeRate", RATE_DECIMAL_PLACES);
   connectedField = point->addValueField("batteryConnected", 0);
   stateField = point->addValueField("batteryState", 0);
   phaseField = point->addValueField("batteryPhase", 0);
   elapsedSecsField = point->addValueField("batteryElapsedSecs", 0);
   fullVoltsField = point->addValueField("batteryFullVolts", VOLTS_DECIMAL_PLACES);

   table.addRow("Volts:", "#.###v");
   table.addRow("State:", TABLE_TEXT_FORMAT);
   table.addRow("Details:", TABLE_TEXT_FORMAT);
   table.addRow("Elapsed:", TABLE_TEXT_FORMAT);

   sketch.completeInitialization();

#ifdef ARDUINO_BUTTON_A_SUPPORTED
   sketch.arduino.buttonA.reset();
#endif
   loadPhase();

   drawStaticHeading();
}

void loop()
{
   sketch.loop();

   // Optional manual advance/reset if needed (e.g. to restart when Done)
   if (checkTogglePressed())
   {
      uint8_t next = static_cast<uint8_t>(currentPhase) + 1;
      if (next > static_cast<uint8_t>(CalibrationPhase::Done))
      {
         next = static_cast<uint8_t>(CalibrationPhase::PreCharge);
      }
      setPhase(static_cast<CalibrationPhase>(next));
   }

   float rawVolts = sketch.arduino.battery.readVolts();
   float avgVolts = sketch.arduino.battery.readAverageVolts();
   float avgCurrentMa = sketch.arduino.battery.readAverageCurrentMa();
   bool hasCurrent = isfinite(avgCurrentMa);
   bool connected = sketch.arduino.battery.isConnected();
   float avgPercent = connected ? sketch.arduino.battery.readAveragePercent() : NAN;
   float rawPercent = sketch.arduino.battery.readRawPercent();
   float rippleVolts = sketch.arduino.battery.readRippleVolts();
   float windowMinVolts = sketch.arduino.battery.readWindowMinVolts();
   float windowMaxVolts = sketch.arduino.battery.readWindowMaxVolts();
   float windowSpanVolts = sketch.arduino.battery.readWindowSpanVolts();
   float windowRiseVolts = sketch.arduino.battery.readWindowRiseVolts();
   float windowFallVolts = sketch.arduino.battery.readWindowFallVolts();
   BatteryChargingState chargingState = sketch.arduino.battery.readChargingState();
   float chargeRate = sketch.arduino.battery.readChargeRate();
   bool hasChargeRate = isfinite(chargeRate);

   // In Phase 1 (PreCharge), once full soak has completed and we're prompting "UNPLUG PWR!",
   // automatically advance to Phase 2 (Discharge) as soon as unplugging is detected (for
   // boards that don't reset on USB unplug). If the board DOES reset on unplug, we already
   // saved Discharge to NVS when waitingForUnplug became true, so it boots into Discharge!
   if (currentPhase == CalibrationPhase::PreCharge &&
       waitingForUnplug &&
       connected &&
       chargingState == BatteryChargingState::No)
   {
      setPhase(CalibrationPhase::Discharge);
   }

   if (connected && isfinite(avgVolts))
   {
      if (!isfinite(sessionMinVolts) || avgVolts < sessionMinVolts)
      {
         sessionMinVolts = avgVolts;
      }
      if (!isfinite(sessionMaxVolts) || avgVolts > sessionMaxVolts)
      {
         sessionMaxVolts = avgVolts;
      }
   }

   if (sampleTimer.ready())
   {
      if (isfinite(avgVolts))
      {
         voltsField->set(avgVolts);
      }
      if (hasCurrent)
      {
         currentMaField->set(avgCurrentMa);
      }
      if (connected && isfinite(avgPercent))
      {
         percentField->set(avgPercent);
      }
      if (isfinite(rawPercent))
      {
         rawPercentField->set(rawPercent);
      }
      if (hasChargeRate)
      {
         chargeRateField->set(chargeRate);
      }

      float sampleMin = isfinite(windowMinVolts) ? windowMinVolts : rawVolts;
      float sampleMax = isfinite(windowMaxVolts) ? windowMaxVolts : rawVolts;
      if (isfinite(sampleMin) && (!isfinite(intervalMinVolts) || sampleMin < intervalMinVolts))
      {
         intervalMinVolts = sampleMin;
      }
      if (isfinite(sampleMax) && (!isfinite(intervalMaxVolts) || sampleMax > intervalMaxVolts))
      {
         intervalMaxVolts = sampleMax;
      }
      if (windowSpanVolts > intervalMaxSpanVolts)
      {
         intervalMaxSpanVolts = windowSpanVolts;
      }
      if (rippleVolts > intervalMaxRippleVolts)
      {
         intervalMaxRippleVolts = rippleVolts;
      }
      if (windowRiseVolts > intervalMaxRiseVolts)
      {
         intervalMaxRiseVolts = windowRiseVolts;
      }
      if (windowFallVolts > intervalMaxFallVolts)
      {
         intervalMaxFallVolts = windowFallVolts;
      }
      if (!connected)
      {
         intervalAllConnected = false;
      }

      if (currentPhase == CalibrationPhase::PreCharge)
      {
         if (connected && chargingState == BatteryChargingState::Full)
         {
            if (fullSoakSecs < FULL_SOAK_SPAN_S)
            {
               fullSoakSecs++;
            }
            if (fullSoakSecs >= FULL_SOAK_SPAN_S && !waitingForUnplug)
            {
               waitingForUnplug = true;
               // Pre-save Discharge to NVS so if unplugging causes a board reset, it boots
               // straight into Phase 2 (Discharge).
               saveNextBootPhase(CalibrationPhase::Discharge);
               Serial.println("\n>>> PRE-CHARGE COMPLETE! UNPLUG EXTERNAL POWER NOW <<<");
#ifdef ARDUINO_SOUND_SUPPORTED
               sketch.arduino.sound.playTone(PHASE_TONE_FREQ_HZ, PHASE_TONE_DURATION_MS);
#endif
            }
         }
      }
      else if (currentPhase == CalibrationPhase::Charge)
      {
         if (connected && chargingState == BatteryChargingState::Full)
         {
            if (fullSoakSecs < FULL_SOAK_SPAN_S)
            {
               fullSoakSecs++;
            }
            if (fullSoakSecs >= FULL_SOAK_SPAN_S)
            {
               Serial.println("\n>>> FULL CHARGE CALIBRATION COMPLETE! <<<");
               setPhase(CalibrationPhase::Done);
            }
         }
      }
   }

   uint32_t elapsedSecs = static_cast<uint32_t>(phaseStopwatch.elapsedSecs());

   if (uploadTimer.ready())
   {
      float minV = isfinite(intervalMinVolts) ? intervalMinVolts : rawVolts;
      float maxV = isfinite(intervalMaxVolts) ? intervalMaxVolts : rawVolts;
      if (isfinite(minV))
      {
         minVoltsField->set(minV);
      }
      if (isfinite(maxV))
      {
         maxVoltsField->set(maxV);
      }
      if (isfinite(sessionMinVolts))
      {
         sessionMinVoltsField->set(sessionMinVolts);
      }
      if (isfinite(sessionMaxVolts))
      {
         sessionMaxVoltsField->set(sessionMaxVolts);
      }
      spanVoltsField->set(intervalMaxSpanVolts);
      rippleVoltsField->set(intervalMaxRippleVolts);
      riseVoltsField->set(intervalMaxRiseVolts);
      fallVoltsField->set(intervalMaxFallVolts);
      connectedField->set((connected && intervalAllConnected) ? 1.0f : 0.0f);
      stateField->set(static_cast<float>(chargingState));
      phaseField->set(static_cast<float>(currentPhase));
      elapsedSecsField->set(elapsedSecs);
      fullVoltsField->set(sketch.arduino.battery.readFullChargeVolts());

      sketch.postPoints();
      resetIntervalExtremes();
      phaseUploads++;

      if (currentPhase == CalibrationPhase::Discharge)
      {
         if (connected && chargingState == BatteryChargingState::No)
         {
            if (dischargeConfirmUploads < DISCHARGE_CONFIRM_UPLOADS)
            {
               dischargeConfirmUploads++;
            }
            if (dischargeConfirmUploads >= DISCHARGE_CONFIRM_UPLOADS && !armedChargeOnNextBoot)
            {
               armedChargeOnNextBoot = true;
               // Pre-save Charge to NVS so when the battery eventually dies at cutoff and
               // the user plugs power back in, it boots straight into Phase 3 (Charge)!
               saveNextBootPhase(CalibrationPhase::Charge);
               Serial.println("\n>>> DISCHARGE CONFIRMED: ARMED PHASE 3 (CHARGE) FOR NEXT BOOT AFTER CUTOFF <<<");
            }
         }
      }
   }

   const char* stateText = formatChargingState(chargingState);
   Color stateColor = (chargingState == BatteryChargingState::BadCharger) ? Color::RED : Color::VALUE;

   if (serialLogTimer.ready())
   {
      Serial.printf(
         "[%lus] phase=%s(%lus, #%lu)  volts=%.3fV [%.3f..%.3fV]  current=%.1fmA  pct=%.1f%%  state=%s\n",
         millis() / 1000UL,
         formatPhase(currentPhase),
         static_cast<unsigned long>(elapsedSecs),
         static_cast<unsigned long>(phaseUploads),
         avgVolts,
         sessionMinVolts,
         sessionMaxVolts,
         avgCurrentMa,
         avgPercent,
         stateText);
   }

   if (!displayTimer.ready())
   {
      return;
   }

   // Row 0: Live battery voltage
   table.setValue(0, avgVolts);

   // Row 1: Battery state + current percentage / mA (or No Battery)
   if (!connected)
   {
      table.setValue(1, "No Battery", Color::RED);
   }
   else if (hasCurrent)
   {
      int16_t pct = static_cast<int16_t>(roundf(avgPercent));
      int16_t currentInt = static_cast<int16_t>(roundf(avgCurrentMa));
      String statePctStr = String(pct) + "% " + (currentInt >= 0 ? "+" : "") + String(currentInt) + "mA";
      table.setValue(1, statePctStr, stateColor);
   }
   else
   {
      int16_t pct = static_cast<int16_t>(roundf(avgPercent));
      String statePctStr = String(stateText) + " " + String(pct) + "%";
      table.setValue(1, statePctStr, stateColor);
   }

   // Row 2: Guided calibration details / instruction status
   if (currentPhase == CalibrationPhase::PreCharge)
   {
      if (waitingForUnplug)
      {
         table.setValue(2, DETAILS_UNPLUG_POWER, Color::LIME);
      }
      else if (!connected)
      {
         table.setValue(2, DETAILS_CONNECT_BATTERY, Color::RED);
      }
      else if (chargingState == BatteryChargingState::No)
      {
         table.setValue(2, DETAILS_PLUG_IN_POWER, Color::LIME);
      }
      else if (fullSoakSecs == 0)
      {
         table.setValue(2, DETAILS_CHARGE_TO_FULL, Color::YELLOW);
      }
      else
      {
         uint16_t remSecs = FULL_SOAK_SPAN_S - fullSoakSecs;
         table.setValue(2, String(DETAILS_FULL_CONFIRMING_PREFIX) + String(remSecs) + "s", Color::YELLOW);
      }
   }
   else if (currentPhase == CalibrationPhase::Discharge)
   {
      if (chargingState == BatteryChargingState::Yes || chargingState == BatteryChargingState::Full)
      {
         table.setValue(2, DETAILS_UNPLUG_POWER, Color::LIME);
      }
      else
      {
         table.setValue(2, DETAILS_DISCHARGING, Color::YELLOW);
      }
   }
   else if (currentPhase == CalibrationPhase::Charge)
   {
      if (chargingState == BatteryChargingState::No)
      {
         table.setValue(2, DETAILS_PLUG_IN_POWER, Color::LIME);
      }
      else if (fullSoakSecs == 0)
      {
         table.setValue(2, DETAILS_CHARGING, Color::YELLOW);
      }
      else
      {
         uint16_t remSecs = FULL_SOAK_SPAN_S - fullSoakSecs;
         table.setValue(2, String(DETAILS_FULL_CONFIRMING_PREFIX) + String(remSecs) + "s", Color::YELLOW);
      }
   }
   else
   {
      table.setValue(2, DETAILS_DONE, Color::LIME);
   }

   // Row 3: Elapsed time in the current calibration phase
   table.setValue(3, formatElapsed(elapsedSecs));

   table.draw();
}
