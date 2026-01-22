#include "Feather_ESP32_S3.h"
#include "Stopwatch.h"
#include "Button.h"
#include "RunningAverager.h"

// 
// This sketch displays the current temperature on an Arduino ESP32 Feather
//
Feather_ESP32_S3 feather;
Button button(0);

// The setup() function runs once each time the micro-controller starts
void setup()
{
   Serial.begin();
   feather.begin();
   button.begin();
}

Stopwatch sw(false);
uint16_t lastCount = 0;

void loop()
{
   if (button.wasPressed() && sw.isRunning() == false)
   {
      sw.reset();
      sw.start();
   }

   feather.display.setTextSize(3);
   feather.setCursor(0, 0);

   feather.print("   Is: ");
   feather.println(button.isCurrentlyPressed() ? "True" : "False", 5, Color565::YELLOW);
   feather.print("  Was: ");
   feather.println(button.wasPressed() ? "True" : "False", 5, Color565::YELLOW);
   feather.print("Count: ");
   feather.println(button.getPressedCount(), 2, Color565::YELLOW);

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
      feather.setCursor(0, feather.display.height() - 16);
      feather.print("Restarting in ", Color565::GRAY);
      feather.print((5-sw.elapsedSecs()), "s", 4, 1, Color565::GRAY);
   }
   else
   {
      // cover up the time remaining msg
      feather.display.fillRect(0, feather.display.height() - 16, feather.display.width(), 16, (uint16_t) Color565::BLACK);
   }
}


