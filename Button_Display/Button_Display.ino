
#include "ArduinoBoard.h"

#ifndef ARDUINO_BUTTON_SUPPORTED
#error "This sketch requires a board with button support (e.g. Feather ESP32-S3 or Feather M0)."
#endif
#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include "Stopwatch.h"

Arduino arduino;

Stopwatch sw(false);
uint16_t lastCount = 0;

//Button button(6);
Button& button = arduino.buttonA;

Format pinFormat("##");
Format boolFormat(5);
Format timeFormat("##.#s");

void setup()
{
   Serial.begin(115200);
   arduino.begin();
   button.begin();
   button.autoReset = false;
}

void loop()
{
   if ((button.wasPressed() && sw.isRunning() == false) || button.isPressed())
   {
      sw.reset();
      sw.start();
   }

   arduino.setTextSize(3);
   arduino.setCursor(0, 0);

   arduino.println("Pin: ", button.getPin(), pinFormat);
   arduino.println(" Is: ", button.isPressed() ? "True" : "False", boolFormat);
   arduino.println("Was: ", button.wasPressed() ? "True" : "False", boolFormat);
   arduino.println("  #: ", button.getPressedCount());

   if (lastCount != button.getPressedCount())
   {
      sw.reset();
   }
   lastCount = button.getPressedCount();

   if (sw.elapsedSecs() > 5)
   {
      button.reset();
      sw.stop();
      sw.reset();
   }

   if (sw.isRunning())
   {
      arduino.display.setTextSize(2);
      arduino.setCursor(0, arduino.display.height() - arduino.charH());
      arduino.print("Resetting in ", Color::SUB_LABEL);
      arduino.print((5 - sw.elapsedSecs()), timeFormat, Color::SUB_LABEL);
   }
   else
   {
      // cover up the time remaining msg
      arduino.display.fillRect(0, arduino.display.height() - arduino.charH(), arduino.display.width(), arduino.charH(), (uint16_t)Color::BLACK);
   }
}


