/// <summary>
/// Counter input display with timing metrics.
/// </summary>
/// <remarks>
/// Displays pulse count and time span between pulses from a digital input pin.
/// Tracks minimum pulse span since last reset. Button A resets metrics.
/// 
/// Hardware: Feather ESP32 with TFT display and counter input on GPIO pin.
/// </remarks>

#include <Arduino.h>

#include "Counter.h"
#include "ArduinoBoard.h"

#ifndef ARDUINO_BUTTON_SUPPORTED
#error "This sketch requires a board with button support (e.g. Feather ESP32-S3 or Feather M0)."
#endif
#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include "SerialX.h"

Arduino arduino;
Counter counter(0);
double minSpan = 0;

Format countFormat("######");
Format spanFormat("#######.## ms");

void setup()
{
   SerialX::begin();
   arduino.begin();
   counter.begin();
}

void loop()
{
   arduino.setCursor(0, 0);
   arduino.setTextSize(3);

   double span = counter.span() / 1000.0;
   minSpan = min(span, minSpan == 0 ? span : minSpan);

   arduino.println("Counter", Color::HEADING);
   arduino.moveCursorY(arduino.charH() / 2);

   arduino.setTextSize(2);
   arduino.println("  Pin: ", counter.getPin());
   arduino.moveCursorY(2);

   arduino.println("Count: ", counter.count(), countFormat);
   arduino.moveCursorY(2);

   arduino.println(" Span: ", span, spanFormat);
   arduino.moveCursorY(2);

   arduino.print("  Min: ", Color::LABEL);
   if (minSpan == 0)
   {
      arduino.print("----", Color::GRAY);
   }
   else
   {
      arduino.print(minSpan, spanFormat, Color::VALUE);
   }

   if (arduino.buttonA.wasPressed())
   {
      minSpan = 0;
      counter.reset();
   }
}
