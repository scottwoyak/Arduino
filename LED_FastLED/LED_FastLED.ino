/// <summary>
/// FastLED NeoPixel/WS2812B color cycle demonstration.
/// </summary>
/// <remarks>
/// Cycles through primary and secondary colors on a NeoPixel LED at 1-second intervals
/// using the FastLED library. Supports multiple hardware configurations including
/// Waveshare ESP32-S3 Zero and standard Arduino boards with onboard NeoPixel.
/// 
/// Hardware: ESP32 board with integrated or connected WS2812B/NeoPixel RGB LED.
/// </remarks>

#include <Arduino.h>
#include <FastLED.h>

#include "SerialX.h"
#include "Stopwatch.h"

constexpr unsigned long COLOR_CYCLE_INTERVAL_MS = 1000;  // Change color every second
constexpr uint8_t NUM_LEDS = 1;

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
CRGB leds[NUM_LEDS];

void setup()
{
   SerialX::begin();

   // Initialize FastLED for the onboard NeoPixel
#if defined ARDUINO_WAVESHARE_ESP32_S3_ZERO
   FastLED.addLeds<WS2812B, 21, RGB>(leds, NUM_LEDS);
#else
   FastLED.addLeds<NEOPIXEL, PIN_NEOPIXEL>(leds, NUM_LEDS);
#endif

   Serial.println("FastLED - NeoPixel Color Cycle Demonstration");
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
         leds[0] = CRGB::Red;
         break;

      case ColorMode::Yellow:
         leds[0] = CRGB::Yellow;
         break;

      case ColorMode::Green:
         leds[0] = CRGB::Green;
         break;

      case ColorMode::Cyan:
         leds[0] = CRGB::Cyan;
         break;

      case ColorMode::Blue:
         leds[0] = CRGB::Blue;
         break;

      case ColorMode::Magenta:
         leds[0] = CRGB::Magenta;
         break;

      case ColorMode::White:
         leds[0] = CRGB::White;
         break;
   }

   FastLED.show();
   delay(1000);
}
