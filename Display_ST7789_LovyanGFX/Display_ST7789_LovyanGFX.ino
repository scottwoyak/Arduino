#define LGFX_AUTODETECT
#include <LovyanGFX.h>

#include "Scott16.h"
#include "Scott32.h"
#include "RollingStats.h"

LGFX display;
RollingStats fps(100);

void setup()
{
   display.init();
}

long lastMicros = micros();

// Add the main program code into the continuous loop() function
void loop()
{
   unsigned long newMicros = micros();
   fps.set(1000000.0 / (newMicros - lastMicros));

   display.setCursor(0, 0);

//#define OLD_FONTS
#ifdef OLD_FONTS

   // this code is for drawing the traditional Adafruit style block fonts
   display.setTextSize(4);
   display.println(random(9999));
   display.println(random(9999));
   display.println(random(9999));

   display.setTextSize(2);
   display.setCursor(0, display.height() - 16);

#else

   // this code is for anti-aliased fonts

   display.loadFont(Scott32);
   display.setTextColor(TFT_WHITE, TFT_BLACK);
   display.println(random(9999));
   display.println(random(9999));
   display.println(random(9999));

   display.loadFont(Scott16);
   display.setCursor(0, display.height() - display.fontHeight());

#endif

   display.print("FPS: ");
   display.print(fps.get(),1);
   display.print("  "); // erase any remaining characters from previous loop

   lastMicros = newMicros;
}
