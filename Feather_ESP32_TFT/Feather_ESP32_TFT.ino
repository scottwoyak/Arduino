#include "ArduinoBoard.h"

#ifndef ARDUINO_BUTTON_SUPPORTED
#error "This sketch requires a board with button support (e.g. Feather ESP32-S3 or Feather M0)."
#endif
#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include "RollingRate.h"

Arduino arduino;
RollingRate fps(100);

void setup()
{
   arduino.begin();
}

long counter = 0;

void loop()
{
   fps.tick();

   // note: this display isn't double buffered. Past contents are not cleared,
   // but text characters overwrite past content. That's why there is an extra
   // space after "fps" - some rates are over 100 and some are 2 digits.
   arduino.setTextSize(5);
   arduino.setCursor(0, 0);
   arduino.println(counter++, Color::VALUE);
   arduino.moveCursorY(arduino.charH() / 4);

   arduino.setTextSize(3);
   arduino.println("ButtonA: ", arduino.buttonA.isPressed() ? "TRUE " : "FALSE");

   arduino.setCursor(0, -arduino.charH());
   arduino.print(fps.get(), 1);
   arduino.print(" fps ");

}
