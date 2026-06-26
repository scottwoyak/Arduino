
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

   feather.setTextSize(3);
   feather.setCursor(0, 0);

   feather.println("Pin: ", button.getPin(), pinFormat);
   feather.println(" Is: ", button.isPressed() ? "True" : "False", boolFormat);
   feather.println("Was: ", button.wasPressed() ? "True" : "False", boolFormat);
   feather.println("  #: ", button.getPressedCount());

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
      feather.display.setTextSize(2);
      feather.setCursor(0, feather.display.height() - feather.charH());
      feather.print("Resetting in ", Color::SUB_LABEL);
      feather.print((5 - sw.elapsedSecs()), timeFormat, Color::SUB_LABEL);
   }
   else
   {
      // cover up the time remaining msg
      feather.display.fillRect(0, feather.display.height() - feather.charH(), feather.display.width(), feather.charH(), (uint16_t)Color::BLACK);
   }

#if defined(ADAFRUIT_FEATHER_M0)
   feather.display.display();
#endif
}


