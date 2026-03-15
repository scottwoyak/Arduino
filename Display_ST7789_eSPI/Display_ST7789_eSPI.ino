
#include <TFT_eSPI.h>

#include <Scott24.h>

TFT_eSPI tft;

// The setup() function runs once each time the micro-controller starts
void setup()
{
   tft.init();
   tft.setRotation(1);
   tft.fillScreen(TFT_BLACK);
   tft.setTextColor(TFT_WHITE, TFT_BLACK);
}

long lastMicros = micros();

// Add the main program code into the continuous loop() function
void loop()
{
   unsigned long newMicros = micros();
   float fps = 1000000.0 / (newMicros - lastMicros);

   tft.setCursor(0, 0);

//#define OLD_FONTS
#ifdef OLD_FONTS

   // this code is for drawing the traditional Adafruit style block fonts
   tft.setTextSize(4);
   tft.print(random(999));

   tft.setTextSize(2);
   tft.setCursor(0, tft.height() - 16);
   tft.print(fps, 1);
   tft.print(" fps ");

#else

   // this code is for the TFT_eSPI anti-aliased fonts

   // 
   // Note for TFT_eSPI - it draws proportional fonts. Even though
   // we are using a monospaced font, the library does not draw each 
   // character with the same width. It knows this and provides special
   // functions (drawNumber, drawFloat) to manually align digits
   //

   tft.loadFont(Scott24);
   tft.setTextColor(TFT_WHITE, TFT_BLACK, true);
   tft.drawNumber(random(999), 0, 0);

   //tft.loadFont(Scott);
   tft.setCursor(0, tft.height() - tft.fontHeight());
   tft.drawFloat(fps, 1, tft.getCursorX(), tft.getCursorY());
   tft.print(" fps  ");

#endif

   lastMicros = newMicros;
}
