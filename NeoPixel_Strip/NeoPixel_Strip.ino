//
// WS2812B NeoPixel strip demonstration.
//
// Consecutively illuminates one LED at a time along a WS2812B strip, advancing
// to the next LED every 0.5 seconds. After each full pass across the strip,
// advances to the next color, alternating between primary and secondary colors
// (Red, Yellow, Green, Cyan, Blue, Magenta).
//
// Hardware: ESP32 with an external WS2812B NeoPixel strip on GPIO 7.
//

#include <Arduino.h>
#include <array>

#include <Adafruit_NeoPixel.h>

#include "SerialX.h"
#include "Timer.h"

constexpr int16_t STRIP_PIN = 7;
constexpr uint16_t NUM_STRIP_LEDS = 3;
constexpr uint8_t BRIGHTNESS = 25;
constexpr uint32_t STEP_INTERVAL_MS = 500;

struct StripColor
{
   const char* name;
   uint8_t r;
   uint8_t g;
   uint8_t b;
};

constexpr std::array<StripColor, 6> STRIP_COLORS = {{
   { "Red",     255,   0,   0 },
   { "Yellow",  255, 255,   0 },
   { "Green",     0, 255,   0 },
   { "Cyan",      0, 255, 255 },
   { "Blue",      0,   0, 255 },
   { "Magenta", 255,   0, 255 },
}};

Adafruit_NeoPixel strip(NUM_STRIP_LEDS, STRIP_PIN, NEO_GRB + NEO_KHZ800);
Timer stepTimer(STEP_INTERVAL_MS);
uint16_t currentLed = 0;
size_t currentColor = 0;

///
/// <summary>
/// Lights only the current LED with the current color and turns off all others.
/// </summary>
///
void showCurrentStep()
{
   const StripColor& color = STRIP_COLORS[currentColor];

   Serial.print("LED ");
   Serial.print(currentLed);
   Serial.print(": ");
   Serial.println(color.name);

   strip.clear();
   strip.setPixelColor(currentLed, strip.Color(color.r, color.g, color.b));
   strip.show();
}

void setup()
{
   SerialX::begin();

   strip.begin();
   strip.setBrightness(BRIGHTNESS);
   strip.clear();
   strip.show();

   Serial.println("NeoPixel Strip - Sequential LED & Color Cycle");
   showCurrentStep();
}

void loop()
{
   if (stepTimer.ready())
   {
      currentLed++;
      if (currentLed >= NUM_STRIP_LEDS)
      {
         currentLed = 0;
         currentColor = (currentColor + 1) % STRIP_COLORS.size();
      }

      showCurrentStep();
   }
}
