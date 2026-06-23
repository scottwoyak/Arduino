#include "LED.h"
#include "Stopwatch.h"
#include "Feather.h"

Feather feather;

void setup()
{
   Serial.begin(115200);
   feather.begin();

   feather.neoPixel.setColor(1.0f, 0.0f, 0.0f);
   feather.neoPixel.setLevel(0.1f);
   feather.neoPixel.blink(500);
}

void loop()
{
}
