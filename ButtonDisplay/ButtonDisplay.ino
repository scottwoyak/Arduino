
#include "Feather.h"
#include "Stopwatch.h"

Feather feather;

Stopwatch sw(false);
uint16_t lastCount = 0;

//Button button(6);
Button& button = feather.buttonA;

Format pinFormat("##");
Format boolFormat(5);
Format timeFormat("##.#s");

void setup()
{
   Serial.begin(115200);
   feather.begin();
   button.begin();
   button.autoReset = false;
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

   feather.print("Pin: ", Color::LABEL);
   feather.println(button.getPin(), pinFormat, Color::VALUE);
   feather.print(" Is: ", Color::LABEL);
   feather.println(button.isPressed() ? "True" : "False", boolFormat, Color::VALUE);
   feather.print("Was: ", Color::LABEL);
   feather.println(button.wasPressed() ? "True" : "False", boolFormat, Color::VALUE);
   feather.print("  #: ", Color::LABEL);
   feather.println(button.getPressedCount(), Color::VALUE);

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
      feather.print("Resetting in ", Color::SUB_LABEL);
      feather.print((5 - sw.elapsedSecs()), timeFormat, Color::SUB_LABEL);
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


