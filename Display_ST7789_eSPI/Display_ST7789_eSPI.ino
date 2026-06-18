
#include <TFT_eSPI.h>
#include <RollingAverage.h>

#include <Scott16.h>
#include <Scott32.h>

TFT_eSPI display;
RollingAverage fps(100);

// The setup() function runs once each time the micro-controller starts
void setup()
{
   display.init();
   display.setRotation(1);
   display.fillScreen(TFT_BLACK);
   display.setTextColor(TFT_WHITE, TFT_BLACK);
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

   // this code is for the TFT_eSPI anti-aliased fonts

   // 
   // Note for TFT_eSPI - it draws proportional fonts. Even though
   // we are using a monospaced font, the library does not draw each 
   // character with the same width. It knows this and provides special
   // functions (drawNumber, drawFloat) to manually align digits
   //

   display.loadFont(Scott32);
   display.setTextColor(TFT_WHITE, TFT_BLACK, true);
   display.println(random(9999));
   display.println(random(9999));
   display.println(random(9999));

   display.loadFont(Scott16);
   display.setCursor(0, display.height() - display.fontHeight());

#endif

   display.print("FPS: ");
   display.print(fps.get(), 1);
   display.print("  "); // erase any remaining characters from previous loop

   lastMicros = newMicros;
}
