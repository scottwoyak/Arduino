
#if defined(ADAFRUIT_FEATHER_M0)

#include "Feather_M0_OLED.h"
const Color TextColor = Color::WHITE;
const Color ValueColor = Color::WHITE;
const Color InfoColor = Color::WHITE;
Feather_M0_OLED feather;

#elif defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S3_TFT)

#include "Feather_ESP32_S3.h"
const Color TextColor = Color::WHITE;
const Color ValueColor = Color::YELLOW;
const Color InfoColor = Color::GRAY;
Feather_ESP32_S3 feather;

#endif

#include "Stopwatch.h"
#include "RunningAverager.h"

void setup()
{
   Serial.begin(115200);
   feather.begin();
}

Stopwatch sw(false);
uint16_t lastCount = 0;
Button& button = feather.buttonA;

void loop()
{
   if ((button.wasPressed() && sw.isRunning() == false) || button.isCurrentlyPressed())
   {
      sw.reset();
      sw.start();
   }

   feather.display.setTextSize((uint8_t)TextSize::MEDIUM);
   feather.setCursor(0, 0);

   feather.print(" Is: ");
   feather.println(button.isCurrentlyPressed() ? "True" : "False", 5, ValueColor);
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
      feather.print("Restarting in ", InfoColor);
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


