/// <summary>
/// Battery status display for Feather displays.
/// </summary>
/// <remarks>
/// Monitors battery voltage, charge percentage, and charge rate from a MAX17048 fuel gauge IC
/// and displays the information on a TFT display. Indicates charging/draining state when
/// the charge rate exceeds 1%/hour.
/// 
/// Hardware: Feather ESP32 with TFT display and MAX17048 battery fuel gauge (I2C).
/// </remarks>

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MAX1704X.h>

#include "Feather.h"
#include "SerialX.h"

constexpr float MIN_CHARGE_RATE_FOR_STATUS = 1.0f;  // %/hour threshold for showing charge state

Feather feather;
Adafruit_MAX17048 battery;

Format batteryVoltsFormat("#.#v");
Format percentFormat("###%");
Format chargeRateFormat("+###%/hr");

void setup()
{
   SerialX::begin();
   feather.begin();
   Wire.begin();

   // Wait for battery fuel gauge to initialize
   while (!battery.begin())
   {
      Serial.println("Looking for battery fuel gauge...");
      delay(1000);
   }

   Serial.println("Battery Display - Ready");
}

void loop()
{
   feather.setCursor(0, 0);
   feather.setTextSize(3);
   feather.println("Battery Info", Color::HEADING);
   feather.moveCursorY(feather.charH() / 4);

   // Display battery voltage
   feather.println("Volts: ", battery.cellVoltage(), batteryVoltsFormat);
   feather.moveCursorY(feather.charH() / 4);

   // Display charge percentage
   feather.print("State: ", battery.cellPercent(), percentFormat);
   uint16_t x = feather.display.getCursorX();
   feather.println();
   feather.moveCursorY(feather.charH() / 4);

   // Display charge rate and status
   feather.setTextSize(2);
   feather.setCursorX(x);
   float chargeRate = battery.chargeRate();
   feather.println(" Rate: ", chargeRate, chargeRateFormat);
   feather.setCursorX(x);

   if (fabs(chargeRate) >= MIN_CHARGE_RATE_FOR_STATUS)
   {
      feather.println(chargeRate > 0 ? "Charging" : "Draining", Color::VALUE2);
   }
   else
   {
      feather.println("        ");  // Clear line
   }
}
