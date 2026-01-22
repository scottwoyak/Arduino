#pragma once

#include <Adafruit_ST7789.h> // TFT display
#include <ArduinoWithDisplay.h>
#include <Button.h>
#include <FixedLengthString.h>
#include <string>

class Feather_ESP32_S3 : public ArduinoWithDisplay
{
public:
   Adafruit_ST7789 display;
   Button buttonA;

   Feather_ESP32_S3() : ArduinoWithDisplay(&display), display(TFT_CS, TFT_DC, TFT_RST), buttonA(0)
   {
   }

   void begin()
   {
      // initialize TFT
      pinMode(TFT_BACKLITE, OUTPUT);
      digitalWrite(TFT_BACKLITE, HIGH);

      // turn on the TFT / I2C power supply
      pinMode(TFT_I2C_POWER, OUTPUT);
      digitalWrite(TFT_I2C_POWER, HIGH);
      delay(10);

      display.init(135, 240); // Init ST7789 240x135
      display.setRotation(3);
      display.fillScreen((uint16_t)Color::BLACK);

      display.setTextColor((uint16_t)Color::WHITE, (uint16_t)Color::BLACK);
      display.setTextSize(2);

      buttonA.begin();
   }
};
