#include "Led.h"
#include <Stopwatch.h>

constexpr uint8_t RED_PIN = 9;
constexpr uint8_t GREEN_PIN = 6;
constexpr uint8_t BLUE_PIN = 5;

Stopwatch sw;

enum Color { Red, Green, Blue };

Color color = Red;

RGBLED led(RED_PIN, GREEN_PIN, BLUE_PIN);

void setup()
{
   Serial.begin(115200);
   while (!Serial) { delay(100); }

   led.begin();
}

void loop()
{
   if (sw.elapsedMillis() > 1000)
   {
      sw.reset();

      if (color == Red)
      {
         color = Green;
      }
      else if (color == Green)
      {
         color = Blue;
      }
      else
      {
         color = Red;
      }
   }

   switch (color)
   {
   case Red:
      led.setColor(1.0f, 0.0f, 0.0f);
      break;
   case Green:
      led.setColor(0.0f, 1.0f, 0.0f);
      break;
   case Blue:
      led.setColor(0.0f, 0.0f, 1.0f);
      break;
   }

   delay(1000);
}
