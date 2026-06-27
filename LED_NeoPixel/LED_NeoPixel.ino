/// <summary>
/// NeoPixel LED blinking indicator demonstration.
/// </summary>
/// <remarks>
/// Initializes a Feather onboard NeoPixel to blink in red at 500ms intervals.
/// Demonstrates basic NeoPixel color, brightness, and animation control.
/// 
/// Hardware: Feather board with integrated NeoPixel LED.
/// </remarks>

#include <Arduino.h>

#include "Feather.h"
#include "LED.h"
#include "Stopwatch.h"

constexpr float RED_INTENSITY = 1.0f;
constexpr float GREEN_INTENSITY = 0.0f;
constexpr float BLUE_INTENSITY = 0.0f;

constexpr float BRIGHTNESS_LEVEL = 0.1f;
constexpr unsigned long BLINK_INTERVAL_MS = 500;

Feather feather;

void setup()
{
   Serial.begin(115200);
   feather.begin();

   // Configure NeoPixel: red color, 10% brightness, blink at 500ms
   feather.neoPixel.setColor(RED_INTENSITY, GREEN_INTENSITY, BLUE_INTENSITY);
   feather.neoPixel.setLevel(BRIGHTNESS_LEVEL);
   feather.neoPixel.blink(BLINK_INTERVAL_MS);
}

void loop()
{
}
