//
// Battery Monitor Display
//
// Monitors battery voltage, charge percentage, charging state, and charge rate (when
// supported by a hardware fuel gauge) via the board's Battery instance, displays live
// readings in an aligned table on the TFT display, and uploads 1-minute time-averaged
// battery metrics to InfluxDB once per minute for testing and calibration.
//
// Behavior:
// - Uses the shared MonitorSketch class (see MonitorSketch.h) to own the boot/init
//   sequence: display init, status LED, WiFi, OTA, and the standard InfluxDB setup/post/
//   flush cycle.
// - Uses the 7-day retention bucket and site "Bragg"; the device's location is prompted
//   for (via touch keyboard or over Serial) the first time it runs, then saved to
//   Preferences (NVS) so it survives reboots and OTA firmware updates. Hold buttonA right
//   after startup to force a re-prompt.
// - Samples battery voltage, percentage, ripple, and charge rate every SAMPLE_INTERVAL_MS
//   and accumulates 1-minute time-averaged values for InfluxDB upload.
// - Continuously updates the TFT display table and logs a 1-second diagnostic line to
//   Serial.
//

#include <Arduino.h>
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

const auto VERSION = MakeVersion("1.0");
constexpr auto SKETCH_NAME = "Battery_Monitor_Display";
constexpr auto PREFERENCES_NAMESPACE = "BatteryMonitor";

constexpr uint16_t SAMPLE_INTERVAL_MS = 1000;
constexpr uint8_t INFLUX_INTERVAL_S = 60;
constexpr uint8_t VOLTS_DECIMAL_PLACES = 3;
constexpr uint8_t PERCENT_DECIMAL_PLACES = 1;
constexpr float DISPLAY_UPDATE_RATE = 5.0f;

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
InfluxField* percentField = nullptr;

Timer sampleTimer(SAMPLE_INTERVAL_MS);
TimerSecs uploadTimer(INFLUX_INTERVAL_S);
RateTimer displayTimer(DISPLAY_UPDATE_RATE);
TimerSecs serialLogTimer(1.0f);
Table table(&sketch.arduino, 0, 0);

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
/// Draws the static heading and positions the battery table below it.
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
   percentField = point->addTimeAverageField(INFLUX_INTERVAL_S, "batteryPercent", PERCENT_DECIMAL_PLACES);

   table.addRow("Volts:", "#.##v");
   table.addRow("Charge:", "##########");
   table.addRow("State:", "###########");
   table.addRow("Rate:", "+###%/hr");

   sketch.completeInitialization();

   drawStaticHeading();
}

void loop()
{
   sketch.loop();

   float rawVolts = sketch.arduino.battery.readVolts();
   float avgVolts = sketch.arduino.battery.readAverageVolts();
   bool connected = sketch.arduino.battery.isConnected();
   float avgPercent = connected ? sketch.arduino.battery.readAveragePercent() : NAN;
   float rippleVolts = sketch.arduino.battery.readRippleVolts();
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
   }

   if (uploadTimer.ready())
   {
      sketch.postPoints();
   }

   const char* stateText = formatChargingState(chargingState);
   Color stateColor = (chargingState == BatteryChargingState::BadCharger) ? Color::RED : Color::VALUE;

   if (serialLogTimer.ready())
   {
      const char* charge = !connected ? "No Battery" : "Connected";
      Serial.printf(
         "[%lus] raw=%.3fV  avg=%.3fV  ripple=%.3fV  charge=%s  state=%s\n",
         millis() / 1000UL,
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

   // Display battery voltage
   table.setValue(0, avgVolts, Color::VALUE);

   // Display battery charge (percentage or No Battery)
   if (!connected)
   {
      table.setValue(1, "No Battery", Color::RED);
   }
   else
   {
      table.setValue(1, String((int)roundf(avgPercent)) + "%", Color::VALUE);
   }

   // Display battery state (Charging, Discharging, Full, Bad Charger, or Unknown)
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

   // Display charge rate when supported by the board's battery monitor
   if (connected && hasChargeRate)
   {
      table.setLabelColor(3, Color::LABEL);
      table.setValue(3, chargeRate, Color::VALUE);
   }
   else
   {
      table.setLabelColor(3, Color::BLACK);
      table.setValue(3, "", Color::BLACK);
   }

   table.draw();
}
