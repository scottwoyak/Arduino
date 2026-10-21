//
// ST7789V2 display test sketch for the Waveshare ESP32-S3-Touch-LCD-1.9 board.
//
// Continuously renders random numbers, touch coordinates (when touched), and frame
// rate on the 1.9" 170x320 ST7789V2 display using LovyanGFX and the Woyak board wrapper.
//

#include <Arduino.h>

// Only compile in the font sizes this sketch uses, to save flash.
#define TEXT_SIZE_2
#define TEXT_SIZE_4

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display."
#include "WrongBoard.h"
#endif

#include "RollingRate.h"
#include "SerialX.h"

Arduino arduino;
RollingRate fps(100);
Format valueFormat("####", Format::Alignment::LEFT);
Format fpsFormat("###.#/s", Format::Alignment::RIGHT);
Format coordFormat("###", Format::Alignment::LEFT);

void setup()
{
   SerialX::begin();
   arduino.begin();

   Serial.println("--- Waveshare ESP32-S3-Touch-LCD-1.9 Display Settings ---");
   Serial.printf("Display Width: %d px\n", arduino.width());
   Serial.printf("Display Height: %d px\n", arduino.height());
}

void loop()
{
   fps.tick();

   arduino.setTextSize(4);
   arduino.setCursor(0, 0);
   arduino.println((uint16_t)random(9999), valueFormat);
   arduino.println((uint16_t)random(9999), valueFormat);
   arduino.println((uint16_t)random(9999), valueFormat);

   arduino.setTextSize(2);
   int16_t footerY = arduino.height() - arduino.charH();

   lgfx::touch_point_t touchPoint;
   if (arduino.display.getTouch(&touchPoint) > 0)
   {
      arduino.setCursor(0, footerY);
      arduino.print("T:", Color::LIGHTBLUE);
      arduino.print(touchPoint.x, coordFormat, Color::LIGHTBLUE);
      arduino.print(",", Color::LIGHTBLUE);
      arduino.print(touchPoint.y, coordFormat, Color::LIGHTBLUE);
   }
   else
   {
      arduino.setCursor(0, footerY);
      arduino.print("         ");
   }

   arduino.setCursorY(footerY);
   arduino.printlnR(fps.get(), fpsFormat, Color::LIGHTGRAY);
}

