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

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include "SerialX.h"

Arduino arduino;

constexpr uint8_t pins[] = {
   A0, A1, A2, A3, A4, A5,
   5, 6, 9, 10, 11, 12, 13, 0,
};

constexpr auto NUM_PINS = sizeof(pins) / sizeof(pins[0]);

void setup()
{
   SerialX::begin();
   arduino.begin();
   arduino.display.setRotation(2);

   for (uint8_t i = 0; i < NUM_PINS; i++)
   {
      pinMode(pins[i], INPUT_PULLUP);
   }
}

void loop()
{
   arduino.setCursor(0, 0);
   arduino.setTextSize(2);
   uint8_t lineH = arduino.charH() + 1;

   // Display analog pins (A0-A5) on the left with analog values
   for (uint8_t i = 0; i < 6; i++)
   {
      bool high = digitalRead(pins[i]) == HIGH;
      arduino.print(high ? "HIGH " : "LOW  ", high ? Color::VALUE : Color::VALUE2);
      arduino.print(String("A") + i, high ? Color::LABEL : Color::VALUE2);
      arduino.print(analogRead(pins[i]), Color::VALUE);
      pinMode(pins[i], INPUT_PULLUP);  // Reset after analogRead()
      arduino.moveCursorY(lineH);
      arduino.setCursorX(0);
   }

   // Display digital pins (5-13, 0) on the right in compact format
   uint16_t x = arduino.display.width() - 7 * arduino.charW();
   arduino.setCursorY(arduino.display.height() - arduino.charH());
   for (uint8_t i = 6; i < NUM_PINS; i++)
   {
      arduino.setCursorX(x);
      if (pins[i] < 10)
      {
         arduino.print(" ");
      }
      bool high = digitalRead(pins[i]) == HIGH;
      arduino.print(pins[i], high ? Color::LABEL : Color::VALUE2);
      arduino.print(high ? " HIGH" : "  LOW", high ? Color::VALUE : Color::VALUE2);
      arduino.moveCursorY(-lineH);
   }
}
