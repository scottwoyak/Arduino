#pragma once

#include <Adafruit_GFX.h> 
#include <Format.h>
#include <string>
#include "Color.h"

class Adafruit_GFX_wInfo
{
public:
   virtual uint8_t charW() = 0;
   virtual uint8_t charH() = 0;
};

class ArduinoWithDisplay
{
private:
   Adafruit_GFX* _display;
   Adafruit_GFX_wInfo* _displayInfo;

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

   ArduinoWithDisplay(Adafruit_GFX* display, Adafruit_GFX_wInfo* displayInfo)
   {
      _display = display;
      _displayInfo = displayInfo;
   }

   virtual void clearDisplay(Color color = Color::BLACK)
   {
      _display->fillScreen((uint16_t)Color::BLACK);
      _display->setCursor(0, 0);
   }

   void setTextSize(uint8_t size)
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
      if (echoToSerial)
      {
         Serial.println();
      }
      _display->println();
   }

   uint8_t charW()
   {
      return _displayInfo->charW();
   }
   uint8_t charH()
   {
      return _displayInfo->charH();
   }

   void printR(const char* str, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      setCursorX(-strlen(str) * charW());
      print(str, textColor, backgroundColor);
   }
   void printlnR(const char* str, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      printR(str, textColor, backgroundColor);
      println();
   }

   void printC(const char* str, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      setCursorX((_display->width() - strlen(str) * charW()) / 2);
      print(str, textColor, backgroundColor);
   }
   void printlnC(const char* str, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      printC(str, textColor, backgroundColor);
      println();
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

   //
   // ------------------------------------------- unsigned long variants
   //
   void print(unsigned long value, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      String str(value);
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(unsigned long value, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      String str(value);
      _println(str.c_str(), textColor, backgroundColor);
   }
   void print(unsigned long value, uint8_t base, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      String str(value, base);
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(unsigned long value, uint8_t base, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      String str(value, base);
      _println(str.c_str(), textColor, backgroundColor);
   }
   void print(unsigned long value, Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(unsigned long value, Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      print(value, format, textColor, backgroundColor);
      println();
   }
};
