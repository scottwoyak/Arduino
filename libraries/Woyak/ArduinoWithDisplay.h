#pragma once

// TODO add more defines for more displays and move to a separate file
#if defined(_ADAFRUIT_ST77XXH_)
#define COLOR_565

#define BUTTON_A 0

enum TextSize : uint8_t
{
   TINY = 1,
   SMALL = 2,
   MEDIUM = 3,
   LARGE = 4,
   HUGE = 5,
};

#elif defined(_Adafruit_GRAYOLED_H_)
#define COLOR_MONOCHROME

enum TextSize : uint8_t
{
   TINY = 1,
   SMALL = 1,
   MEDIUM = 2,
   LARGE = 2,
   HUGE = 3,
};

#endif

enum CharSize : uint8_t
{
   TINY_H = TextSize::TINY * 8,
   SMALL_H = TextSize::SMALL * 8,
   MEDIUM_H = TextSize::MEDIUM * 8,
   LARGE_H = TextSize::LARGE * 8,
   HUGE_H = TextSize::HUGE * 8,

   TINY_W = TextSize::TINY * 6,
   SMALL_W = TextSize::SMALL * 6,
   MEDIUM_W = TextSize::MEDIUM * 6,
   LARGE_W = TextSize::LARGE * 6,
   HUGE_W = TextSize::HUGE * 6,
};

#include <Adafruit_GFX.h> // TFT display
#include <FixedLengthString.h>
#include <string>

#ifdef COLOR_565
enum class Color : uint16_t
{
   BLACK = 0x0000,
   WHITE = 0xFFFF,
   RED = 0xF800,
   GREEN = 0x07E0,
   BLUE = 0x001F,
   CYAN = 0x07FF,
   MAGENTA = 0xF81F,
   YELLOW = 0xFFE0,
   ORANGE = 0xFC00,
   GRAY = 0x8430,
};
#elif defined COLOR_MONOCHROME
enum class Color : uint16_t
{
   WHITE = 1,
   BLACK = 0,
};
#endif


class ArduinoWithDisplay
{
private:
   Adafruit_GFX* _display;

   void _print(const char* str, Color textColor, Color backgroundColor)
   {
      if (echoToSerial)
      {
         Serial.print(str);
      }
      _display->setTextColor((uint16_t)textColor, (uint16_t)backgroundColor);
      _display->print(str);
   }
   void _println(const char* str, Color textColor, Color backgroundColor)
   {
      if (echoToSerial)
      {
         Serial.println(str);
      }
      _display->setTextColor((uint16_t)textColor, (uint16_t)backgroundColor);
      _display->println(str);
   }
   void _print(FixedLengthString& str, Color textColor, Color backgroundColor)
   {
      if (echoToSerial)
      {
         Serial.print(str);
      }
      _display->setTextColor((uint16_t)textColor, (uint16_t)backgroundColor);
      _display->print(str);
   }
   void _println(FixedLengthString& str, Color textColor, Color backgroundColor)
   {
      if (echoToSerial)
      {
         Serial.println(str);
      }
      _display->setTextColor((uint16_t)textColor, (uint16_t)backgroundColor);
      _display->println(str);
   }

public:
   bool echoToSerial = false;

   ArduinoWithDisplay(Adafruit_GFX* display)
   {
      _display = display;
   }

   void clear(Color color = Color::BLACK)
   {
      _display->fillScreen((uint16_t)Color::BLACK);
      _display->setCursor(0, 0);
   }

   void setTextSize(TextSize size)
   {
      _display->setTextSize((uint8_t)size);
   }

   void setCursor(int16_t x, int16_t y)
   {
      if (x < 0)
      {
         x = _display->width() + x;
      }
      if (y < 0)
      {
         y = _display->height() + y;
      }

      _display->setCursor(x, y);
   }
   void setCursorX(int16_t x)
   {
      setCursor(x, _display->getCursorY());
   }
   void setCursorY(int16_t y)
   {
      setCursor(_display->getCursorX(), y);
   }

   void moveCursor(int16_t deltaX, int16_t deltaY)
   {
      int16_t x = _display->getCursorX() + deltaX;
      int16_t y = _display->getCursorY() + deltaY;

      _display->setCursor(x, y);
   }
   void moveCursorX(int16_t deltaX)
   {
      moveCursor(deltaX, 0);
   }
   void moveCursorY(int16_t deltaY)
   {
      moveCursor(0, deltaY);
   }

   void println()
   {
      _display->println();
   }

   //
   // ------------------------------------------- const char* variants
   //
   void print(const char* str, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      _print(str, textColor, backgroundColor);
   }
   void println(const char* str, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      _println(str, textColor, backgroundColor);
   }
   void print(const char* str, uint8_t length, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      FixedLengthString s(str, length);
      _print(s, textColor, backgroundColor);
   }
   void println(const char* str, uint8_t length, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      FixedLengthString s(str, length);
      _println(s, textColor, backgroundColor);
   }

   //
   // ------------------------------------------- String variants
   //
   void print(const String& str, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(const String& str, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      _println(str.c_str(), textColor, backgroundColor);
   }
   void print(const String& str, uint8_t length, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      FixedLengthString s(str, length);
      _print(s, textColor, backgroundColor);
   }
   void println(const String& str, uint8_t length, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      FixedLengthString s(str, length);
      _println(s, textColor, backgroundColor);
   }

   //
   // ------------------------------------------- std::string variants
   //
   void print(const std::string& str, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(const std::string& str, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      _println(str.c_str(), textColor, backgroundColor);
   }
   void print(const std::string& str, uint8_t length, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      FixedLengthString s(str, length);
      _print(s, textColor, backgroundColor);
   }
   void println(const std::string& str, uint8_t length, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      FixedLengthString s(str, length);
      _println(s, textColor, backgroundColor);
   }

   //
   // ------------------------------------------- float variants
   //
   void print(float value, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      String str(value);
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(float value, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      String str(value);
      _println(str.c_str(), textColor, backgroundColor);
   }
   void print(float value, uint8_t length, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      FixedLengthString str(value, length, 2);
      _print(str, textColor, backgroundColor);
   }
   void println(float value, uint8_t length, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      FixedLengthString str(value, length, 2);
      _println(str, textColor, backgroundColor);
   }
   void print(float value, uint8_t length, uint8_t  precision = 2, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      FixedLengthString str(value, length, precision);
      _print(str, textColor, backgroundColor);
   }
   void println(float value, uint8_t length, uint8_t  precision = 2, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      FixedLengthString str(value, length, precision);
      _println(str, textColor, backgroundColor);
   }
   void print(float value, const char* postFix, uint8_t length, uint8_t  precision, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      FixedLengthString str(String(value, (uint)precision) + postFix, length);
      _print(str, textColor, backgroundColor);
   }
   void println(float value, const char* postFix, uint8_t length, uint8_t  precision, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      FixedLengthString str(String(value, (uint)precision) + postFix, length);
      _println(str, textColor, backgroundColor);
   }
   void print(float value, const char* postFix, uint8_t length, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      FixedLengthString str(String(value, 2) + postFix, length);
      _print(str, textColor, backgroundColor);
   }
   void println(float value, const char* postFix, uint8_t length, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      FixedLengthString str(String(value, 2) + postFix, length);
      _println(str, textColor, backgroundColor);
   }

   //
   // ------------------------------------------- double variants
   //
   void print(double value, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      String str(value);
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(double value, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      String str(value);
      _println(str.c_str(), textColor, backgroundColor);
   }
   void print(double value, uint8_t length, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      FixedLengthString str(value, length, 2);
      _print(str, textColor, backgroundColor);
   }
   void println(double value, uint8_t length, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      FixedLengthString str(value, length, 2);
      _println(str, textColor, backgroundColor);
   }
   void print(double value, uint8_t length, uint8_t precision = 2, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      FixedLengthString str(value, length, precision);
      _print(str, textColor, backgroundColor);
   }
   void println(double value, uint8_t length, uint8_t precision = 2, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      FixedLengthString str(value, length, precision);
      _println(str, textColor, backgroundColor);
   }
   void print(double value, const char* postFix, uint8_t length, uint8_t  precision, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      FixedLengthString str(String(value, (uint)precision) + postFix, length);
      _print(str, textColor, backgroundColor);
   }
   void println(double value, const char* postFix, uint8_t length, uint8_t  precision, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      FixedLengthString str(String(value, (uint)precision) + postFix, length);
      _println(str, textColor, backgroundColor);
   }
   void print(double value, const char* postFix, uint8_t length, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      FixedLengthString str(String(value) + postFix, length);
      _print(str, textColor, backgroundColor);
   }
   void println(double value, const char* postFix, uint8_t length, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      FixedLengthString str(String(value) + postFix, length);
      _println(str, textColor, backgroundColor);
   }

   //
   // ------------------------------------------- int variants
   //
   void print(int value, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      String str(value);
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(int value, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      String str(value);
      _println(str.c_str(), textColor, backgroundColor);
   }
   void print(int value, uint8_t length, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      FixedLengthString str(value, length, 10);
      _print(str, textColor, backgroundColor);
   }
   void println(int value, uint8_t length, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      FixedLengthString str(value, length, 10);
      _println(str, textColor, backgroundColor);
   }
   void print(int value, uint8_t length, uint8_t base = 10, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      FixedLengthString str(value, length, base);
      _print(str, textColor, backgroundColor);
   }
   void println(int value, uint8_t length, uint8_t base = 10, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      FixedLengthString str(value, length, base);
      _println(str, textColor, backgroundColor);
   }
   void print(int value, const char* postFix, uint8_t length, uint8_t  base, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      FixedLengthString str(String(value, base) + postFix, length);
      _print(str, textColor, backgroundColor);
   }
   void println(int value, const char* postFix, uint8_t length, uint8_t  base, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      FixedLengthString str(String(value, base) + postFix, length);
      _println(str, textColor, backgroundColor);
   }
   void print(int value, const char* postFix, uint8_t length, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      FixedLengthString str(String(value) + postFix, length);
      _print(str, textColor, backgroundColor);
   }
   void println(int value, const char* postFix, uint8_t length, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      FixedLengthString str(String(value) + postFix, length);
      _println(str, textColor, backgroundColor);
   }

   //
   // ------------------------------------------- long variants
   //
   void print(long value, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      String str(value);
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(long value, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      String str(value);
      _println(str.c_str(), textColor, backgroundColor);
   }
   void print(long value, uint8_t length, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      FixedLengthString str(value, length, 10);
      _print(str, textColor, backgroundColor);
   }
   void println(long value, uint8_t length, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      FixedLengthString str(value, length, 10);
      _println(str, textColor, backgroundColor);
   }
   void print(long value, uint8_t length, uint8_t base = 10, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      FixedLengthString str(value, length, base);
      _print(str, textColor, backgroundColor);
   }
   void println(long value, uint8_t length, uint8_t base = 10, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      FixedLengthString str(value, length, base);
      _println(str, textColor, backgroundColor);
   }
   void print(long value, const char* postFix, uint8_t length, uint8_t  base, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      FixedLengthString str(String(value, base) + postFix, length);
      _print(str, textColor, backgroundColor);
   }
   void println(long value, const char* postFix, uint8_t length, uint8_t  base, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      FixedLengthString str(String(value, base) + postFix, length);
      _println(str, textColor, backgroundColor);
   }
   void print(long value, const char* postFix, uint8_t length, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      FixedLengthString str(String(value) + postFix, length);
      _print(str, textColor, backgroundColor);
   }
   void println(long value, const char* postFix, uint8_t length, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      FixedLengthString str(String(value) + postFix, length);
      _println(str, textColor, backgroundColor);
   }
};
