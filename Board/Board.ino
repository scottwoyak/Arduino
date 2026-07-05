/// <summary>
/// Cross-board test sketch that blinks the onboard NeoPixel LED and prints a serial heartbeat.
/// </summary>
/// <remarks>
/// Demonstrates the unified Arduino board wrapper (ArduinoBoard.h): this sketch compiles and
/// runs unchanged on both the Adafruit Feather ESP32-S3 TFT and the Waveshare ESP32-S3-Zero by
/// simply selecting a different board in Visual Studio/Visual Micro.
///
/// Hardware: Adafruit Feather ESP32-S3 TFT or Waveshare ESP32-S3-Zero, using the onboard NeoPixel LED.
/// </remarks>

#include <Arduino.h>

#include "ArduinoBoard.h"

#ifndef ARDUINO_LED_SUPPORTED
#error "This sketch requires a board with onboard NeoPixel LED support (e.g. Feather ESP32-S3 or Waveshare ESP32-S3-Zero)."
#endif

#include "SerialX.h"
#include "Timer.h"

constexpr float LED_BRIGHTNESS = 0.1f;
constexpr unsigned long BLINK_INTERVAL_MS = 500;
constexpr unsigned long HEARTBEAT_INTERVAL_MS = 1000;

Arduino arduino;
Timer heartbeatTimer(HEARTBEAT_INTERVAL_MS);

void setup()
{
   SerialX::begin();
   Serial.println("BoardTest");

   arduino.begin();

   arduino.neoPixel.setColor(1.0f, 1.0f, 1.0f);
   arduino.neoPixel.setLevel(LED_BRIGHTNESS);
   arduino.neoPixel.blink(BLINK_INTERVAL_MS);
}

void loop()
{
   if (heartbeatTimer.ready())
   {
      Serial.println("Alive");
   }
}
