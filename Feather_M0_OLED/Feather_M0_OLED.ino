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

   feather.display.clearDisplay();
   feather.display.setTextSize(2);
   feather.display.setCursor(0, 0);
   feather.display.print(counter++);
   feather.display.setTextSize(1);
   feather.display.setCursor(0, feather.display.height() - 8);
   feather.display.print(fps);
   feather.display.print(" fps");
   feather.display.display();

   lastMicros = newMicros;
}
