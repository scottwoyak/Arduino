#include <FastLED.h>
#include "Stopwatch.h"
#include "SerialX.h"

Stopwatch sw;

enum class Color
{
   Red,
   Yellow,
   Green,
   Cyan,
   Blue,
   Magenta,
   White,
   Count,
} mode;

Color operator++(Color& color, int)
{
   color = static_cast<Color>((static_cast<int>(color) + 1) % static_cast<int>(Color::Count));
   return color;
}

Color color = Color::Red;

constexpr uint8_t NUM_LEDS = 1;
CRGB leds[NUM_LEDS];

void setup()
{
   SerialX::begin();

   // Initialize FastLED and the onboard NeoPixel
#if defined ARDUINO_WAVESHARE_ESP32_S3_ZERO
   FastLED.addLeds<WS2812B, 21, RGB>(leds, NUM_LEDS);
#else
   FastLED.addLeds<NEOPIXEL, PIN_NEOPIXEL>(leds, NUM_LEDS);
#endif
}

void loop()
{
   if (sw.elapsedMillis() > 1000)
   {
	  sw.reset();
	  color++;
   }

   switch (color)
   {
   case Color::Red:
	  leds[0] = CRGB::Red;
	  break;
   case Color::Yellow:
	  leds[0] = CRGB::Yellow;
	  break;
   case Color::Green:
	  leds[0] = CRGB::Green;
	  break;
   case Color::Cyan:
	  leds[0] = CRGB::Cyan;
	  break;
   case Color::Blue:
	  leds[0] = CRGB::Blue;
	  break;
   case Color::Magenta:
	  leds[0] = CRGB::Magenta;
	  break;
   case Color::White:
	  leds[0] = CRGB::White;
	  break;
   }

   FastLED.show();

   delay(1000);
}
