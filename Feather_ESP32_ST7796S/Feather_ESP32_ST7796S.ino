#include "Feather_ESP32_S3_ST7796S.h"
#include "SerialX.h"

constexpr auto PIN_BACKLITE = 5;
constexpr auto PIN_DC = 6;
constexpr auto PIN_RST = 9;
constexpr auto PIN_CS = 10;
Feather_ESP32_S3_ST7796S feather(PIN_CS, PIN_DC, PIN_RST, PIN_BACKLITE);

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
   feather.setTextSize(4);
   feather.setCursor(0, 0);
   feather.println(counter++, Color::VALUE);
   feather.moveCursorY(feather.charH() / 4);

   feather.setTextSize(3);
   feather.print("ButtonA: ", Color::LABEL);
   feather.println(feather.buttonA.isPressed() ? "TRUE " : "FALSE", Color::VALUE);

   feather.setTextSize(2);
   feather.setCursor(0, -feather.charH());
   feather.print(fps,1);
   feather.print(" fps ");

   lastMicros = newMicros;
}
