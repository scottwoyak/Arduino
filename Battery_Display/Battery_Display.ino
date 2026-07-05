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

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include "SerialX.h"

constexpr float MIN_CHARGE_RATE_FOR_STATUS = 1.0f;  // %/hour threshold for showing charge state

Arduino arduino;
Adafruit_MAX17048 battery;

Format batteryVoltsFormat("#.#v");
Format percentFormat("###%");
Format chargeRateFormat("+###%/hr");

void setup()
{
   SerialX::begin();
   arduino.begin();
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
   arduino.setCursor(0, 0);
   arduino.setTextSize(3);
   arduino.println("Battery Info", Color::HEADING);
   arduino.moveCursorY(arduino.charH() / 4);

   // Display battery voltage
   arduino.println("Volts: ", battery.cellVoltage(), batteryVoltsFormat);
   arduino.moveCursorY(arduino.charH() / 4);

   // Display charge percentage
   arduino.print("State: ", battery.cellPercent(), percentFormat);
   uint16_t x = arduino.display.getCursorX();
   arduino.println();
   arduino.moveCursorY(arduino.charH() / 4);

   // Display charge rate and status
   arduino.setTextSize(2);
   arduino.setCursorX(x);
   float chargeRate = battery.chargeRate();
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
