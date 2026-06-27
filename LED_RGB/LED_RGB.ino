/// <summary>
/// RGB LED color cycle demonstration.
/// </summary>
/// <remarks>
/// Cycles through primary and secondary colors on an RGB LED at 1-second intervals.
/// Demonstrates basic RGB LED control with PWM pins.
/// 
/// Colors tested: Red, Yellow, Green, Cyan, Blue, Magenta, White.
/// Hardware: ESP32 with common-cathode RGB LED on PWM pins.
/// </remarks>

#include <Arduino.h>

#include "LED.h"
#include "SerialX.h"
#include "Stopwatch.h"

constexpr uint8_t RED_PIN = 1;
constexpr uint8_t GREEN_PIN = 2;
constexpr uint8_t BLUE_PIN = 3;

constexpr unsigned long COLOR_CYCLE_INTERVAL_MS = 1000;  // Change color every second

enum class ColorMode
{
   Red,
   Yellow,
   Green,
   Cyan,
   Blue,
   Magenta,
   White,
   Count,
};

/// <summary>
/// Post-increment operator for color cycling.
/// </summary>
ColorMode operator++(ColorMode& color, int)
{
   color = static_cast<ColorMode>((static_cast<int>(color) + 1) % static_cast<int>(ColorMode::Count));
   return color;
}

Stopwatch sw;
ColorMode currentColor = ColorMode::Red;
RGBLED led(RED_PIN, GREEN_PIN, BLUE_PIN);

void setup()
{
   SerialX::begin();

   led.begin();
   led.turnOn();

   Serial.println("RGB LED - Color Cycle Demonstration");
}

void loop()
{
   // Change color every COLOR_CYCLE_INTERVAL_MS milliseconds
   if (sw.elapsedMillis() > COLOR_CYCLE_INTERVAL_MS)
   {
      sw.reset();
      currentColor++;
   }

   // Set LED color based on current mode
   switch (currentColor)
   {
      case ColorMode::Red:
         led.setColor(1.0f, 0.0f, 0.0f);
         break;

      case ColorMode::Yellow:
         led.setColor(1.0f, 1.0f, 0.0f);
         break;

      case ColorMode::Green:
         led.setColor(0.0f, 1.0f, 0.0f);
         break;

      case ColorMode::Cyan:
         led.setColor(0.0f, 1.0f, 1.0f);
         break;

      case ColorMode::Blue:
         led.setColor(0.0f, 0.0f, 1.0f);
         break;

      case ColorMode::Magenta:
         led.setColor(1.0f, 0.0f, 1.0f);
         break;

      case ColorMode::White:
         led.setColor(1.0f, 1.0f, 1.0f);
         break;
   }

   delay(1000);
}
