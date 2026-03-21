#pragma once

#include "TFT_eSPI.h"
#include <Format.h>
#include <string>
#include "Color.h"
#include "Structs.h"

#include "Fonts/RobotoMonoRegular08.h"
#include "Fonts/RobotoMonoRegular16.h"
#include "Fonts/RobotoMonoRegular24.h"
#include "Fonts/RobotoMonoRegular32.h"
#include "Fonts/RobotoMonoRegular40.h"
#include "Fonts/RobotoMonoRegular48.h"
#include "Fonts/RobotoMonoRegular56.h"
#include "Fonts/RobotoMonoRegular64.h"

#include "Fonts/RobotoRegular08.h"
#include "Fonts/RobotoRegular16.h"
#include "Fonts/RobotoRegular24.h"
#include "Fonts/RobotoRegular32.h"
#include "Fonts/RobotoRegular40.h"
#include "Fonts/RobotoRegular48.h"
#include "Fonts/RobotoRegular56.h"
#include "Fonts/RobotoRegular64.h"

enum DisplayRotation
{
   PORTRAIT = 0,
   LANDSCAPE = 1,
   PORTRAIT_FLIP = 2,
   LANDSCAPE_FLIP = 3,
};

class ArduinoWithDisplay
{
private:
   void _print(const char* str, Color textColor, Color backgroundColor)
   {
      if (echoToSerial)
      {
         Serial.print(str);
      }
      display.setTextColor((uint16_t)textColor, (uint16_t)backgroundColor, true);
      display.print(str);
   }
   void _println(const char* str, Color textColor, Color backgroundColor)
   {
      if (echoToSerial)
      {
         Serial.println(str);
      }
      display.setTextColor((uint16_t)textColor, (uint16_t)backgroundColor, true);
      display.println(str);
   }

public:
   TFT_eSPI display;
   bool echoToSerial = false;

   ArduinoWithDisplay()
   {
   }

   uint16_t width()
   {
      return display.width();
   }

   uint16_t height()
   {
      return display.height();
   }

   uint8_t charH()
   {
      return display.fontHeight();
   }

   uint8_t charW()
   {
      // if monospaced, all chars return the same width. If not, '0' is an average width
      // and will be the same for all digits
      return display.textWidth("0");
   }

   void setRotation(DisplayRotation rotation)
   {
      display.setRotation((uint8_t)rotation);
   }  

   virtual void clearDisplay(Color color = Color::BLACK)
   {
      display.fillScreen((uint16_t)color);
      display.setCursor(0, 0);
   }

   void printFontMetrics()
   {
      Serial.println("---------------------------------------------------------------");
      Serial.print("TextSize: ");
      Serial.println(display.textsize);
      Serial.print("gFont.gCount: ");
      Serial.println(display.gFont.gCount);
      Serial.print("gFont.yAdvance: ");
      Serial.println(display.gFont.yAdvance);
      Serial.print("gFont.ascent: ");
      Serial.println(display.gFont.ascent);
      Serial.print("gFont.descent: ");
      Serial.println(display.gFont.descent);
      Serial.print("gFont.maxAscent: ");
      Serial.println(display.gFont.maxAscent);
      Serial.print("gFont.maxDescent: ");
      Serial.println(display.gFont.maxDescent);

      for (uint16_t i = 0; i < display.gFont.gCount; i++)
      {
         Serial.print(i);
         Serial.print(" '");
         if (display.gUnicode[i] < 256)
         {
            Serial.print((char) display.gUnicode[i]);
         }
         Serial.print("'");
         Serial.print("\t");
         Serial.print(" gxAdvance:");
         Serial.print(display.gxAdvance[i]);
         Serial.print("\t");
         Serial.print(" gdX:");
         Serial.print(display.gdX[i]);
         Serial.print("\t");
         Serial.print(" gWidth:");
         Serial.print(display.gWidth[i]);
         Serial.print("\t");
         Serial.print(" gdY:");
         Serial.print(display.gdY[i]);
         Serial.print("\t");
         Serial.print(" gHeight:");
         Serial.print(display.gHeight[i]);

         Serial.println();
      }
   }

   void setTextSize(uint8_t size, bool mono=true)
   {
      display.setTextSize(size);

      if (mono)
      {
         switch (size)
         {
         case 0:
         case 1:
            display.loadFont(RobotoMonoRegular08);
            break;

         case 2:
            display.loadFont(RobotoMonoRegular16);
            break;

         case 3:
            display.loadFont(RobotoMonoRegular24);
            break;

         case 4:
            display.loadFont(RobotoMonoRegular32);
            break;

         case 5:
            display.loadFont(RobotoMonoRegular40);
            break;

         case 6:
            display.loadFont(RobotoMonoRegular48);
            break;

         case 7:
            display.loadFont(RobotoMonoRegular56);
            break;

         case 8:
            display.loadFont(RobotoMonoRegular64);
            break;

         default:
            display.loadFont(RobotoMonoRegular24);
         }
      }
      else
      {
         switch (size)
         {
         case 0:
         case 1:
            display.loadFont(RobotoRegular08);
            break;

         case 2:
            display.loadFont(RobotoRegular16);
            break;

         case 3:
            display.loadFont(RobotoRegular24);
            break;

         case 4:
            display.loadFont(RobotoRegular32);
            break;

         case 5:
            display.loadFont(RobotoRegular40);
            break;

         case 6:
            display.loadFont(RobotoRegular48);
            break;

         case 7:
            display.loadFont(RobotoRegular56);
            break;

         case 8:
            display.loadFont(RobotoRegular64);
            break;

         default:
            display.loadFont(RobotoRegular24);
         }
      }

      //printFontMetrics();

      // correct for the use of negative values for descents
      if (((int16_t)display.gFont.maxDescent) < 0)
      {
         display.gFont.maxDescent = -((int16_t) display.gFont.maxDescent);
         display.gFont.yAdvance = display.gFont.maxAscent + display.gFont.maxDescent;
      }
      if (display.gFont.descent < 0)
      {
         display.gFont.descent = -display.gFont.descent;
      }

      // if we're making this a monospaced font, adjust each characters dimensions
      if (mono)
      {
         // get the max char width and make them all the same
         uint8_t maxAdvance = 0;
         for (uint16_t i = 0; i < display.gFont.gCount; i++)
         {
            maxAdvance = std::max(maxAdvance, display.gxAdvance[i]);
         }

         for (uint16_t i = 0; i < display.gFont.gCount; i++)
         {
            display.gxAdvance[i] = maxAdvance;
            display.gdX[i] = (maxAdvance - display.gWidth[i])/2;
         }

         // TFT_eSPI guesses at the space width. Make it the monospace value
         display.gFont.spaceWidth = maxAdvance;
      }
   }

   void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, Color color)
   {
      if (x < 0)
      {
         x = display.width() + x;
      }
      if (y < 0)
      {
         y = display.height() + y;
      }

      display.fillRect(x, y, w, h, (uint16_t) color);

   }

   void setCursor(int16_t x, int16_t y)
   {
      if (x < 0)
      {
         x = display.width() + x;
      }
      if (y < 0)
      {
         y = display.height() + y;
      }

      display.setCursor(x, y);
   }
   void setCursorX(int16_t x)
   {
      setCursor(x, display.getCursorY());
   }
   void setCursorY(int16_t y)
   {
      setCursor(display.getCursorX(), y);
   }

   void moveCursor(int16_t deltaX, int16_t deltaY)
   {
      int16_t x = display.getCursorX() + deltaX;
      int16_t y = display.getCursorY() + deltaY;

      display.setCursor(x, y);
   }
   void moveCursorX(int16_t deltaX)
   {
      moveCursor(deltaX, 0);
   }
   void moveCursorY(int16_t deltaY)
   {
      moveCursor(0, deltaY);
   }
   Point16 getCursor()
   {
      return Point16(display.getCursorX(), display.getCursorY());
   }
   void setCursor(Point16 pt)
   {
      setCursor(pt.x, pt.y);
   }

   void println()
   {
      if (echoToSerial)
      {
         Serial.println();
      }
      display.println();
   }

   //
   // ------------------------------------------- printR variants
   //
   void printR(const char* str, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      uint16_t len = display.textWidth(str);
      setCursorX(-len);
      print(str, textColor, backgroundColor);
   }
   void printlnR(const char* str, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      printR(str, textColor, backgroundColor);
      println();
   }
   void printR(std::string str, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      printR(str.c_str(), textColor, backgroundColor);
   }
   void printlnR(std::string str, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      printlnR(str.c_str(), textColor, backgroundColor);
   }


   //
   // ------------------------------------------- printC variants
   //
   void printC(const char* str, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      uint16_t len = display.textWidth(str);
      setCursorX((display.width() - len) / 2);
      print(str, textColor, backgroundColor);
   }
   void printlnC(const char* str, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      printC(str, textColor, backgroundColor);
      println();
   }
   void printC(std::string str, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      printC(str.c_str(), textColor, backgroundColor);
   }
   void printlnC(std::string str, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      printlnC(str.c_str(), textColor, backgroundColor);
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
   void print(const char* str, const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      std::string s = format.toString(str);
      _print(s.c_str(), textColor, backgroundColor);
   }
   void println(const char* str, const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
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
   void print(String& str, const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      std::string s = format.toString(str);
      _print(s.c_str(), textColor, backgroundColor);
   }
   void println(String& str, const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
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
   void print(const std::string& str, const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      std::string s = format.toString(str);
      _print(s.c_str(), textColor, backgroundColor);
   }
   void println(const std::string& str, const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
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
   void print(float value, const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(float value, const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      print(value, format, textColor, backgroundColor);
      println();
   }
   void printR(float value, const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      printR(str.c_str(), textColor, backgroundColor);
   }
   void printlnR(float value, const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      printlnR(str.c_str(), textColor, backgroundColor);
   }
   void printC(float value, const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      printC(str.c_str(), textColor, backgroundColor);
   }
   void printlnC(float value, const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      printlnC(str.c_str(), textColor, backgroundColor);
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
   void print(double value, const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(double value, const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      print(value, format, textColor, backgroundColor);
      println();
   }
   void printR(double value, const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      printR(str.c_str(), textColor, backgroundColor);
   }
   void printlnR(double value, const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      printlnR(str.c_str(), textColor, backgroundColor);
   }
   void printC(double value, const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      printC(str.c_str(), textColor, backgroundColor);
   }
   void printlnC(double value, const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      printlnC(str.c_str(), textColor, backgroundColor);
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
   void print(uint8_t value, const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(uint8_t value, const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
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
   void print(int value, const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(int value, const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
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
   void print(long value, const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(long value, const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
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
   void print(unsigned long value, const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(unsigned long value, const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      print(value, format, textColor, backgroundColor);
      println();
   }
};
