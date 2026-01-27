#include "Feather.h"
#include "Stopwatch.h"
#include "RotaryEncoder.h"

Feather feather;
RotaryEncoder encoder(9, 6, 5);

// The setup() function runs once each time the micro-controller starts
void setup()
{
   // start serial port
   Serial.begin(115200);

   // wait a few seconds for the serial monitor to open
   while (millis() < 2000 && !Serial)
   {
   };
   delay(500);

   feather.begin();
   encoder.begin();
}

void loop()
{
   if (encoder.wasPressed())
   {
      encoder.reset();
      encoder.setPosition(0);
   }

   feather.setCursor(0, 0);

   if (feather.display.height() / CharSize::MEDIUM_H >= 5)
   {
      feather.setTextSize(TextSize::MEDIUM);
   }
   else
   {
      feather.setTextSize(TextSize::SMALL);
   }

   feather.print("     A: ", Color::WHITE);
   feather.println(encoder.isLowA() ? "Low" : "High", 4, Color::YELLOW);
   feather.moveCursorY(2);

   feather.print("     B: ", Color::WHITE);
   feather.println(encoder.isLowB() ? "Low" : "High", 4, Color::YELLOW);
   feather.moveCursorY(2);

   feather.print("   Pos: ", Color::WHITE);
   feather.println(encoder.getPosition(), 3, Color::YELLOW);
   feather.moveCursorY(2);

   feather.print("Button: ", Color::WHITE);
   feather.println(encoder.isPressed()?"True" : "False", 5, Color::YELLOW);

   feather.setCursorY(-CharSize::SMALL_H);
   feather.setTextSize(TextSize::SMALL);
   feather.print("A:", Color::WHITE);
   feather.print(encoder.getPinA(), Color::YELLOW);
   feather.print("  B:", Color::WHITE);
   feather.print(encoder.getPinB(), Color::YELLOW);
   feather.print("  Button:", Color::WHITE);
   feather.println(encoder.getButtonPin(), Color::YELLOW);

   feather.displayDisplay();
}


