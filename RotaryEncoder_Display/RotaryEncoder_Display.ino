#include "Feather.h"
#include "SerialX.h"
#include "Stopwatch.h"
#include "RotaryEncoder.h"

Feather feather;
RotaryEncoder encoder(9, 6, 5);

Format posFormat("+######");
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
      encoder.setPosition(0);
   }

   feather.setCursor(0, 0);
   feather.setTextSize(3);

   feather.println("     A: ", encoder.isLowA() ? "Low" : "High", highLowFormat);
   feather.moveCursorY(2);

   feather.println("     B: ", encoder.isLowB() ? "Low" : "High", highLowFormat);
   feather.moveCursorY(2);

   feather.println("   Pos:", encoder.getPosition(), posFormat);
   feather.moveCursorY(2);

   feather.println("Button: ", encoder.button.isPressed()?"True" : "False", boolFormat);

   feather.setTextSize(2);
   feather.setCursorY(-feather.charH());
   feather.print("A:", encoder.getPinA());
   feather.print("  B:", encoder.getPinB());
   feather.println("  Button:", encoder.getButtonPin());
}


