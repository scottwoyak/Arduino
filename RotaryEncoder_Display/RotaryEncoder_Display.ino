/// <summary>
/// Rotary encoder position and button state display.
/// </summary>
/// <remarks>
/// Displays real-time rotary encoder position, pin states (A/B high/low), and button status.
/// Button press resets encoder position to zero.
/// 
/// Hardware: Feather ESP32 with rotary encoder on GPIO pins and TFT display.
/// </remarks>

#include <Arduino.h>

#include "Feather.h"
#include "RotaryEncoder.h"
#include "SerialX.h"
#include "Stopwatch.h"

Feather feather;
RotaryEncoder encoder(9, 6, 5);

Format posFormat("+######");
Format boolFormat("5");
Format highLowFormat("4");

void setup()
{
   SerialX::begin();
   feather.begin();
   encoder.begin();
}

void loop()
{
   // Reset position on button press
   if (encoder.button.wasPressed())
   {
      encoder.setPosition(0);
   }

   feather.setCursor(0, 0);
   feather.setTextSize(3);

   // Display pin A state
   feather.println("     A: ", encoder.isLowA() ? "Low" : "High", highLowFormat);
   feather.moveCursorY(2);

   // Display pin B state
   feather.println("     B: ", encoder.isLowB() ? "Low" : "High", highLowFormat);
   feather.moveCursorY(2);

   // Display current position
   feather.println("   Pos:", encoder.getPosition(), posFormat);
   feather.moveCursorY(2);

   // Display button state
   feather.println("Button: ", encoder.button.isPressed() ? "True" : "False", boolFormat);

   // Display pin configuration
   feather.setTextSize(2);
   feather.setCursorY(-feather.charH());
   feather.print("A:", encoder.getPinA());
   feather.print("  B:", encoder.getPinB());
   feather.println("  Button:", encoder.getButtonPin());
}
