#pragma once

#include <Adafruit_ST7789.h> // TFT display
#include <FixedLengthString.h>
#include <string>

enum class Color565 : uint16_t
{
   WHITE = ST77XX_WHITE,
   BLACK = ST77XX_BLACK,
   RED = ST77XX_RED,
   GREEN = ST77XX_GREEN,
   BLUE = ST77XX_BLUE,
   CYAN = ST77XX_CYAN,
   MAGENTA = ST77XX_MAGENTA,
   YELLOW = ST77XX_YELLOW,
   ORANGE = ST77XX_ORANGE,
   GRAY = 0x8430,
};

class Feather_ESP32_S3
{
private:
   void _print(FixedLengthString& str, Color565 textColor, Color565 backgroundColor)
   {
      if (echoToSerial)
      {
         Serial.print(str);
      }
      display.setTextColor((uint16_t)textColor, (uint16_t)backgroundColor);
      display.print(str);
   }
   void _println(FixedLengthString& str, Color565 textColor, Color565 backgroundColor)
   {
      _print(str, textColor, backgroundColor);
      if (echoToSerial)
      {
         Serial.println();
      }
      display.println();
   }

public:
   Adafruit_ST7789 display;
   bool echoToSerial = false;

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
      display.fillScreen((uint16_t)Color565::BLACK);

      display.setTextColor((uint16_t)Color565::WHITE, (uint16_t)Color565::BLACK);
      display.setTextSize(2);
   }

   void clear(Color565 color = Color565::BLACK)
   {
      display.fillScreen((uint16_t)Color565::BLACK);
      display.setCursor(0, 0);
   }

   void setCursor(int16_t x, int16_t y)
   {
      display.setCursor(x, y);
   }
   void setCursorX(int16_t x)
   {
      display.setCursor(x, display.getCursorY());
   }
   void setCursorY(int16_t y)
   {
      display.setCursor(display.getCursorX(), y);
   }

   void println()
   {
      display.println();
   }

   void print(const char* str, Color565 textColor = Color565::WHITE, Color565 backgroundColor = Color565::BLACK)
   {
      print(str, 0, textColor, backgroundColor);
   }
   void println(const char* str, Color565 textColor = Color565::WHITE, Color565 backgroundColor = Color565::BLACK)
   {
      println(str, 0, textColor, backgroundColor);
   }
   void print(const char* str, uint8_t length, Color565 textColor = Color565::WHITE, Color565 backgroundColor = Color565::BLACK)
   {
      FixedLengthString s(str, length);
      _print(s, textColor, backgroundColor);
   }
   void println(const char* str, uint8_t length, Color565 textColor = Color565::WHITE, Color565 backgroundColor = Color565::BLACK)
   {
      FixedLengthString s(str, length);
      _println(s, textColor, backgroundColor);
   }

   void print(const String& str, Color565 textColor = Color565::WHITE, Color565 backgroundColor = Color565::BLACK)
   {
      print(str, 0, textColor, backgroundColor);
   }
   void println(const String& str, Color565 textColor = Color565::WHITE, Color565 backgroundColor = Color565::BLACK)
   {
      println(str, 0, textColor, backgroundColor);
   }
   void print(const String& str, uint8_t length, Color565 textColor = Color565::WHITE, Color565 backgroundColor = Color565::BLACK)
   {
      FixedLengthString s(str, length);
      _print(s, textColor, backgroundColor);
   }
   void println(const String& str, uint8_t length, Color565 textColor = Color565::WHITE, Color565 backgroundColor = Color565::BLACK)
   {
      FixedLengthString s(str, length);
      _println(s, textColor, backgroundColor);
   }

   void print(const std::string& str, Color565 textColor = Color565::WHITE, Color565 backgroundColor = Color565::BLACK)
   {
      FixedLengthString s(str, 0);
      _print(s, textColor, backgroundColor);
   }
   void println(const std::string& str, Color565 textColor = Color565::WHITE, Color565 backgroundColor = Color565::BLACK)
   {
      FixedLengthString s(str, 0);
      _println(s, textColor, backgroundColor);
   }
   void print(const std::string& str, uint8_t length, Color565 textColor = Color565::WHITE, Color565 backgroundColor = Color565::BLACK)
   {
      FixedLengthString s(str, length);
      _print(s, textColor, backgroundColor);
   }
   void println(const std::string& str, uint8_t length, Color565 textColor = Color565::WHITE, Color565 backgroundColor = Color565::BLACK)
   {
      FixedLengthString s(str, length);
      _println(s, textColor, backgroundColor);
   }



   void print(float value, uint8_t length, uint8_t  precision = 2, Color565 textColor = Color565::WHITE, Color565 backgroundColor = Color565::BLACK)
   {
      FixedLengthString str(value, length, precision);
      _print(str, textColor, backgroundColor);
   }
   void println(float value, uint8_t length, uint8_t  precision = 2, Color565 textColor = Color565::WHITE, Color565 backgroundColor = Color565::BLACK)
   {
      FixedLengthString str(value, length, precision);
      _println(str, textColor, backgroundColor);
   }
   void print(float value, const char* postFix, uint8_t length, uint8_t  precision = 2, Color565 textColor = Color565::WHITE, Color565 backgroundColor = Color565::BLACK)
   {
      FixedLengthString str(String(value, (uint) precision) + postFix, length);
      _print(str, textColor, backgroundColor);
   }
   void println(float value, const char* postFix, uint8_t length, uint8_t  precision = 2, Color565 textColor = Color565::WHITE, Color565 backgroundColor = Color565::BLACK)
   {
      FixedLengthString str(String(value, (uint) precision) + postFix, length);
      _println(str, textColor, backgroundColor);
   }

   void print(double value, uint8_t length, uint8_t precision = 2, Color565 textColor = Color565::WHITE, Color565 backgroundColor = Color565::BLACK)
   {
      FixedLengthString str(value, length, precision);
      _print(str, textColor, backgroundColor);
   }
   void println(double value, uint8_t length, uint8_t precision = 2, Color565 textColor = Color565::WHITE, Color565 backgroundColor = Color565::BLACK)
   {
      FixedLengthString str(value, length, precision);
      _println(str, textColor, backgroundColor);
   }
   void print(double value, const char* postFix, uint8_t length, uint8_t  precision = 2, Color565 textColor = Color565::WHITE, Color565 backgroundColor = Color565::BLACK)
   {
      FixedLengthString str(String(value, (uint) precision) + postFix, length);
      _print(str, textColor, backgroundColor);
   }
   void println(double value, const char* postFix, uint8_t length, uint8_t  precision = 2, Color565 textColor = Color565::WHITE, Color565 backgroundColor = Color565::BLACK)
   {
      FixedLengthString str(String(value, (uint) precision) + postFix, length);
      _println(str, textColor, backgroundColor);
   }

   void print(int value, uint8_t length, uint8_t base = 10, Color565 textColor = Color565::WHITE, Color565 backgroundColor = Color565::BLACK)
   {
      FixedLengthString str(value, length, base);
      _print(str, textColor, backgroundColor);
   }
   void println(int value, uint8_t length, uint8_t base = 10, Color565 textColor = Color565::WHITE, Color565 backgroundColor = Color565::BLACK)
   {
      FixedLengthString str(value, length, base);
      _println(str, textColor, backgroundColor);
   }
   void print(int value, const char* postFix, uint8_t length, uint8_t  base = 10, Color565 textColor = Color565::WHITE, Color565 backgroundColor = Color565::BLACK)
   {
      FixedLengthString str(String(value, base) + postFix, length);
      _print(str, textColor, backgroundColor);
   }
   void println(int value, const char* postFix, uint8_t length, uint8_t  base = 10, Color565 textColor = Color565::WHITE, Color565 backgroundColor = Color565::BLACK)
   {
      FixedLengthString str(String(value, base) + postFix, length);
      _println(str, textColor, backgroundColor);
   }

   void print(long value, uint8_t length, uint8_t base = 10, Color565 textColor = Color565::WHITE, Color565 backgroundColor = Color565::BLACK)
   {
      FixedLengthString str(value, length, base);
      _print(str, textColor, backgroundColor);
   }
   void println(long value, uint8_t length, uint8_t base = 10, Color565 textColor = Color565::WHITE, Color565 backgroundColor = Color565::BLACK)
   {
      FixedLengthString str(value, length, base);
      _println(str, textColor, backgroundColor);
   }
   void print(long value, const char* postFix, uint8_t length, uint8_t  base = 10, Color565 textColor = Color565::WHITE, Color565 backgroundColor = Color565::BLACK)
   {
      FixedLengthString str(String(value, base) + postFix, length);
      _print(str, textColor, backgroundColor);
   }
   void println(long value, const char* postFix, uint8_t length, uint8_t  base = 10, Color565 textColor = Color565::WHITE, Color565 backgroundColor = Color565::BLACK)
   {
      FixedLengthString str(String(value, base) + postFix, length);
      _println(str, textColor, backgroundColor);
   }
};
