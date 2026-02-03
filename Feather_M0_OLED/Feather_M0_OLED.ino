#include "Feather_M0_OLED.h"

Feather_M0_OLED feather;

void setup()
{
   feather.begin();
}

long counter = 0;
long lastMicros = micros();

void loop()
{
   long newMicros = micros();
   double fps = 1000000.0 / (newMicros - lastMicros);

   feather.clearDisplay();
   feather.setTextSize(2);
   feather.setCursor(0, 0);
   feather.println(counter++);
   feather.moveCursorY(feather.charH() / 4);

   feather.setTextSize(1);
   feather.print("ButtonA: ");
   feather.println(feather.buttonA.isPressed() ? "TRUE " : "FALSE");
   feather.moveCursorY(1);
   feather.print("ButtonB: ");
   feather.println(feather.buttonB.isPressed() ? "TRUE " : "FALSE");
   feather.moveCursorY(1);
   feather.print("ButtonC: ");
   feather.println(feather.buttonC.isPressed() ? "TRUE " : "FALSE");
   feather.moveCursorY(1);


   feather.setTextSize(1);
   feather.setCursor(0, -feather.charH());
   feather.print(fps);
   feather.print(" fps");
   feather.displayDisplay();

   lastMicros = newMicros;
}
