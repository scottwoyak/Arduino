#pragma once

#include <Adafruit_GFX.h> 
#include <Format.h>
#include <string>

// TODO add more defines for more displays and move to a separate file
#if defined(_ADAFRUIT_ST77XXH_)
#define COLOR_565

constexpr auto BUTTON_A = 0;

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
   DARKGRAY = 0x6B4D,

   HEADING = ORANGE,
   LABEL = WHITE,
   VALUE = YELLOW,
   VALUE2 = CYAN,
   SUB_LABEL = GRAY,
};
#elif defined COLOR_MONOCHROME
enum class Color : uint16_t
{
   WHITE = 1,
   BLACK = 0,
   RED = 1,
   GREEN = 1,
   BLUE = 1,
   CYAN = 1,
   MAGENTA = 1,
   YELLOW = 1,
   ORANGE = 1,
   GRAY = 1,
   DARKGRAY = 0,

   HEADING = WHITE,
   LABEL = WHITE,
   VALUE = WHITE,
   VALUE2 = WHITE,
   SUB_LABEL = WHITE,
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
   void setTextSize(uint8_t size)
   {
      _display->setTextSize(size);
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
      if (echoToSerial)
      {
         Serial.println();
      }
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
   void print(const char* str, Format format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      std::string s = format.toString(str);
      _print(s.c_str(), textColor, backgroundColor);
   }
   void println(const char* str, Format format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      print(str, format, textColor, backgroundColor);
      println();
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
   void print(String& str, Format format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      std::string s = format.toString(str);
      _print(s.c_str(), textColor, backgroundColor);
   }
   void println(String& str, Format format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      print(str, format, textColor, backgroundColor);
      println();
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
   void print(const std::string& str, Format format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      std::string s = format.toString(str);
      _print(s.c_str(), textColor, backgroundColor);
   }
   void println(const std::string& str, Format format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      print(str, format, textColor, backgroundColor);
      println();
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
   void print(float value, uint precision, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      String str(value, precision);
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(float value, uint precision, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      String str(value, precision);
      _println(str.c_str(), textColor, backgroundColor);
   }
   void print(float value, Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(float value, Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      print(value, format, textColor, backgroundColor);
      println();
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
   void print(double value, uint precision, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      String str(value, precision);
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(double value, uint precision, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      String str(value, precision);
      _println(str.c_str(), textColor, backgroundColor);
   }
   void print(double value, Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(double value, Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      print(value, format, textColor, backgroundColor);
      println();
   }

   //
   // ------------------------------------------- uint8_t variants
   //
   void print(uint8_t value, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      String str(value);
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(uint8_t value, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      String str(value);
      _println(str.c_str(), textColor, backgroundColor);
   }
   void print(uint8_t value, uint8_t base, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      String str(value, base);
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(uint8_t value, uint8_t base, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      String str(value, base);
      _println(str.c_str(), textColor, backgroundColor);
   }
   void print(uint8_t value, Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(uint8_t value, Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      print(value, format, textColor, backgroundColor);
      println();
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
   void print(int value, uint8_t base, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      String str(value, base);
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(int value, uint8_t base, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      String str(value, base);
      _println(str.c_str(), textColor, backgroundColor);
   }
   void print(int value, Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(int value, Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      print(value, format, textColor, backgroundColor);
      println();
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
   void print(long value, uint8_t base, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      String str(value, base);
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(long value, uint8_t base, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      String str(value, base);
      _println(str.c_str(), textColor, backgroundColor);
   }
   void print(long value, Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(long value, Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      print(value, format, textColor, backgroundColor);
      println();
   }
};
