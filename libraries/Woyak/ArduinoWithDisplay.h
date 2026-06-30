#pragma once

#include "LGX_FeatherESP32_S3_TFT.h"
#include "Format.h"
#include <string>
#include "Color.h"
#include "Structs.h"

#include "Fonts/RobotoMonoBold.h"
#include "Fonts/Roboto.h"

/// <summary>Display rotation orientation options.</summary>
enum DisplayRotation
{
   /// <summary>Portrait orientation (0 degrees).</summary>
   PORTRAIT = 0,
   /// <summary>Landscape orientation (90 degrees).</summary>
   LANDSCAPE = 1,
   /// <summary>Portrait flipped orientation (180 degrees).</summary>
   PORTRAIT_FLIP = 2,
   /// <summary>Landscape flipped orientation (270 degrees).</summary>
   LANDSCAPE_FLIP = 3,
};

/// <summary>Base class for Arduino platforms.</summary>
class Arduino
{
public:
   /// <summary>Initialize the Arduino platform.</summary>
   virtual void begin() = 0;
};

/// <summary>Arduino platform with integrated display support for graphics and text rendering.</summary>
class ArduinoWithDisplay : Arduino
{
private:
   void _print(const char* str, Color textColor, Color backgroundColor)
   {
      if (echoToSerial)
      {
         Serial.print(str);
      }

      display.setTextColor((uint16_t)textColor, (uint16_t)backgroundColor);
      display.print(str);
   }
   void _println(const char* str, Color textColor, Color backgroundColor)
   {
      if (echoToSerial)
      {
         Serial.println(str);
      }

      display.setTextColor((uint16_t)textColor, (uint16_t)backgroundColor);
      display.println(str);
   }


public:
   /// <summary>Display driver instance for rendering graphics and text.</summary>
   LGFX display;
   /// <summary>If true, all text output is echoed to the serial port.</summary>
   bool echoToSerial = false;

   /// <summary>Initializes a new instance of the ArduinoWithDisplay class.</summary>
   ArduinoWithDisplay()
   {}

   /// <summary>Initializes the display with default settings (landscape, black background, white text).</summary>
   void begin() override
   {
      display.init();

      display.setRotation(DisplayRotation::LANDSCAPE);
      display.fillScreen((uint16_t)Color::BLACK);

      display.setTextColor((uint16_t)Color::WHITE, (uint16_t)Color::BLACK);
      display.setTextSize(2);
      display.setTextWrap(false);
      display.setBrightness(255);
   }

   /// <summary>Gets the display width in pixels.</summary>
   /// <returns>Width of the display in pixels.</returns>
   uint16_t width()
   {
      return display.width();
   }

   /// <summary>Gets the display height in pixels.</summary>
   /// <returns>Height of the display in pixels.</returns>
   uint16_t height()
   {
      return display.height();
   }

   /// <summary>Gets the current font height in pixels.</summary>
   /// <returns>Height of the current font in pixels.</returns>
   uint8_t charH()
   {
      return display.fontHeight();
   }

   /// <summary>Gets the current font character width in pixels.</summary>
   /// <returns>Width of a character in the current font in pixels.</returns>
   uint8_t charW()
   {
      // if monospaced, all chars return the same width. If not, '0' is an average width
      // and will be the same for all digits
      const lgfx::v1::VLWfont* font = (const lgfx::v1::VLWfont*) display.getFont();
      uint16_t gNum;
      font->getUnicodeIndex(0x30, &gNum);
      return font->gxAdvance[gNum];

      // Note: textWidth returns the gxAdvance value for all characters of a string except
      // the last character which only returns gdX+glyphWidth for a more pixel perfect
      // width calculation. This means that when asking for the width of a single character,
      // you don't get the full advance and thus the need for us to manually compute the
      // value above.
      //return display.textWidth("0");
   }

   /// <summary>Sets the display rotation orientation.</summary>
   /// <param name="rotation">The desired rotation orientation.</param>
   void setRotation(DisplayRotation rotation)
   {
      display.setRotation((uint8_t)rotation);
   }

   /// <summary>Clears the display by filling it with the specified color and resetting cursor to origin.</summary>
   /// <param name="color">The fill color; defaults to black.</param>
   virtual void clearDisplay(Color color = Color::BLACK)
   {
      display.fillScreen((uint16_t)color);
      display.setCursor(0, 0);
   }

   void printFontMetrics()
   {
      const lgfx::v1::VLWfont* font = (const lgfx::v1::VLWfont*) display.getFont();

      Serial.println("---------------------------------------------------------------");
      Serial.print("font->gCount: ");
      Serial.println(font->gCount);
      Serial.print("font->yAdvance: ");
      Serial.println(font->yAdvance);
      Serial.print("font->ascent: ");
      Serial.println(font->ascent);
      Serial.print("font->descent: ");
      Serial.println(font->descent);
      Serial.print("font->maxAscent: ");
      Serial.println(font->maxAscent);
      Serial.print("font->maxDescent: ");
      Serial.println(font->maxDescent);

      for (uint16_t i = 0; i < font->gCount; i++)
      {
         Serial.print(i);
         Serial.print(" '");
         if (font->gUnicode[i] < 256)
         {
            Serial.print((char)font->gUnicode[i]);
         }
         Serial.print("'");
         Serial.print("\t");
         Serial.print(" gxAdvance:");
         Serial.print(font->gxAdvance[i]);
         Serial.print("\t");
         Serial.print(" gdX:");
         Serial.print(font->gdX[i]);
         Serial.print("\t");
         Serial.print(" gWidth:");
         Serial.print(font->gWidth[i]);

         Serial.println();
      }
   }

   /// <summary>Sets the text font size and spacing mode.</summary>
   /// <param name="size">Font size index (0-7); outside this range is constrained to valid bounds.</param>
   /// <param name="mono">If true, uses monospaced font; if false, uses proportional font. Defaults to true.</param>
   void setTextSize(uint8_t size, bool mono = true)
   {
      // LGFX uses setTextSize to set a scaling factor. We instead load a properly
      // sized font. Make sure to set the scaling factor back to 1
      display.setTextSize(1);

      size = constrain(size, 0, 7);
      if (mono)
      {
         display.loadFont(RobotoMonoBold[size]);
         //display.loadFont(Roboto[size]); // uncomment this to see if we can force a proportional font to be monospace
      }
      else
      {
         display.loadFont(Roboto[size]);
      }

      //printFontMetrics();

      // if we're making this a monospaced font, adjust each characters dimensions
      if (mono)
      {
         // this is unsafe code - we are accessing an internal LGFX data structure
         // and modifying it
         lgfx::v1::VLWfont* font = (lgfx::v1::VLWfont*)(display.getFont());

         // get the max char width and make them all the same
         uint8_t maxAdvance = 0;
         for (uint16_t i = 0; i < font->gCount; i++)
         {
            maxAdvance = std::max(maxAdvance, font->gxAdvance[i]);
         }

         // TODO LGFX doesn't actually use any of these values. I think it gets the values
         // from the data structure each time. We need to implement our own IFont class
         // that derives from VLWfont and overrides updateFontMetric(), I think
         /*
         for (uint16_t i = 0; i < font->gCount; i++)
         {
            font->gxAdvance[i] = maxAdvance;
            font->gdX[i] = (maxAdvance - font->gWidth[i]) / 2;
            font->gWidth[i] = maxAdvance;
         }
         */

         // TFT_eSPI & LGFX guess at the space width. Make it the monospace value
         font->spaceWidth = maxAdvance;
      }
   }

   /// <summary>Fills a rectangular area with the specified color, supporting negative coordinates as offsets from the far edge.</summary>
   /// <param name="x">X coordinate; negative values offset from right edge.</param>
   /// <param name="y">Y coordinate; negative values offset from bottom edge.</param>
   /// <param name="w">Rectangle width in pixels.</param>
   /// <param name="h">Rectangle height in pixels.</param>
   /// <param name="color">Fill color.</param>
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

      display.fillRect(x, y, w, h, (uint16_t)color);

   }

   /// <summary>Sets the cursor position, supporting negative coordinates as offsets from the far edge.</summary>
   /// <param name="x">X coordinate; negative values offset from right edge.</param>
   /// <param name="y">Y coordinate; negative values offset from bottom edge.</param>
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

   /// <summary>Sets the cursor X coordinate while preserving the Y coordinate.</summary>
   /// <param name="x">X coordinate; negative values offset from right edge.</param>
   void setCursorX(int16_t x)
   {
      setCursor(x, display.getCursorY());
   }
   /// <summary>Sets the cursor Y coordinate while preserving the X coordinate.</summary>
   /// <param name="y">Y coordinate; negative values offset from bottom edge.</param>
   void setCursorY(int16_t y)
   {
      setCursor(display.getCursorX(), y);
   }

   /// <summary>Moves the cursor by the specified offsets from its current position.</summary>
   /// <param name="deltaX">Horizontal offset in pixels.</param>
   /// <param name="deltaY">Vertical offset in pixels.</param>
   void moveCursor(int16_t deltaX, int16_t deltaY)
   {
      int16_t x = display.getCursorX() + deltaX;
      int16_t y = display.getCursorY() + deltaY;

      display.setCursor(x, y);
   }
   /// <summary>Moves the cursor horizontally by the specified offset.</summary>
   /// <param name="deltaX">Horizontal offset in pixels.</param>
   void moveCursorX(int16_t deltaX)
   {
      moveCursor(deltaX, 0);
   }
   /// <summary>Moves the cursor vertically by the specified offset.</summary>
   /// <param name="deltaY">Vertical offset in pixels.</param>
   void moveCursorY(int16_t deltaY)
   {
      moveCursor(0, deltaY);
   }
   /// <summary>Gets the current cursor X coordinate.</summary>
   /// <returns>Current X coordinate in pixels.</returns>
   int16_t getCursorX()
   {
      return display.getCursorX();
   }
   /// <summary>Gets the current cursor Y coordinate.</summary>
   /// <returns>Current Y coordinate in pixels.</returns>
   int16_t getCursorY()
   {
      return display.getCursorY();
   }
   /// <summary>Gets the current cursor position as a point.</summary>
   /// <returns>Point containing current cursor X and Y coordinates.</returns>
   Point16 getCursor()
   {
      return Point16(display.getCursorX(), display.getCursorY());
   }
   /// <summary>Sets the cursor position from a Point16 coordinate.</summary>
   /// <param name="pt">Point containing X and Y coordinates.</param>
   void setCursor(Point16 pt)
   {
      setCursor(pt.x, pt.y);
   }

   /// <summary>Prints a newline and optionally echoes to serial.</summary>
   void println()
   {
      if (echoToSerial)
      {
         Serial.println();
      }
      display.println();
   }

   // ----------- Labeled print variants (print label + value with separate colors)
   /// <summary>Prints a label and formatted value on the same line with separate colors.</summary>
   /// <param name="label">Label text (printed in LABEL color).</param>
   /// <param name="value">Value to print (printed in valueColor).</param>
   /// <param name="valueColor">Color for the value; defaults to VALUE color.</param>
   /// <param name="backgroundColor">Background color; defaults to BLACK.</param>
   template<typename TValue>
   void print(const char* label, const TValue& value, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(label, Color::LABEL, backgroundColor);
      print(value, valueColor, backgroundColor);
   }
   template<typename TValue>
   void println(const char* label, const TValue& value, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(label, value, valueColor, backgroundColor);
      println();
   }
   template<typename TValue>
   void print(const char* label, const TValue& value, const Format& format, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(label, Color::LABEL, backgroundColor);
      print(value, format, valueColor, backgroundColor);
   }
   template<typename TValue>
   void println(const char* label, const TValue& value, const Format& format, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(label, value, format, valueColor, backgroundColor);
      println();
   }
   template<typename TValue>
   void printR(const char* label, const TValue& value, const Format& format, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      std::string valueText = format.toString(value);
      std::string rowText = std::string(label) + valueText;
      uint16_t len = display.textWidth(rowText.c_str());
      setCursorX(-len);
      print(label, Color::LABEL, backgroundColor);
      print(value, format, valueColor, backgroundColor);
   }
   template<typename TValue>
   void printlnR(const char* label, const TValue& value, const Format& format, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      printR(label, value, format, valueColor, backgroundColor);
      println();
   }
   template<typename TValue>
   void printR(const char* label, const Format& format, const TValue& value, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      printR(label, value, format, valueColor, backgroundColor);
   }
   template<typename TValue>
   void printlnR(const char* label, const Format& format, const TValue& value, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      printlnR(label, value, format, valueColor, backgroundColor);
   }
   template<typename TValue>
   void printC(const char* label, const TValue& value, const Format& format, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      std::string valueText = format.toString(value);
      std::string rowText = std::string(label) + valueText;
      uint16_t len = display.textWidth(rowText.c_str());
      setCursorX((display.width() - len) / 2);
      print(label, Color::LABEL, backgroundColor);
      print(value, format, valueColor, backgroundColor);
   }
   template<typename TValue>
   void printlnC(const char* label, const TValue& value, const Format& format, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      printC(label, value, format, valueColor, backgroundColor);
      println();
   }
   template<typename TValue>
   void printC(const char* label, const Format& format, const TValue& value, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      printC(label, value, format, valueColor, backgroundColor);
   }
   template<typename TValue>
   void printlnC(const char* label, const Format& format, const TValue& value, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      printlnC(label, value, format, valueColor, backgroundColor);
   }

   // ----------- Right-aligned print variants (printR - aligns text to right edge)
   /// <summary>Prints a string right-aligned (flush to right edge of display).</summary>
   /// <param name="str">String to print.</param>
   /// <param name="textColor">Text color; defaults to WHITE.</param>
   /// <param name="backgroundColor">Background color; defaults to BLACK.</param>
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


   // ----------- Center-aligned print variants (printC - aligns text to center of display)
   /// <summary>Prints a string centered horizontally on the display.</summary>
   /// <param name="str">String to print.</param>
   /// <param name="textColor">Text color; defaults to WHITE.</param>
   /// <param name="backgroundColor">Background color; defaults to BLACK.</param>
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
   void printR(uint8_t value, const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      printR(str.c_str(), textColor, backgroundColor);
   }
   void printlnR(uint8_t value, const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      printlnR(str.c_str(), textColor, backgroundColor);
   }

   //
   // ------------------------------------------- uint16_t variants
   //
   void print(uint16_t value, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      String str(value);
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(uint16_t value, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      String str(value);
      _println(str.c_str(), textColor, backgroundColor);
   }
   void print(uint16_t value, uint8_t base, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      String str(value, base);
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(uint16_t value, uint8_t base, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      String str(value, base);
      _println(str.c_str(), textColor, backgroundColor);
   }
   void print(uint16_t value, const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(uint16_t value, const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      print(value, format, textColor, backgroundColor);
      println();
   }
   void printR(uint16_t value, const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      printR(str.c_str(), textColor, backgroundColor);
   }
   void printlnR(uint16_t value, const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      printlnR(str.c_str(), textColor, backgroundColor);
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
   void printR(int value, const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      printR(str.c_str(), textColor, backgroundColor);
   }
   void printlnR(int value, const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      printlnR(str.c_str(), textColor, backgroundColor);
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
   void printR(long value, const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      printR(str.c_str(), textColor, backgroundColor);
   }
   void printlnR(long value, const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      printlnR(str.c_str(), textColor, backgroundColor);
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
   void printR(unsigned long value, const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      printR(str.c_str(), textColor, backgroundColor);
   }
   void printlnR(unsigned long value, const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      printlnR(str.c_str(), textColor, backgroundColor);
   }

   //
   // ------------------------------------------- size_t variants
   //
   void print(size_t value, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      print((unsigned long)value, textColor, backgroundColor);
   }
   void println(size_t value, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      println((unsigned long)value, textColor, backgroundColor);
   }
   void print(size_t value, uint8_t base, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      print((unsigned long)value, base, textColor, backgroundColor);
   }
   void println(size_t value, uint8_t base, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      println((unsigned long)value, base, textColor, backgroundColor);
   }
   void print(size_t value, const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      print((unsigned long)value, format, textColor, backgroundColor);
   }
   void println(size_t value, const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      println((unsigned long)value, format, textColor, backgroundColor);
   }
   void printR(size_t value, const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      printR((unsigned long)value, format, textColor, backgroundColor);
   }
   void printlnR(size_t value, const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      printlnR((unsigned long)value, format, textColor, backgroundColor);
   }
};
