
#include "Feather.h"
#include "SerialX.h"

Feather feather;

constexpr uint8_t pins[] = {
   A0,
   A1,
   A2,
   A3,
   A4,
   A5,
   5,
   6,
   9,
   10,
   11,
   12,
   13,
   0,
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

   for (uint8_t i = 0; i < 6; i++)
   {
      /*
      feather.print(String("A") + i, Color::LABEL);
      feather.print(digitalRead(pins[i]) == HIGH ? " HIGH" : " LOW ", Color::VALUE);
      */
      bool high = digitalRead(pins[i]) == HIGH;
      feather.print(high ? "HIGH " : "LOW  ", high ? Color::VALUE : Color::VALUE2);
      feather.print(String("A") + i, high ? Color::LABEL : Color::VALUE2);
      feather.print(analogRead(pins[i]), Color::VALUE);
      pinMode(pins[i], INPUT_PULLUP); // reset after calling analogRead()
      feather.moveCursorY(lineH);
      feather.setCursorX(0);
   }

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
