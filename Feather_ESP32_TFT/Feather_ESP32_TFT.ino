#include "Feather_ESP32_S3.h"
#include "RollingRate.h"

Feather_ESP32_S3 feather;
RollingRate fps(100);

void setup()
{
   feather.begin();
}

long counter = 0;

void loop()
{
   fps.tick();

   // note: this display isn't double buffered. Past contents are not cleared,
   // but text characters overwrite past content. That's why there is an extra
   // space after "fps" - some rates are over 100 and some are 2 digits.
   feather.setTextSize(5);
   feather.setCursor(0, 0);
   feather.println(counter++, Color::VALUE);
   feather.moveCursorY(feather.charH() / 4);

   feather.setTextSize(3);
   feather.print("ButtonA: ", Color::LABEL);
   feather.println(feather.buttonA.isPressed() ? "TRUE " : "FALSE", Color::VALUE);

   feather.setCursor(0, -feather.charH());
   feather.print(fps.get(), 1);
   feather.print(" fps ");

}
