
#include "Feather.h"
#include "Stopwatch.h"

Feather feather;

Stopwatch sw(false);
uint16_t lastCount = 0;

Button button(6);
//Button& button = feather.buttonA;

void setup()
{
   Serial.begin(115200);
   feather.begin();
   button.begin();
}

void loop()
{
   if ((button.wasPressed() && sw.isRunning() == false) || button.isPressed())
   {
      sw.reset();
      sw.start();
   }

   feather.setTextSize(TextSize::MEDIUM);
   feather.setCursor(0, 0);

   feather.print("Pin: ");
   feather.println(button.getPin(), 2, ValueColor);
   feather.print(" Is: ");
   feather.println(button.isPressed() ? "True" : "False", 5, ValueColor);
   feather.print("Was: ");
   feather.println(button.wasPressed() ? "True" : "False", 5, ValueColor);
   feather.print("  #: ");
   feather.println(button.getPressedCount(), 2, ValueColor);

   if (lastCount != button.getPressedCount())
   {
      sw.reset();
   }
   lastCount = button.getPressedCount();

   if (sw.elapsedSecs() > 5)
   {
      button.reset();
      sw.stop();
      sw.reset();
   }

   if (sw.isRunning())
   {
      feather.display.setTextSize((uint8_t)TextSize::SMALL);
      feather.setCursor(0, feather.display.height() - CharSize::SMALL_H);
      feather.print("Resetting in ", InfoColor);
      feather.print((5 - sw.elapsedSecs()), "s", 4, 1, InfoColor);
   }
   else
   {
      // cover up the time remaining msg
      feather.display.fillRect(0, feather.display.height() - CharSize::SMALL_H, feather.display.width(), CharSize::SMALL_H, (uint16_t)Color::BLACK);
   }

#if defined(ADAFRUIT_FEATHER_M0)
   feather.display.display();
#endif
}


