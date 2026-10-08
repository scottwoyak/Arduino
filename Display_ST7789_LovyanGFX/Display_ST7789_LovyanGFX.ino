#define LGFX_AUTODETECT
#include <LovyanGFX.h>

// Only compile in the font sizes this sketch uses, to save flash.
#define TEXT_SIZES_CUSTOM
#define TEXT_SIZE_2
#define TEXT_SIZE_4
#include "Fonts/Roboto.h"
#include "RollingRate.h"

LGFX display;
RollingRate fps(100);

void setup()
{
   display.init();
}


// Add the main program code into the continuous loop() function
void loop()
{
   fps.tick();

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

   display.loadFont(Roboto_32);
   display.setTextColor(TFT_WHITE, TFT_BLACK);
   display.println(random(9999));
   display.println(random(9999));
   display.println(random(9999));

   display.loadFont(Roboto_16);
   display.setCursor(0, display.height() - display.fontHeight());

#endif

   display.print("FPS: ");
   display.print(fps.get(),1);
   display.print("  "); // erase any remaining characters from previous loop

}
