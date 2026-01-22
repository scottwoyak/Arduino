#pragma once

#include <Adafruit_SH110X.h>
#include <ArduinoWithDisplay.h>
#include <Button.h>
#include <FixedLengthString.h>
#include <string>

class Feather_M0_OLED : public ArduinoWithDisplay
{
public:
   Adafruit_SH1107 display;
   Button buttonA;
   Button buttonB;
   Button buttonC;

   Feather_M0_OLED() : ArduinoWithDisplay(&display), display(64, 128, &Wire), buttonA(9), buttonB(6), buttonC(5)
   {
   }

   void begin()
   {
      buttonA.begin();
      buttonB.begin();
      buttonC.begin();

      display.begin(0x3C, true); // Address 0x3C default
      display.setTextColor((uint16_t)Color::WHITE);
      display.setRotation(1);
      display.clearDisplay();
      display.display();
   }
};
