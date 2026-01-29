#include "Feather.h"
#include "SerialX.h"
#include "Stopwatch.h"
#include "RotaryEncoder.h"

Feather feather;
RotaryEncoder encoder(9, 6, 5);

Format posFormat("+###");
Format boolFormat(5);
Format highLowFormat(4);

// The setup() function runs once each time the micro-controller starts
void setup()
{
   SerialX::begin();
   feather.begin();
   encoder.begin();
}

void loop()
{
   if (encoder.button.wasPressed())
   {
      encoder.button.reset();
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

   feather.print("     A: ", Color::LABEL);
   feather.println(encoder.isLowA() ? "Low" : "High", highLowFormat, Color::VALUE);
   feather.moveCursorY(2);

   feather.print("     B: ", Color::LABEL);
   feather.println(encoder.isLowB() ? "Low" : "High", highLowFormat, Color::VALUE);
   feather.moveCursorY(2);

   feather.print("   Pos:", Color::LABEL);
   feather.println(encoder.getPosition(), posFormat, Color::VALUE);
   feather.moveCursorY(2);

   feather.print("Button: ", Color::LABEL);
   feather.println(encoder.button.isPressed()?"True" : "False", boolFormat, Color::VALUE);

   feather.setCursorY(-CharSize::SMALL_H);
   feather.setTextSize(TextSize::SMALL);
   feather.print("A:", Color::LABEL);
   feather.print(encoder.getPinA(), Color::VALUE);
   feather.print("  B:", Color::LABEL);
   feather.print(encoder.getPinB(), Color::VALUE);
   feather.print("  Button:", Color::LABEL);
   feather.println(encoder.getButtonPin(), Color::VALUE);

   feather.displayDisplay();
}


