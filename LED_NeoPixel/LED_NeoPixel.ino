//
// NeoPixel LED blinking indicator demonstration.
//
// Initializes a Feather onboard NeoPixel to blink in red at 500ms intervals.
// Demonstrates basic NeoPixel color, brightness, and animation control.
//
// Hardware: Feather board with integrated NeoPixel LED.
//

#include <Arduino.h>

#include "ArduinoBoard.h"

#ifndef ARDUINO_LED_SUPPORTED
#error "This sketch requires a board with onboard NeoPixel LED support (e.g. Feather ESP32-S3 or Waveshare ESP32-S3-Zero)."
#endif

#include "LED.h"

constexpr float RED_INTENSITY = 1.0f;
constexpr float GREEN_INTENSITY = 0.0f;
constexpr float BLUE_INTENSITY = 0.0f;

constexpr float BRIGHTNESS_LEVEL = 0.1f;
constexpr uint16_t BLINK_INTERVAL_MS = 500;

Arduino arduino;
NeoPixelLED& led = arduino.neoPixel;

void setup()
{
   arduino.begin();

   // Configure NeoPixel: red color, 10% brightness, blink at 500ms
   led.setColor(RED_INTENSITY, GREEN_INTENSITY, BLUE_INTENSITY);
   led.setLevel(BRIGHTNESS_LEVEL);
   led.blink(BLINK_INTERVAL_MS);
}

void loop()
{
}
