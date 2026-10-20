//
// Battery status display.
//
// Monitors battery voltage, charge percentage, and charge rate (when supported by a
// hardware fuel gauge) via the board's Battery instance and displays the information on
// the TFT display. Indicates charging/draining state when the charge rate exceeds 1%/hour.
//

#include <Arduino.h>
#include <cmath>

#include "ArduinoBoard.h"

#if !defined(ARDUINO_DISPLAY_SUPPORTED) || !defined(ARDUINO_BATTERY_SUPPORTED)
#error "This sketch requires a board with a display and battery support."
#include "WrongBoard.h"
#endif

#include "SerialX.h"
#include "Table.h"
#include "Timer.h"

constexpr float MIN_CHARGE_RATE_FOR_STATUS = 1.0f;  // %/hour threshold for showing charge state
constexpr float DISPLAY_UPDATE_RATE = 5.0f;

Arduino arduino;
RateTimer displayTimer(DISPLAY_UPDATE_RATE);
TimerSecs serialLogTimer(1.0f);
Table table(&arduino, 0, 0);

void setup()
{
   SerialX::begin();
   arduino.begin();

   while (!arduino.battery.begin())
   {
      Serial.println("Looking for battery monitor...");
      delay(1000);
   }

   arduino.clearDisplay();
   arduino.setCursor(0, 0);
   arduino.setTextSize(DEFAULT_HEADING_SIZE);
   arduino.println("Battery Info", Color::HEADING);
   arduino.moveCursorY(arduino.charH() / 4);

   table.setPosition(0, arduino.getCursorY());
   table.addRow("Volts:", "#.##v");
   table.addRow("Charge:", "##########");
   table.addRow("State:", "###########");
   table.addRow("Rate:", "+###%/hr");

   Serial.println("Battery Display - Ready");
}

void loop()
{
   float rawVolts = arduino.battery.readVolts();
   float avgVolts = arduino.battery.readAverageVolts();
   bool connected = arduino.battery.isConnected();
   float avgPercent = connected ? arduino.battery.readAveragePercent() : NAN;
   BatteryChargingState chargingState = arduino.battery.readChargingState();
   float chargeRate = arduino.battery.readChargeRate();
   bool hasChargeRate = isfinite(chargeRate);

   const char* stateText = "Unknown";
   Color stateColor = Color::VALUE;
   if (chargingState == BatteryChargingState::BadCharger)
   {
      stateText = "Bad Charger";
      stateColor = Color::RED;
   }
   else if (chargingState == BatteryChargingState::Full)
   {
      stateText = "Full";
   }
   else if (chargingState == BatteryChargingState::Yes)
   {
      stateText = "Charging";
   }
   else if (chargingState == BatteryChargingState::No)
   {
      stateText = "Discharging";
   }

   if (serialLogTimer.ready())
   {
      const char* charge = !connected ? "No Battery" : "Connected";
      Serial.printf(
         "[%lus] raw=%.3fV  avg=%.3fV  ripple=%.3fV  charge=%s  state=%s\n",
         millis() / 1000UL,
         rawVolts,
         avgVolts,
         arduino.battery.readRippleVolts(),
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
   if (connected)
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
