/// <summary>
/// Digital and analog pin state monitor display.
/// </summary>
/// <remarks>
/// Displays real-time digital (HIGH/LOW) and analog read values for 14 input pins.
/// Pins A0-A5 are displayed on the left with digital state and analog values.
/// Pins 5-13 and 0 are displayed on the right as a compact list.
/// Auto-resets pin mode after analog reads to prevent conflicts.
/// 
/// Hardware: Arduino Feather with TFT display.
/// </remarks>

#include <Arduino.h>

#include "Feather.h"
#include "SerialX.h"

Feather feather;

constexpr uint8_t pins[] = {
   A0, A1, A2, A3, A4, A5,
   5, 6, 9, 10, 11, 12, 13, 0,
};

constexpr auto NUM_PINS = sizeof(pins) / sizeof(pins[0]);

void setup()
{
   SerialX::begin();
   feather.begin();
   feather.display.setRotation(2);

   for (uint8_t i = 0; i < NUM_PINS; i++)
   {
      pinMode(pins[i], INPUT_PULLUP);
   }
}

void loop()
{
   feather.setCursor(0, 0);
   feather.setTextSize(2);
   uint8_t lineH = feather.charH() + 1;

   // Display analog pins (A0-A5) on the left with analog values
   for (uint8_t i = 0; i < 6; i++)
   {
      bool high = digitalRead(pins[i]) == HIGH;
      feather.print(high ? "HIGH " : "LOW  ", high ? Color::VALUE : Color::VALUE2);
      feather.print(String("A") + i, high ? Color::LABEL : Color::VALUE2);
      feather.print(analogRead(pins[i]), Color::VALUE);
      pinMode(pins[i], INPUT_PULLUP);  // Reset after analogRead()
      feather.moveCursorY(lineH);
      feather.setCursorX(0);
   }

   // Display digital pins (5-13, 0) on the right in compact format
   uint16_t x = feather.display.width() - 7 * feather.charW();
   feather.setCursorY(feather.display.height() - feather.charH());
   for (uint8_t i = 6; i < NUM_PINS; i++)
   {
      feather.setCursorX(x);
      if (pins[i] < 10)
      {
         feather.print(" ");
      }
      bool high = digitalRead(pins[i]) == HIGH;
      feather.print(pins[i], high ? Color::LABEL : Color::VALUE2);
      feather.print(high ? " HIGH" : "  LOW", high ? Color::VALUE : Color::VALUE2);
      feather.moveCursorY(-lineH);
   }
}
