//
// Battery Monitor Display
//
// Monitors battery voltage, charge percentage, charging state, and charge rate (when
// supported by a hardware fuel gauge) via the board's Battery instance, displays live
// readings and a guided 8-step hardware test scenario wizard on the TFT display, and
// uploads 5-second battery diagnostic metrics (including the active scenario step) to
// InfluxDB for model testing and calibration.
//
// Behavior:
// - Uses the shared MonitorSketch class (see MonitorSketch.h) to own the boot/init
//   sequence: display init, status LED, WiFi, OTA, and the standard InfluxDB setup/post/
//   flush cycle.
// - Uses the 7-day retention bucket and site "Bragg"; the device's location is prompted
//   for (via touch keyboard or over Serial) the first time it runs, then saved to
//   Preferences (NVS) so it survives reboots and OTA firmware updates. Hold buttonA right
//   after startup to force a re-prompt.
// - Tracks an 8-step battery test scenario in NVS (surviving cable-plug resets). Each
//   step captures 12 five-second InfluxDB uploads (60 seconds total); once enough data is
//   recorded for the current step, the display highlights the next action to perform and
//   waits for a buttonA press or screen tap to advance to the next step.
// - Continuously updates the TFT display table and logs a 1-second diagnostic line to
//   Serial.
//

#include <Arduino.h>
#include <array>
#include <cmath>
#include <sstream>

#include "ArduinoBoard.h"

#if !defined(ARDUINO_DISPLAY_SUPPORTED) || !defined(ARDUINO_BATTERY_SUPPORTED)
#error "This sketch requires a board with a display and battery support."
#include "WrongBoard.h"
#endif

#include "LibraryVersion.h"
#include "SerialX.h"
#include "Table.h"
#include "Timer.h"

#include "WiFiSettings.h"

#include "MonitorSketch.h"

const auto VERSION = MakeVersion("1.1");
constexpr auto SKETCH_NAME = "Battery_Monitor_Display";
constexpr auto PREFERENCES_NAMESPACE = "BatteryMonitor";
constexpr auto SCENARIO_PREF_KEY = "scenario";

constexpr uint16_t SAMPLE_INTERVAL_MS = 1000;
constexpr uint8_t INFLUX_INTERVAL_S = 5;
constexpr uint8_t UPLOADS_PER_SCENARIO = 12; // 12 * 5s = 60s of data per scenario
constexpr uint8_t VOLTS_DECIMAL_PLACES = 3;
constexpr uint8_t PERCENT_DECIMAL_PLACES = 1;
constexpr uint8_t RATE_DECIMAL_PLACES = 2;
constexpr float DISPLAY_UPDATE_RATE = 5.0f;

///
/// <summary>
/// Describes one guided hardware test scenario step: the active state label shown while
/// capturing data, the transition action shown once enough samples are uploaded, and
/// whether the step must wait until the battery reaches Full (plus a 60-second full-charge
/// soak) before prompting the user.
/// </summary>
///
struct ScenarioStep
{
   const char* activeLabel;
   const char* nextActionPrompt;
   bool waitForFull;
};

constexpr uint8_t NUM_SCENARIOS = 8;

constexpr std::array<ScenarioStep, NUM_SCENARIOS> SCENARIOS =
{ {
   { "1:Bat+PC USB",    "Unplug Bat+Btn", false },
   { "2:NoBat PC USB",  "Replug Bat+Btn", false },
   { "3:Bat+PC USB",    "Unplug USB+Btn", false },
   { "4:Bat Only",      "Plug Ext5V+Btn", false },
   { "5:Bat+Ext 5V",    "Unplug Bat+Btn", false },
   { "6:NoBat Ext 5V",  "Replug Bat+Btn", false },
   { "7:Chg->Full Ext", "Unplug Ext+Btn", true  },
   { "8:Bat @ Full",    "DONE! Btn=1",    false },
} };

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
InfluxField* minVoltsField = nullptr;
InfluxField* maxVoltsField = nullptr;
InfluxField* spanVoltsField = nullptr;
InfluxField* rippleVoltsField = nullptr;
InfluxField* riseVoltsField = nullptr;
InfluxField* fallVoltsField = nullptr;
InfluxField* percentField = nullptr;
InfluxField* rawPercentField = nullptr;
InfluxField* chargeRateField = nullptr;
InfluxField* connectedField = nullptr;
InfluxField* stateField = nullptr;
InfluxField* scenarioField = nullptr;
InfluxField* fullVoltsField = nullptr;

float intervalMinVolts = NAN;
float intervalMaxVolts = NAN;
float intervalMaxSpanVolts = 0.0f;
float intervalMaxRippleVolts = 0.0f;
float intervalMaxRiseVolts = 0.0f;
float intervalMaxFallVolts = 0.0f;
bool intervalAllConnected = true;

uint8_t currentScenario = 1; // 1..NUM_SCENARIOS
uint8_t scenarioUploads = 0;
uint8_t scenarioFullUploads = 0;
bool scenarioNotifiedReady = false;
bool touchWasDown = false;

Timer sampleTimer(SAMPLE_INTERVAL_MS);
TimerSecs uploadTimer(INFLUX_INTERVAL_S);
RateTimer displayTimer(DISPLAY_UPDATE_RATE);
TimerSecs serialLogTimer(1.0f);
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
/// Loads the persisted test scenario index (1..NUM_SCENARIOS) from Preferences so a board
/// reset when plugging/unplugging USB resumes on the same scenario step.
/// </summary>
///
void loadScenario()
{
   sketch.arduino.preferences.begin(PREFERENCES_NAMESPACE, true);
   uint8_t saved = sketch.arduino.preferences.getUChar(SCENARIO_PREF_KEY, 1);
   sketch.arduino.preferences.end();

   if (saved < 1 || saved > NUM_SCENARIOS)
   {
      saved = 1;
   }
   currentScenario = saved;
   scenarioUploads = 0;
   scenarioFullUploads = 0;
   scenarioNotifiedReady = false;
}

///
/// <summary>
/// Persists the active test scenario index (1..NUM_SCENARIOS) to Preferences and resets
/// the per-step upload counter.
/// </summary>
/// <param name="scenario">Scenario step number (1..NUM_SCENARIOS).</param>
///
void setScenario(uint8_t scenario)
{
   if (scenario < 1 || scenario > NUM_SCENARIOS)
   {
      scenario = 1;
   }
   currentScenario = scenario;
   scenarioUploads = 0;
   scenarioFullUploads = 0;
   scenarioNotifiedReady = false;

   sketch.arduino.preferences.begin(PREFERENCES_NAMESPACE, false);
   sketch.arduino.preferences.putUChar(SCENARIO_PREF_KEY, currentScenario);
   sketch.arduino.preferences.end();

   Serial.printf(
      "\n=== SCENARIO %u/%u: %s (capture %us, then: %s) ===\n",
      currentScenario,
      NUM_SCENARIOS,
      SCENARIOS[currentScenario - 1].activeLabel,
      UPLOADS_PER_SCENARIO * INFLUX_INTERVAL_S,
      SCENARIOS[currentScenario - 1].nextActionPrompt);
}

///
/// <summary>
/// Checks whether the user pressed buttonA or tapped the touch screen to advance to the
/// next test scenario step.
/// </summary>
/// <returns>True if an advance input was detected; otherwise false.</returns>
///
bool checkAdvancePressed()
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
/// Formats the latest battery readings for the "GetReadings" DeviceServer command.
/// </summary>
/// <returns>Formatted readings text.</returns>
///
std::string getReadings()
{
   bool connected = sketch.arduino.battery.isConnected();
   float rawVolts = sketch.arduino.battery.readVolts();
   float avgVolts = sketch.arduino.battery.readAverageVolts();
   float avgPercent = connected ? sketch.arduino.battery.readAveragePercent() : NAN;
   float rippleVolts = sketch.arduino.battery.readRippleVolts();
   float chargeRate = sketch.arduino.battery.readChargeRate();
   BatteryChargingState state = sketch.arduino.battery.readChargingState();

   std::ostringstream out;
   out << std::fixed;
   out.precision(3);
   out << "scenario=" << (unsigned)currentScenario << " (" << SCENARIOS[currentScenario - 1].activeLabel << ")\n";
   out << "uploads=" << (unsigned)scenarioUploads << "/" << (unsigned)UPLOADS_PER_SCENARIO << "\n";
   out << "connected=" << (connected ? "true" : "false") << "\n";
   out << "volts=" << avgVolts << " V (raw " << rawVolts << " V)\n";
   out << "ripple=" << rippleVolts << " V\n";
   out.precision(1);
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
/// Positions the battery table below the static heading.
/// </summary>
///
void drawStaticHeading()
{
   sketch.arduino.setCursor(0, 0);
   sketch.arduino.setTextSize(DEFAULT_HEADING_SIZE);
   sketch.arduino.println("Battery Info", Color::HEADING);
   sketch.arduino.moveCursorY(sketch.arduino.charH() / 4);

   table.setPosition(0, sketch.arduino.getCursorY());
   table.invalidate();
}

void setup()
{
   sketch.onReadings(getReadings, "Reports the latest battery voltage, percentage, ripple, and charging state");

   sketch.begin();

   InfluxPoint* point = sketch.addPoint({ { "item", "Battery" } });
   voltsField = point->addTimeAverageField(INFLUX_INTERVAL_S, "batteryVolts", VOLTS_DECIMAL_PLACES);
   minVoltsField = point->addValueField("batteryMinVolts", VOLTS_DECIMAL_PLACES);
   maxVoltsField = point->addValueField("batteryMaxVolts", VOLTS_DECIMAL_PLACES);
   spanVoltsField = point->addValueField("batterySpanVolts", VOLTS_DECIMAL_PLACES);
   rippleVoltsField = point->addValueField("batteryRippleVolts", VOLTS_DECIMAL_PLACES);
   riseVoltsField = point->addValueField("batteryRiseVolts", VOLTS_DECIMAL_PLACES);
   fallVoltsField = point->addValueField("batteryFallVolts", VOLTS_DECIMAL_PLACES);
   percentField = point->addTimeAverageField(INFLUX_INTERVAL_S, "batteryPercent", PERCENT_DECIMAL_PLACES);
   rawPercentField = point->addTimeAverageField(INFLUX_INTERVAL_S, "batteryRawPercent", PERCENT_DECIMAL_PLACES);
   chargeRateField = point->addTimeAverageField(INFLUX_INTERVAL_S, "batteryChargeRate", RATE_DECIMAL_PLACES);
   connectedField = point->addValueField("batteryConnected", 0);
   stateField = point->addValueField("batteryState", 0);
   scenarioField = point->addValueField("batteryScenario", 0);
   fullVoltsField = point->addValueField("batteryFullVolts", VOLTS_DECIMAL_PLACES);

   table.addRow("Volts:", "#.###v");
   table.addRow("Charge:", "##########");
   table.addRow("State:", "###########");
   table.addRow("Test:", "##############");

   sketch.completeInitialization();

#ifdef ARDUINO_BUTTON_A_SUPPORTED
   sketch.arduino.buttonA.reset();
#endif
   loadScenario();

   drawStaticHeading();
}

void loop()
{
   sketch.loop();

   if (checkAdvancePressed())
   {
      uint8_t nextScenario = (currentScenario >= NUM_SCENARIOS) ? 1 : (currentScenario + 1);
      setScenario(nextScenario);
      uploadTimer.reset();
      resetIntervalExtremes();
   }

   float rawVolts = sketch.arduino.battery.readVolts();
   float avgVolts = sketch.arduino.battery.readAverageVolts();
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

   if (sampleTimer.ready())
   {
      if (isfinite(avgVolts))
      {
         voltsField->set(avgVolts);
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
   }

   if (uploadTimer.ready())
   {
      minVoltsField->set(intervalMinVolts);
      maxVoltsField->set(intervalMaxVolts);
      spanVoltsField->set(intervalMaxSpanVolts);
      rippleVoltsField->set(intervalMaxRippleVolts);
      riseVoltsField->set(intervalMaxRiseVolts);
      fallVoltsField->set(intervalMaxFallVolts);
      connectedField->set(intervalAllConnected ? 1.0f : 0.0f);
      stateField->set(static_cast<float>(chargingState));
      scenarioField->set(static_cast<float>(currentScenario));
      fullVoltsField->set(sketch.arduino.battery.readFullChargeVolts());

      sketch.postPoints();
      resetIntervalExtremes();

      if (scenarioUploads < 255)
      {
         scenarioUploads++;
      }

      const ScenarioStep& currentStep = SCENARIOS[currentScenario - 1];
      bool reachedFullPlateau = connected && (chargingState == BatteryChargingState::Full);
      if (currentStep.waitForFull)
      {
         if (reachedFullPlateau && scenarioFullUploads < 255)
         {
            scenarioFullUploads++;
         }
      }

      bool stepComplete = currentStep.waitForFull
         ? (scenarioUploads >= UPLOADS_PER_SCENARIO && scenarioFullUploads >= UPLOADS_PER_SCENARIO)
         : (scenarioUploads >= UPLOADS_PER_SCENARIO);

      if (stepComplete && !scenarioNotifiedReady)
      {
         scenarioNotifiedReady = true;
         Serial.printf(
            "\n>>> SCENARIO %u CAPTURE COMPLETE (%u uploads). NEXT ACTION: %s <<<\n",
            currentScenario,
            scenarioUploads,
            currentStep.nextActionPrompt);
#ifdef ARDUINO_SOUND_SUPPORTED
         sketch.arduino.sound.playTone(880.0f, 150);
#endif
      }
   }

   const char* stateText = formatChargingState(chargingState);
   Color stateColor = (chargingState == BatteryChargingState::BadCharger) ? Color::RED : Color::VALUE;

   if (serialLogTimer.ready())
   {
      const char* charge = !connected ? "No Battery" : "Connected";
      Serial.printf(
         "[%lus] step=%u(%u/%u)  raw=%.3fV  avg=%.3fV  ripple=%.3fV  charge=%s  state=%s\n",
         millis() / 1000UL,
         currentScenario,
         scenarioUploads,
         UPLOADS_PER_SCENARIO,
         rawVolts,
         avgVolts,
         rippleVolts,
         charge,
         stateText);
   }

   if (!displayTimer.ready())
   {
      return;
   }

   // Row 0: Battery voltage
   table.setValue(0, avgVolts, Color::VALUE);

   // Row 1: Battery charge percentage (and charge rate when available) or No Battery
   if (!connected)
   {
      table.setValue(1, "No Battery", Color::RED);
   }
   else if (hasChargeRate)
   {
      char chargeBuf[16];
      int pct = (int)roundf(avgPercent);
      int rate = (int)roundf(chargeRate);
      String chargeStr = String(pct) + "% " + (rate >= 0 ? "+" : "") + String(rate) + "%/h";
      table.setValue(1, chargeStr, Color::VALUE);
   }
   else
   {
      table.setValue(1, String((int)roundf(avgPercent)) + "%", Color::VALUE);
   }

   // Row 2: Battery state (Charging, Discharging, Full, Bad Charger, or Unknown)
   if (connected || chargingState == BatteryChargingState::BadCharger)
   {
      table.setLabelColor(2, Color::LABEL);
      table.setValue(2, stateText, stateColor);
   }
   else
   {
      table.setLabelColor(2, Color::BLACK);
      table.setValue(2, "", Color::BLACK);
   }

   // Row 3: Single test status row — shows a countdown while capturing, then switches
   // to the action instruction when it's time to change the setup.
   const ScenarioStep& step = SCENARIOS[currentScenario - 1];
   if (step.waitForFull)
   {
      if (scenarioFullUploads < UPLOADS_PER_SCENARIO)
      {
         if (scenarioFullUploads == 0)
         {
            String chgText = String("#") + String(currentScenario) + " Chg->Full";
            table.setValue(3, chgText, Color::YELLOW);
         }
         else
         {
            uint16_t remainingSecs = (UPLOADS_PER_SCENARIO - scenarioFullUploads) * INFLUX_INTERVAL_S;
            String fullWaitText = String("#") + String(currentScenario) + " Full " + String(remainingSecs) + "s";
            table.setValue(3, fullWaitText, Color::YELLOW);
         }
      }
      else
      {
         table.setValue(3, step.nextActionPrompt, Color::LIME);
      }
   }
   else if (scenarioUploads < UPLOADS_PER_SCENARIO)
   {
      uint16_t remainingSecs = (UPLOADS_PER_SCENARIO - scenarioUploads) * INFLUX_INTERVAL_S;
      String waitText = String("#") + String(currentScenario) + " Wait " + String(remainingSecs) + "s";
      table.setValue(3, waitText, Color::YELLOW);
   }
   else
   {
      table.setValue(3, step.nextActionPrompt, Color::LIME);
   }

   table.draw();
}
