#define LGFX_AUTODETECT
#include <LovyanGFX.h>

LGFX display;

void setup()
{
   display.init();
}

long lastMicros = micros();

// Add the main program code into the continuous loop() function
void loop()
{
   long newMicros = micros();
   double fps = 1000000.0 / (newMicros - lastMicros);

   display.setTextSize(4);
   display.setCursor(0, 0);
   display.println(random(9999));
   display.println(random(9999));
   display.println(random(9999));

   display.setTextSize(2);
   display.setCursor(0, display.height() - 16);
   display.print(fps, 1);
   display.print(" fps ");

   lastMicros = newMicros;
}
