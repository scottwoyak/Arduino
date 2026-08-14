//
// RGB LED color cycle demonstration.
//
// Automatically cycles through primary and secondary colors on an RGB LED at
// 1-second intervals. Pressing buttonA switches to manual mode, where colors
// only change when buttonA is pressed.
// Demonstrates basic RGB LED control with PWM pins.
//
// Colors tested: Red, Yellow, Green, Cyan, Blue, Magenta, White.
// Hardware: ESP32 with common-cathode RGB LED on PWM pins.
//

#include <Arduino.h>

#include "Button.h"
#include "LED.h"
#include "SerialX.h"
#include "Timer.h"
#include "Waveshare_ESP32_S3_Zero.h"

#ifdef ARDUINO_WAVESHARE_ESP32_S3_ZERO
constexpr uint8_t RED_PIN = WaveShare_ESP32_S3_Zero_Sensors::DEFAULT_RED_LED_PIN;
constexpr uint8_t GREEN_PIN = WaveShare_ESP32_S3_Zero_Sensors::DEFAULT_GREEN_LED_PIN;
constexpr uint8_t BLUE_PIN = WaveShare_ESP32_S3_Zero_Sensors::DEFAULT_BLUE_LED_PIN;
#else
constexpr uint8_t RED_PIN = A2;
constexpr uint8_t GREEN_PIN = A1;
constexpr uint8_t BLUE_PIN = A0;
#endif

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

///
/// <summary>
/// Gets the display name for a color mode.
/// </summary>
/// <param name="color">Color mode to name</param>
/// <returns>Human-readable color name</returns>
///
const char* colorName(ColorMode color)
{
   switch (color)
   {
      case ColorMode::Red:
         return "Red";

      case ColorMode::Yellow:
         return "Yellow";

      case ColorMode::Green:
         return "Green";

      case ColorMode::Cyan:
         return "Cyan";

      case ColorMode::Blue:
         return "Blue";

      case ColorMode::Magenta:
         return "Magenta";

      case ColorMode::White:
         return "White";

      default:
         return "Unknown";
   }
}

constexpr uint8_t BUTTON_A_PIN = 0;
constexpr unsigned long COLOR_CYCLE_INTERVAL_MS = 1000;  // Auto-cycle to the next color every second

Button buttonA(BUTTON_A_PIN);
Timer colorTimer(COLOR_CYCLE_INTERVAL_MS);
ColorMode currentColor = ColorMode::Red;
bool manualMode = false;
RGBLED led(RED_PIN, GREEN_PIN, BLUE_PIN);

///
/// <summary>
/// Applies the LED color for the given color mode.
/// </summary>
/// <param name="color">Color mode to display</param>
///
void applyColor(ColorMode color)
{
   switch (color)
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

      default:
         break;
   }
}

void setup()
{
   SerialX::begin();

   buttonA.begin();

   led.begin();
   led.turnOn();

   Serial.println("RGB LED - Color Cycle Demonstration");
   Serial.println(colorName(currentColor));
   applyColor(currentColor);
}

void loop()
{
   if (buttonA.wasPressed())
   {
      if (!manualMode)
      {
         manualMode = true;
         Serial.println("Switched to manual mode");
      }

      currentColor++;
      Serial.println(colorName(currentColor));
      applyColor(currentColor);
   }
   else if (!manualMode && colorTimer.ready())
   {
      currentColor++;
      Serial.println(colorName(currentColor));
      applyColor(currentColor);
   }
}
