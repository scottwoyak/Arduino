#include "Feather_ESP32_S3.h"

Feather_ESP32_S3 feather;

void setup()
{
   feather.begin();
}

long counter = 0;
long lastMicros = micros();

void loop()
{
   long newMicros = micros();
   double fps = 1000000.0 / (newMicros - lastMicros);

   // note: this display isn't double buffered. Past contents are not cleared,
   // but text characters overwrite past content. That's why there is an extra
   // space after "fps" - some rates are over 100 and some are 2 digits.
   feather.display.setTextSize(4);
   feather.display.setCursor(0, 0);
   feather.display.print(counter++);
   feather.display.setTextSize(2);
   feather.display.setCursor(0, feather.display.height() - 16);
   feather.display.print(fps);
   feather.display.print(" fps ");

   lastMicros = newMicros;
}
