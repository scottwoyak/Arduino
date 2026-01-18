#pragma once

#include <Adafruit_ST7789.h> // TFT display

#define WHITE ST77XX_WHITE
#define BLACK ST77XX_BLACK
#define RED ST77XX_RED
#define GREEN ST77XX_GREEN
#define BLUE ST77XX_BLUE
#define CYAN ST77XX_CYAN
#define MAGENTA ST77XX_MAGENTA
#define YELLOW ST77XX_YELLOW
#define ORANGE ST77XX_ORANGE

class Feather_ESP32_S3
{
public:
   Adafruit_ST7789 display;
   bool echoToSerial = true;

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
      display.fillScreen(BLACK);

      display.setTextColor(WHITE, BLACK);
      display.setTextSize(2);
   }

   void print(const char* str, uint16_t textColor = WHITE, uint16_t backgroundColor = BLACK)
   {
      if (echoToSerial)
      {
         Serial.print(str);
      }
      display.setTextColor(textColor, backgroundColor);
      display.print(str);
   }

   void println(const char* str, uint16_t textColor = WHITE, uint16_t backgroundColor = BLACK)
   {
      if (echoToSerial)
      {
         Serial.println(str);
      }
      display.setTextColor(textColor, backgroundColor);
      display.println(str);
   }

   void print(const String& str, uint16_t textColor = WHITE, uint16_t backgroundColor = BLACK)
   {
      if (echoToSerial)
      {
         Serial.print(str);
      }
      display.setTextColor(textColor, backgroundColor);
      display.print(str);
   }

   void println(const String& str, uint16_t textColor = WHITE, uint16_t backgroundColor = BLACK)
   {
      if (echoToSerial)
      {
         Serial.println(str);
      }
      display.setTextColor(textColor, backgroundColor);
      display.println(str);
   }

   void print(double value, int numDigits = 2, uint16_t textColor = WHITE, uint16_t backgroundColor = BLACK)
   {
      if (echoToSerial)
      {
         Serial.print(value, numDigits);
      }
      display.setTextColor(textColor, backgroundColor);
      display.print(value, numDigits);
   }

   void println(double value, int numDigits = 2, uint16_t textColor = WHITE, uint16_t backgroundColor = BLACK)
   {
      if (echoToSerial)
      {
         Serial.println(value, numDigits);
      }
      display.setTextColor(textColor, backgroundColor);
      display.print(value, numDigits);
   }
};
