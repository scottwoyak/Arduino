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
#include "TimedAverage.h"
#include "Timer.h"

constexpr float MIN_CHARGE_RATE_FOR_STATUS = 1.0f;  // %/hour threshold for showing charge state
constexpr unsigned long VOLTS_AVERAGE_SPAN_MS = 1000;
constexpr float DISPLAY_UPDATE_RATE = 5.0f;

Arduino arduino;
TimedAverage voltsAverage(VOLTS_AVERAGE_SPAN_MS);
TimedAverage percentAverage(VOLTS_AVERAGE_SPAN_MS);
RateTimer displayTimer(DISPLAY_UPDATE_RATE);
TimerSecs serialLogTimer(1.0f);

Format batteryVoltsFormat("#.##v");
Format percentFormat("###%");
Format chargeRateFormat("+###%/hr");

void setup()
{
   SerialX::begin();
   arduino.begin();

   while (!arduino.battery.begin())
   {
      Serial.println("Looking for battery monitor...");
      delay(1000);
   }

   Serial.println("Battery Display - Ready");
}

void loop()
{
   float rawVolts = arduino.battery.readVolts();
   voltsAverage.set(rawVolts);
   bool connected = arduino.battery.isConnected();
   BatteryChargingState chargingState = arduino.battery.readChargingState();
   float chargeRate = arduino.battery.readChargeRate();
   bool hasChargeRate = isfinite(chargeRate);

   if (connected)
   {
      float percent = arduino.battery.readPercent();
      if (isfinite(percent))
      {
         percentAverage.set(percent);
      }
   }

   const char* stateText = "Unknown    ";
   Color stateColor = Color::VALUE;
   if (chargingState == BatteryChargingState::BadCharger)
   {
      stateText = "Bad Charger";
      stateColor = Color::RED;
   }
   else if (chargingState == BatteryChargingState::Full)
   {
      stateText = "Full       ";
   }
   else if (chargingState == BatteryChargingState::Yes)
   {
      stateText = "Charging   ";
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
         voltsAverage.average(),
         arduino.battery.readRippleVolts(),
         charge,
         stateText);
   }

   if (!displayTimer.ready())
   {
      return;
   }

   arduino.setCursor(0, 0);
   arduino.setTextSize(DEFAULT_HEADING_SIZE);
   arduino.println("Battery Info", Color::HEADING);
   arduino.moveCursorY(arduino.charH() / 4);

   // Display battery voltage
   arduino.setTextSize(DEFAULT_TEXT_SIZE);
   arduino.println("Volts: ", voltsAverage.average(), batteryVoltsFormat);
   arduino.moveCursorY(arduino.charH() / 4);

   // Display battery charge (percentage or No Battery)
   if (!connected)
   {
      arduino.print("Charge: ", Color::LABEL);
      arduino.println("No Battery", Color::RED);
   }
   else
   {
      arduino.print("Charge: ", percentAverage.average(), percentFormat);
      arduino.println("      ");
   }
   arduino.moveCursorY(arduino.charH() / 4);

   // Display battery state (Charging, Discharging, Bad Charger, or Unknown)
   arduino.print("State: ", Color::LABEL);
   arduino.println(stateText, stateColor);
   arduino.moveCursorY(arduino.charH() / 4);

   // Display charge rate when supported by the board's battery monitor
   if (connected && hasChargeRate)
   {
      arduino.setTextSize(DEFAULT_TEXT_SIZE);
      arduino.println("Rate: ", chargeRate, chargeRateFormat);
   }
}
