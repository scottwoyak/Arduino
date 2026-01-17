#pragma once

#include <Adafruit_ST7789.h> // TFT display

class Feather_ESP32_S3
{
public:
   Adafruit_ST7789 display;

   Feather_ESP32_S3() : display(TFT_CS, TFT_DC, TFT_RST)
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
      display.fillScreen(ST77XX_BLACK);

      display.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
      display.setTextSize(2);
   }
};
