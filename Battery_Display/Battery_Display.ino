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
   bool charging = arduino.battery.isCharging();
   if (connected)
   {
      float percent = arduino.battery.readPercent();
      if (isfinite(percent))
      {
         percentAverage.set(percent);
      }
   }

   if (serialLogTimer.ready())
   {
      const char* state = !connected ? "Disconnected" : (charging ? "Charging" : "Battery");
      Serial.printf(
         "[%lus] raw=%.3fV  avg=%.3fV  state=%s\n",
         millis() / 1000UL,
         rawVolts,
         voltsAverage.average(),
         state);
   }

   if (!displayTimer.ready())
   {
      return;
   }

   arduino.setCursor(0, 0);
   arduino.setTextSize(3);
   arduino.println("Battery Info", Color::HEADING);
   arduino.moveCursorY(arduino.charH() / 4);

   // Display battery voltage
   arduino.println("Volts: ", voltsAverage.average(), batteryVoltsFormat);
   arduino.moveCursorY(arduino.charH() / 4);

   if (!connected)
   {
      arduino.print("State: ", Color::LABEL);
      arduino.println("Disconnected", Color::RED);
      return;
   }

   // Display charge state / percentage
   float chargeRate = arduino.battery.readChargeRate();
   if (!isfinite(chargeRate) && charging)
   {
      arduino.print("State: ", Color::LABEL);
      arduino.println("Charging    ", Color::VALUE2);
      return;
   }

   arduino.print("State: ", percentAverage.average(), percentFormat);
   uint16_t x = arduino.display.getCursorX();
   arduino.println("        ");
   arduino.moveCursorY(arduino.charH() / 4);

   // Display charge rate and status when supported by the board's battery monitor
   if (isfinite(chargeRate))
   {
      arduino.setTextSize(2);
      arduino.setCursorX(x);
      arduino.println(" Rate: ", chargeRate, chargeRateFormat);
      arduino.setCursorX(x);

      if (fabs(chargeRate) >= MIN_CHARGE_RATE_FOR_STATUS)
      {
         arduino.println(chargeRate > 0 ? "Charging" : "Draining", Color::VALUE2);
      }
      else
      {
         arduino.println("        ");  // Clear line
      }
   }
}
