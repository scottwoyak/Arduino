#pragma once

#include <string>

#ifndef ARDUINO_DISPLAY_SUPPORTED
#define ARDUINO_DISPLAY_SUPPORTED
#endif

#include "ArduinoBoardId.h"

// Ensure the board's default heading and text sizes are always compiled in when a sketch
// opts into a custom, reduced TEXT_SIZE_n list via TEXT_SIZES_CUSTOM (see
// Fonts/RobotoMonoBold.h/Roboto.h) so printInitHeader() and InitializingDisplay always
// have their required fonts available at startup.
#ifdef TEXT_SIZES_CUSTOM
#ifndef TEXT_SIZE_2
#define TEXT_SIZE_2
#endif
#ifndef TEXT_SIZE_3
#define TEXT_SIZE_3
#endif
#if defined(ARDUINO_HOSYOND_ESP32S3_TOUCH_LCD_40) || defined(ARDUINO_HOSYOND_ESP32_S3_VIEWER) || defined(ARDUINO_ESP32S3_DEV_PLAYGROUND)
#ifndef TEXT_SIZE_4
#define TEXT_SIZE_4
#endif
#endif
#endif

#include "ArduinoBase.h"
#include "ColorX.h"
#include "Format.h"
#include "Fonts/RobotoMonoBold.h"

// Proportional Roboto font data is excluded by default to save flash. Sketches that use
// proportional text can define INCLUDE_PROPORTIONAL_FONT before including ArduinoBoard.h.
// Requesting a proportional font without INCLUDE_PROPORTIONAL_FONT fails the ASSERT in
// setTextSize()/createSprite().
#ifdef INCLUDE_PROPORTIONAL_FONT
#include "Fonts/Roboto.h"
#define PROPORTIONAL_FONT(size) (Roboto[size])
#else
#define PROPORTIONAL_FONT(size) ((const uint8_t*)nullptr)
#endif

#include "OTAUpdater.h"
#include "Structs.h"

///
/// <summary>
/// Display rotation orientation options.
/// </summary>
///
enum DisplayRotation
{
   PORTRAIT = 0,
   LANDSCAPE = 1,
   PORTRAIT_FLIP = 2,
   LANDSCAPE_FLIP = 3,
};

class InitializingDisplay;

///
/// <summary>
/// Minimum display height, in pixels, at which headers use a larger text size.
/// </summary>
///
constexpr uint16_t LARGE_DISPLAY_MIN_HEIGHT = 320;

///
/// <summary>
/// Arduino platform with integrated display support for graphics and text rendering.
/// </summary>
///
class ArduinoWithDisplay : public ArduinoBase
{
private:
   void _print(const char* str, Color textColor, Color backgroundColor)
   {
      ArduinoBase::print(str, textColor, backgroundColor);
      display.setTextColor((uint16_t)textColor, (uint16_t)backgroundColor);
      display.print(str);
   }

   void _println(const char* str, Color textColor, Color backgroundColor)
   {
      ArduinoBase::println(str, textColor, backgroundColor);
      display.setTextColor((uint16_t)textColor, (uint16_t)backgroundColor);
      display.println(str);
   }

   void _normalizeCoords(int16_t& x, int16_t& y)
   {
      if (x < 0)
      {
         x = display.width() + x;
      }
      if (y < 0)
      {
         y = display.height() + y;
      }
   }

   void _printD(const std::string& str, const Format& format, Color textColor, Color backgroundColor)
   {
      int16_t offsetPixels = (int16_t)(format.decimalOffset() * charW());
      setCursorX(display.width() / 2 - offsetPixels);
      print(str.c_str(), textColor, backgroundColor);
   }

public:
   ///
   /// <summary>
   /// Display driver instance for rendering graphics and text.
   /// </summary>
   ///
   LGFX display;

   ///
   /// <summary>
   /// Normalizes negative coordinates to be offsets from the display's far edge, e.g.
   /// a value of -1 becomes display.width()-1 (or display.height()-1 for y).
   /// </summary>
   /// <param name="x">X coordinate; negative values offset from the right edge.</param>
   /// <param name="y">Y coordinate; negative values offset from the bottom edge.</param>
   ///
   void normalizeCoords(int16_t& x, int16_t& y)
   {
      _normalizeCoords(x, y);
   }

private:
   uint8_t _textSize = 1;

   // hardcoded character width/height (in pixels) for each RobotoMonoBold font size
   // index (1-7, index 0 is unused since there is no font size 0), measured once on
   // device and pasted in here since these values are fixed for a given font and never
   // change at runtime
   struct CharMetrics
   {
      uint8_t width;
      uint8_t height;
   };
   static constexpr CharMetrics _charMetricsTable[8] =
   {
      { 0,  0 },  // size 0 - unused, there is no font size 0
      { 5,  8 },  // size 1 - RobotoMonoBold_8
      { 9,  17 }, // size 2 - RobotoMonoBold_16
      { 14, 25 }, // size 3 - RobotoMonoBold_24
      { 18, 32 }, // size 4 - RobotoMonoBold_32
      { 22, 40 }, // size 5 - RobotoMonoBold_40
      { 27, 49 }, // size 6 - RobotoMonoBold_48
      { 31, 56 }, // size 7 - RobotoMonoBold_56
   };

public:

   ///
   /// <summary>
   /// Initializes a new instance of the ArduinoWithDisplay class.
   /// </summary>
   ///
   ArduinoWithDisplay() {}

   ///
   /// <summary>
   /// Initializes the display with default settings (landscape, black background, white text).
   /// </summary>
   ///
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

   ///
   /// <summary>
   /// Initializes the display (as begin()) and then connects to WiFi, printing consistently
   /// formatted "WiFi..." status text via ArduinoBase::initWifi. Use this overload instead of
   /// begin() when the sketch wants the standard WiFi connect status text.
   /// </summary>
   /// <param name="ssid">The WiFi network name.</param>
   /// <param name="password">The WiFi network password.</param>
   /// <param name="status">Optional status indicator updated to WIFI_CONNECTING while connecting.</param>
   /// <param name="syncTime">True to sync the system clock via NTP after connecting.</param>
   ///
   void begin(const char* ssid, const char* password, IStatus* status = nullptr, bool syncTime = true)
   {
      begin();
      initWifi(ssid, password, status, syncTime);
   }

   ///
   /// <summary>
   /// Gets the display width in pixels.
   /// </summary>
   /// <returns>Width of the display in pixels.</returns>
   ///
   uint16_t width()
   {
      return display.width();
   }

   ///
   /// <summary>
   /// Gets the display height in pixels.
   /// </summary>
   /// <returns>Height of the display in pixels.</returns>
   ///
   uint16_t height()
   {
      return display.height();
   }

   ///
   /// <summary>
   /// Gets the center point of the display in pixels.
   /// </summary>
   /// <returns>The X/Y coordinate of the display's center.</returns>
   ///
   Point16 center()
   {
      return Point16(width() / 2, height() / 2);
   }

   ///
   /// <summary>
   /// Gets the font height in pixels for the given font size, or the current font size
   /// if not specified.
   /// </summary>
   /// <param name="size">Font size index (0-7) to measure; defaults to the current font size.</param>
   /// <returns>Height of the font in pixels.</returns>
   ///
   uint8_t charH(int16_t size = -1)
   {
      if (size < 0)
      {
         return display.fontHeight();
      }

      return _charMetricsTable[constrain(size, 0, 7)].height;
   }

   ///
   /// <summary>
   /// Gets the character width in pixels for the given font size, or the current font
   /// size if not specified. Since font metrics are constant, results are cached the
   /// first time each size is measured.
   /// </summary>
   /// <param name="size">Font size index (0-7) to measure; defaults to the current font size.</param>
   /// <returns>Width of a character in the given font size, in pixels.</returns>
   ///
   uint8_t charW(int16_t size = -1)
   {
      if (size < 0)
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

      return _charMetricsTable[constrain(size, 0, 7)].width;
   }

   ///
   /// <summary>
   /// Gets the precise rendered pixel width of a string in the current font, accounting
   /// for the fact that the last character only contributes its ink width rather than
   /// its full advance width. Use this instead of length() * charW() when a string must
   /// be pixel-accurately right-aligned or centered.
   /// </summary>
   /// <param name="str">String to measure.</param>
   /// <returns>Width of the string in pixels as it will actually be rendered.</returns>
   ///
   uint16_t textWidth(const char* str)
   {
      return display.textWidth(str);
   }

   ///
   /// <summary>
   /// Overload of textWidth(const char*) accepting a std::string so callers don't need
   /// to call .c_str() themselves.
   /// </summary>
   /// <param name="str">String to measure.</param>
   /// <returns>Width of the string in pixels as it will actually be rendered.</returns>
   ///
   uint16_t textWidth(const std::string& str)
   {
      return textWidth(str.c_str());
   }

   ///
   /// <summary>
   /// Indicates whether a "label value" status line fits within the display width at the
   /// current text size.
   /// </summary>
   /// <param name="label">The label text.</param>
   /// <param name="value">The value text.</param>
   /// <returns>True if the label and value fit on one display row; otherwise false.</returns>
   ///
   bool fitsOnDisplay(const char* label, const char* value) override
   {
      // Init status rows are drawn at InitializingDisplay's default size (DEFAULT_TEXT_SIZE),
      // which may differ from the current size (e.g. the larger header size).
      uint8_t originalSize = _textSize;
      setTextSize(DEFAULT_TEXT_SIZE);
      bool fits = textWidth(label) + textWidth(value) <= display.width();
      setTextSize(originalSize);

      return fits;
   }

   ///
   /// <summary>
   /// Sets the display rotation orientation.
   /// </summary>
   /// <param name="rotation">The desired rotation orientation.</param>
   ///
   void setRotation(DisplayRotation rotation)
   {
      display.setRotation((uint8_t)rotation);
   }

   ///
   /// <summary>
   /// Gets a counter that increments every time the display (or a region of it) is cleared,
   /// so cached drawing (e.g. DisplayValue) can tell when its pixels may have been erased.
   /// </summary>
   /// <returns>The number of clears performed so far.</returns>
   ///
   uint32_t clearCount() const
   {
      return _clearCount;
   }

   ///
   /// <summary>
   /// Clears the display by filling it with the specified color and resetting cursor to origin.
   /// </summary>
   /// <param name="color">The fill color; defaults to black.</param>
   ///
   virtual void clearDisplay(Color color = Color::BLACK)
   {
      _clearCount++;
      display.fillScreen((uint16_t)color);
      display.setCursor(0, 0);
   }

   ///
   /// <summary>
   /// Clears a rectangular region of the display by filling it with the specified color.
   /// </summary>
   /// <param name="rect">The rectangle to clear.</param>
   /// <param name="color">The fill color; defaults to black.</param>
   ///
   void clear(Rect16 rect, Color color = Color::BLACK)
   {
      _clearCount++;
      display.fillRect(rect.x, rect.y, rect.width, rect.height, (uint16_t)color);
   }

   ///
   /// <summary>
   /// Prints detailed font glyph metrics to the serial port for debugging.
   /// </summary>
   ///
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

   ///
   /// <summary>
   /// Sets the text font size and spacing mode.
   /// </summary>
   /// <param name="size">Font size index (0-7); outside this range is constrained to valid bounds.</param>
   /// <param name="mono">If true, uses monospaced font; if false, uses proportional font. Defaults to true.</param>
   ///
   void setTextSize(uint8_t size, bool mono = true)
   {
      _textSize = size;

      // LGFX uses setTextSize to set a scaling factor. We instead load a properly
      // sized font. Make sure to set the scaling factor back to 1
      display.setTextSize(1);

      size = constrain(size, 0, 7);

      // A null entry means this size was compiled out via TEXT_SIZES_CUSTOM/TEXT_SIZE_n
      // in the sketch; loadFont() would crash trying to read through it, so fail loudly
      // here instead so the missing TEXT_SIZE_n define is obvious.
      ASSERT((mono ? RobotoMonoBold[size] : PROPORTIONAL_FONT(size)) != nullptr);

      if (mono)
      {
         display.loadFont(RobotoMonoBold[size]);
         //display.loadFont(Roboto[size]); // uncomment this to see if we can force a proportional font to be monospace
      }
      else
      {
         display.loadFont(PROPORTIONAL_FONT(size));
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

   ///
   /// <summary>
   /// Creates and sizes an off-screen sprite with its own copy of the given font size,
   /// applying the same monospace space-width fix as setTextSize() so that space-padded
   /// alignment renders correctly within the sprite.
   /// </summary>
   /// <param name="sprite">The sprite to initialize; must not have already had createSprite() called on it.</param>
   /// <param name="width">The sprite's width, in pixels.</param>
   /// <param name="height">The sprite's height, in pixels.</param>
   /// <param name="size">Font size index (0-7); outside this range is constrained to valid bounds.</param>
   /// <param name="mono">If true, uses monospaced font; if false, uses proportional font. Defaults to true.</param>
   ///
   void createSprite(LGFX_Sprite& sprite, int16_t width, int16_t height, uint8_t size, bool mono = true)
   {
      sprite.setColorDepth(16);
      sprite.createSprite(width, height);

      size = constrain(size, 0, 7);

      // See setTextSize() for why this guards against a compiled-out font size.
      ASSERT((mono ? RobotoMonoBold[size] : PROPORTIONAL_FONT(size)) != nullptr);

      // load our own copy of the font rather than sharing the display's runtime font
      // pointer, which can be freed out from under us if the display later loads a
      // different font
      if (mono)
      {
         sprite.loadFont(RobotoMonoBold[size]);
      }
      else
      {
         sprite.loadFont(PROPORTIONAL_FONT(size));
      }

      if (mono)
      {
         // this is unsafe code - we are accessing an internal LGFX data structure
         // and modifying it
         lgfx::v1::VLWfont* font = (lgfx::v1::VLWfont*)(sprite.getFont());

         // get the max char width and make them all the same
         uint8_t maxAdvance = 0;
         for (uint16_t i = 0; i < font->gCount; i++)
         {
            maxAdvance = std::max(maxAdvance, font->gxAdvance[i]);
         }

         // TFT_eSPI & LGFX guess at the space width. Make it the monospace value
         font->spaceWidth = maxAdvance;
      }
   }

   ///
   /// <summary>
   /// Gets the text size most recently set via setTextSize().
   /// </summary>
   /// <returns>The current font size index.</returns>
   ///
   uint8_t getTextSize() const
   {
      return _textSize;
   }

   ///
   /// <summary>
   /// Fills a rectangular area with the specified color, supporting negative coordinates as offsets from the far edge.
   /// </summary>
   /// <param name="x">X coordinate; negative values offset from right edge.</param>
   /// <param name="y">Y coordinate; negative values offset from bottom edge.</param>
   /// <param name="w">Rectangle width in pixels.</param>
   /// <param name="h">Rectangle height in pixels.</param>
   /// <param name="color">Fill color.</param>
   ///
   void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, Color color)
   {
      _normalizeCoords(x, y);
      display.fillRect(x, y, w, h, (uint16_t)color);
   }

   ///
   /// <summary>
   /// Draws a pixel with the specified color, supporting negative coordinates as offsets from the far edge.
   /// </summary>
   /// <param name="x">X coordinate; negative values offset from right edge.</param>
   /// <param name="y">Y coordinate; negative values offset from bottom edge.</param>
   /// <param name="color">Pixel color.</param>
   ///
   void drawPixel(int16_t x, int16_t y, Color color)
   {
      _normalizeCoords(x, y);
      display.drawPixel(x, y, (uint16_t)color);
   }

   ///
   /// <summary>
   /// Draws a vertical run of pixels with the specified color, supporting negative coordinates as offsets from the far edge.
   /// </summary>
   /// <param name="x">X coordinate; negative values offset from right edge.</param>
   /// <param name="y">Starting Y coordinate; negative values offset from bottom edge.</param>
   /// <param name="h">Number of pixels to draw downward from y.</param>
   /// <param name="color">Line color.</param>
   ///
   void drawFastVLine(int16_t x, int16_t y, int16_t h, Color color)
   {
      _normalizeCoords(x, y);
      display.drawFastVLine(x, y, h, (uint16_t)color);
   }

   ///
   /// <summary>
   /// Bulk-pushes a single row of already-resolved RGB565 colors starting at (x, y), one
   /// SPI/DMA transfer for the whole row instead of one per pixel. Intended for full-repaint
   /// scenarios (e.g. DisplayBuffer::draw(true)) where every pixel in a row is being
   /// redrawn anyway, so resolving a row into a small stack/heap buffer once and pushing it
   /// in one call is far faster than calling drawPixel() once per pixel. Must be called
   /// between startWrite()/endWrite().
   /// </summary>
   /// <param name="x">Starting X coordinate of the row.</param>
   /// <param name="y">Y coordinate of the row.</param>
   /// <param name="colors">Pointer to w consecutive RGB565 colors, one per pixel.</param>
   /// <param name="w">Number of pixels in the row.</param>
   ///
   void pushImageRow(int16_t x, int16_t y, const uint16_t* colors, int16_t w)
   {
      // Passing a raw uint16_t* to pushImage() makes LGFX assume byte-swapped ("swap565")
      // input unless setSwapBytes(true) was called; colors here are already in the same
      // native RGB565 order that drawPixel() expects, so reinterpret as lgfx::rgb565_t to
      // bypass that swap heuristic - otherwise every pushed pixel comes out with scrambled
      // R/G/B bits (e.g. red/magenta speckling instead of the intended solid color).
      display.pushImage(x, y, w, 1, reinterpret_cast<const lgfx::rgb565_t*>(colors));
   }

   ///
   /// <summary>
   /// Bulk-pushes a single column of already-resolved RGB565 colors starting at (x, y), one
   /// SPI/DMA transfer for the whole vertical run instead of one per pixel. Intended for
   /// diffed redraws (e.g. DisplayBuffer::draw(false)) where a contiguous vertical run of
   /// pixels within a column changed since the previous frame, so pushing the whole run in
   /// one call is far faster than calling drawPixel() once per changed pixel. Must be called
   /// between startWrite()/endWrite().
   /// </summary>
   /// <param name="x">X coordinate of the column.</param>
   /// <param name="y">Starting Y coordinate of the run.</param>
   /// <param name="colors">Pointer to h consecutive RGB565 colors, one per pixel.</param>
   /// <param name="h">Number of pixels in the run.</param>
   ///
   void pushImageColumn(int16_t x, int16_t y, const uint16_t* colors, int16_t h)
   {
      // See pushImageRow()'s remarks: reinterpret as lgfx::rgb565_t so LGFX treats this as
      // already-native RGB565 data rather than byte-swapping it.
      display.pushImage(x, y, 1, h, reinterpret_cast<const lgfx::rgb565_t*>(colors));
   }

   ///
   /// <summary>
   /// Begins a batched sequence of drawing calls, deferring the underlying transaction
   /// (e.g. SPI) so multiple draw calls can be sent together instead of one transaction
   /// each. Must be paired with a matching endWrite() call.
   /// </summary>
   ///
   void startWrite()
   {
      display.startWrite();
   }

   ///
   /// <summary>
   /// Ends a batched sequence of drawing calls started with startWrite().
   /// </summary>
   ///
   void endWrite()
   {
      display.endWrite();
   }

   ///
   /// <summary>
   /// Draws a line between two points with the specified color, supporting negative coordinates as offsets from the far edge.
   /// </summary>
   /// <param name="x0">Start X coordinate; negative values offset from right edge.</param>
   /// <param name="y0">Start Y coordinate; negative values offset from bottom edge.</param>
   /// <param name="x1">End X coordinate; negative values offset from right edge.</param>
   /// <param name="y1">End Y coordinate; negative values offset from bottom edge.</param>
   /// <param name="color">Line color.</param>
   ///
   void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, Color color)
   {
      _normalizeCoords(x0, y0);
      _normalizeCoords(x1, y1);
      display.drawLine(x0, y0, x1, y1, (uint16_t)color);
   }

   ///
   /// <summary>
   /// Draws a circle outline centered on the given point, supporting negative coordinates as offsets from the far edge.
   /// </summary>
   /// <param name="x">Center X coordinate; negative values offset from right edge.</param>
   /// <param name="y">Center Y coordinate; negative values offset from bottom edge.</param>
   /// <param name="radius">Circle radius, in pixels.</param>
   /// <param name="color">Circle color.</param>
   ///
   void drawCircle(int16_t x, int16_t y, int16_t radius, Color color)
   {
      _normalizeCoords(x, y);
      display.drawCircle(x, y, radius, (uint16_t)color);
   }

   ///
   /// <summary>
   /// Sets the cursor position, supporting negative coordinates as offsets from the far edge.
   /// </summary>
   /// <param name="x">X coordinate; negative values offset from right edge.</param>
   /// <param name="y">Y coordinate; negative values offset from bottom edge.</param>
   ///
   void setCursor(int16_t x, int16_t y)
   {
      _normalizeCoords(x, y);
      display.setCursor(x, y);
   }

   ///
   /// <summary>
   /// Sets the cursor X coordinate while preserving the Y coordinate.
   /// </summary>
   /// <param name="x">X coordinate; negative values offset from right edge.</param>
   ///
   void setCursorX(int16_t x)
   {
      setCursor(x, display.getCursorY());
   }

   ///
   /// <summary>
   /// Sets the cursor Y coordinate while preserving the X coordinate.
   /// </summary>
   /// <param name="y">Y coordinate; negative values offset from bottom edge.</param>
   ///
   void setCursorY(int16_t y)
   {
      setCursor(display.getCursorX(), y);
   }

   ///
   /// <summary>
   /// Moves the cursor by the specified offsets from its current position.
   /// </summary>
   /// <param name="deltaX">Horizontal offset in pixels.</param>
   /// <param name="deltaY">Vertical offset in pixels.</param>
   ///
   void moveCursor(int16_t deltaX, int16_t deltaY)
   {
      int16_t x = display.getCursorX() + deltaX;
      int16_t y = display.getCursorY() + deltaY;

      display.setCursor(x, y);
   }

   ///
   /// <summary>
   /// Moves the cursor horizontally by the specified offset.
   /// </summary>
   /// <param name="deltaX">Horizontal offset in pixels.</param>
   ///
   void moveCursorX(int16_t deltaX)
   {
      moveCursor(deltaX, 0);
   }

   ///
   /// <summary>
   /// Moves the cursor vertically by the specified offset.
   /// </summary>
   /// <param name="deltaY">Vertical offset in pixels.</param>
   ///
   void moveCursorY(int16_t deltaY)
   {
      moveCursor(0, deltaY);
   }

   ///
   /// <summary>
   /// Gets the current cursor X coordinate.
   /// </summary>
   /// <returns>Current X coordinate in pixels.</returns>
   ///
   int16_t getCursorX()
   {
      return display.getCursorX();
   }

   ///
   /// <summary>
   /// Gets the current cursor Y coordinate.
   /// </summary>
   /// <returns>Current Y coordinate in pixels.</returns>
   ///
   int16_t getCursorY()
   {
      return display.getCursorY();
   }

   ///
   /// <summary>
   /// Gets the current cursor position as a point.
   /// </summary>
   /// <returns>Point containing current cursor X and Y coordinates.</returns>
   ///
   Point16 getCursor()
   {
      return Point16(display.getCursorX(), display.getCursorY());
   }

   ///
   /// <summary>
   /// Sets the cursor position from a Point16 coordinate.
   /// </summary>
   /// <param name="pt">Point containing X and Y coordinates.</param>
   ///
   void setCursor(Point16 pt)
   {
      setCursor(pt.x, pt.y);
   }

   //
   // ----------- const char* variants
   //
   void print(const char* str, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK) override
   {
      _print(str, textColor, backgroundColor);
   }
   void println(const char* str, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK) override
   {
      _println(str, textColor, backgroundColor);
   }
   ///
   /// <summary>
   /// Prints a newline
   /// </summary>
   ///
   void println()
   {
      ArduinoBase::println("");
      display.println();
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
   void printR(const char* str, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      uint16_t len = display.textWidth(str);
      setCursorX(-len);
      print(str, textColor, backgroundColor);
   }
   void printlnR(const char* str, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK) override
   {
      printR(str, textColor, backgroundColor);
      println();
   }
   void printInitHeader(const char* str, Color textColor = Color::HEADING) override
   {
      clearDisplay();
      setTextSize(headerTextSize());
      ArduinoBase::printInitHeader(str, textColor);
      moveCursorY(charH() / 4);
      _initDisplayPending = true;
   }

   void printInitLabel(const char* label, Color color = Color::LABEL) override;
   void printInitValue(const char* value, Color color = Color::VALUE) override;
   void printDisplayInitLabel(const char* label, Color color = Color::LABEL) override;
   void printDisplayInitValue(const char* value, Color color = Color::VALUE) override;
   void restoreInitDisplay(const char* header = "Initializing");

#ifdef ARDUINO_TOUCH_SUPPORTED
   size_t promptTouchOption(
      const char* title,
      const std::vector<String>& options,
      size_t defaultIndex = SIZE_MAX,
      uint32_t timeoutMs = 0);
   String promptTouchText(
      const char* title,
      const String& initialValue = "");
#endif

protected:
   InitializingDisplay* _initDisplay = nullptr;
   uint32_t _clearCount = 0;
   bool _initDisplayPending = false;

   void _printlnInitStatusDisplay(const char* str, Color textColor) override
   {
      printInitLabel(str, textColor);
      printInitValue("");
   }

public:
   ///
   /// <summary>
   /// Gets the text size used for headers printed by printInitHeader() (e.g. the
   /// "Initializing" header shown during setup).
   /// </summary>
   /// <returns>Text size to use for headers.</returns>
   ///
   virtual uint8_t headerTextSize()
   {
      return DEFAULT_HEADING_SIZE;
   }

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
   void print(const char* label, const char* value, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(label, Color::LABEL, backgroundColor);
      print(value, valueColor, backgroundColor);
   }
   void println(const char* label, const char* value, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(label, value, valueColor, backgroundColor);
      println();
   }
   void print(const char* label, const char* value, const Format& format, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(label, Color::LABEL, backgroundColor);
      print(value, format, valueColor, backgroundColor);
   }
   void println(const char* label, const char* value, const Format& format, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(label, value, format, valueColor, backgroundColor);
      println();
   }
   void print(const char* label, const char* value, Color labelColor, Color valueColor, Color backgroundColor)
   {
      print(label, labelColor, backgroundColor);
      print(value, valueColor, backgroundColor);
   }
   void println(const char* label, const char* value, Color labelColor, Color valueColor, Color backgroundColor)
   {
      print(label, value, labelColor, valueColor, backgroundColor);
      println();
   }
   void print(const char* label, const char* value, const Format& format, Color labelColor, Color valueColor, Color backgroundColor)
   {
      print(label, labelColor, backgroundColor);
      print(value, format, valueColor, backgroundColor);
   }
   void println(const char* label, const char* value, const Format& format, Color labelColor, Color valueColor, Color backgroundColor)
   {
      print(label, value, format, labelColor, valueColor, backgroundColor);
      println();
   }
   void printR(const char* label, const char* value, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      std::string rowText = std::string(label) + value;
      uint16_t len = display.textWidth(rowText.c_str());
      setCursorX(-len);
      print(label, Color::LABEL, backgroundColor);
      print(value, valueColor, backgroundColor);
   }
   void printlnR(const char* label, const char* value, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      printR(label, value, valueColor, backgroundColor);
      println();
   }
   void printR(const char* label, const char* value, Color labelColor, Color valueColor, Color backgroundColor)
   {
      std::string rowText = std::string(label) + value;
      uint16_t len = display.textWidth(rowText.c_str());
      setCursorX(-len);
      print(label, labelColor, backgroundColor);
      print(value, valueColor, backgroundColor);
   }
   void printlnR(const char* label, const char* value, Color labelColor, Color valueColor, Color backgroundColor)
   {
      printR(label, value, labelColor, valueColor, backgroundColor);
      println();
   }

   //
   // ----------- String variants
   //
   void print(const String& str, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(const String& str, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      _println(str.c_str(), textColor, backgroundColor);
   }
   void printR(const String& str, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      printR(str.c_str(), textColor, backgroundColor);
   }
   void printlnR(const String& str, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      printlnR(str.c_str(), textColor, backgroundColor);
   }
   void print(const String& str, const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      std::string s = format.toString(str);
      _print(s.c_str(), textColor, backgroundColor);
   }
   void println(const String& str, const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      print(str, format, textColor, backgroundColor);
      println();
   }
   void print(const char* label, const String& value, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(label, Color::LABEL, backgroundColor);
      print(value, valueColor, backgroundColor);
   }
   void println(const char* label, const String& value, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(label, value, valueColor, backgroundColor);
      println();
   }
   void print(const char* label, const String& value, const Format& format, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(label, Color::LABEL, backgroundColor);
      print(value, format, valueColor, backgroundColor);
   }
   void println(const char* label, const String& value, const Format& format, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(label, value, format, valueColor, backgroundColor);
      println();
   }
   void print(const char* label, const String& value, Color labelColor, Color valueColor, Color backgroundColor)
   {
      print(label, labelColor, backgroundColor);
      print(value, valueColor, backgroundColor);
   }
   void println(const char* label, const String& value, Color labelColor, Color valueColor, Color backgroundColor)
   {
      print(label, value, labelColor, valueColor, backgroundColor);
      println();
   }
   void print(const char* label, const String& value, const Format& format, Color labelColor, Color valueColor, Color backgroundColor)
   {
      print(label, labelColor, backgroundColor);
      print(value, format, valueColor, backgroundColor);
   }
   void println(const char* label, const String& value, const Format& format, Color labelColor, Color valueColor, Color backgroundColor)
   {
      print(label, value, format, labelColor, valueColor, backgroundColor);
      println();
   }

   //
   // ----------- std::string variants
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
   void printR(const std::string& str, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      printR(str.c_str(), textColor, backgroundColor);
   }
   void printlnR(const std::string& str, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      printlnR(str.c_str(), textColor, backgroundColor);
   }
   void printC(const std::string& str, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      printC(str.c_str(), textColor, backgroundColor);
   }
   void printlnC(const std::string& str, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      printlnC(str.c_str(), textColor, backgroundColor);
   }
   void print(const char* label, const std::string& value, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(label, Color::LABEL, backgroundColor);
      print(value, valueColor, backgroundColor);
   }
   void println(const char* label, const std::string& value, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(label, value, valueColor, backgroundColor);
      println();
   }
   void print(const char* label, const std::string& value, const Format& format, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(label, Color::LABEL, backgroundColor);
      print(value, format, valueColor, backgroundColor);
   }
   void println(const char* label, const std::string& value, const Format& format, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(label, value, format, valueColor, backgroundColor);
      println();
   }
   void print(const char* label, const std::string& value, Color labelColor, Color valueColor, Color backgroundColor)
   {
      print(label, labelColor, backgroundColor);
      print(value, valueColor, backgroundColor);
   }
   void println(const char* label, const std::string& value, Color labelColor, Color valueColor, Color backgroundColor)
   {
      print(label, value, labelColor, valueColor, backgroundColor);
      println();
   }
   void print(const char* label, const std::string& value, const Format& format, Color labelColor, Color valueColor, Color backgroundColor)
   {
      print(label, labelColor, backgroundColor);
      print(value, format, valueColor, backgroundColor);
   }
   void println(const char* label, const std::string& value, const Format& format, Color labelColor, Color valueColor, Color backgroundColor)
   {
      print(label, value, format, labelColor, valueColor, backgroundColor);
      println();
   }
   void printR(const char* label, const std::string& value, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      printR(label, value.c_str(), valueColor, backgroundColor);
   }
   void printlnR(const char* label, const std::string& value, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      printR(label, value, valueColor, backgroundColor);
      println();
   }
   void printR(const char* label, const std::string& value, Color labelColor, Color valueColor, Color backgroundColor)
   {
      printR(label, value.c_str(), labelColor, valueColor, backgroundColor);
   }
   void printlnR(const char* label, const std::string& value, Color labelColor, Color valueColor, Color backgroundColor)
   {
      printR(label, value, labelColor, valueColor, backgroundColor);
      println();
   }

   //
   // ----------- float variants
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
   void print(float value, unsigned int precision, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      String str(value, precision);
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(float value, unsigned int precision, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
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
      printR(str, textColor, backgroundColor);
   }
   void printlnR(float value, const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      printlnR(str, textColor, backgroundColor);
   }
   void printC(float value, const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      printC(str, textColor, backgroundColor);
   }
   void printlnC(float value, const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      printlnC(str, textColor, backgroundColor);
   }
   void print(const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toNoValueString();
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      print(format, textColor, backgroundColor);
      println();
   }
   void printR(const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toNoValueString();
      printR(str, textColor, backgroundColor);
   }
   void printlnR(const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toNoValueString();
      printlnR(str, textColor, backgroundColor);
   }
   void printC(const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toNoValueString();
      printC(str, textColor, backgroundColor);
   }
   void printlnC(const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toNoValueString();
      printlnC(str, textColor, backgroundColor);
   }
   void printD(double value, const Format& format, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      _printD(str, format, textColor, backgroundColor);
   }
   void printlnD(double value, const Format& format, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      printD(value, format, textColor, backgroundColor);
      println();
   }
   void printD(const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toNoValueString();
      _printD(str, format, textColor, backgroundColor);
   }
   void printlnD(const Format& format, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      printD(format, textColor, backgroundColor);
      println();
   }
   void print(const char* label, float value, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(label, Color::LABEL, backgroundColor);
      print(value, valueColor, backgroundColor);
   }
   void println(const char* label, float value, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(label, value, valueColor, backgroundColor);
      println();
   }
   void print(const char* label, float value, const Format& format, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(label, Color::LABEL, backgroundColor);
      print(value, format, valueColor, backgroundColor);
   }
   void println(const char* label, float value, const Format& format, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(label, value, format, valueColor, backgroundColor);
      println();
   }
   void printR(const char* label, float value, const Format& format, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      std::string valueText = format.toString(value);
      std::string rowText = std::string(label) + valueText;
      uint16_t len = display.textWidth(rowText.c_str());
      setCursorX(-len);
      print(label, Color::LABEL, backgroundColor);
      print(value, format, valueColor, backgroundColor);
   }
   void printlnR(const char* label, float value, const Format& format, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      printR(label, value, format, valueColor, backgroundColor);
      println();
   }
   void print(const char* label, float value, Color labelColor, Color valueColor, Color backgroundColor)
   {
      print(label, labelColor, backgroundColor);
      print(value, valueColor, backgroundColor);
   }
   void println(const char* label, float value, Color labelColor, Color valueColor, Color backgroundColor)
   {
      print(label, value, labelColor, valueColor, backgroundColor);
      println();
   }
   void print(const char* label, float value, const Format& format, Color labelColor, Color valueColor, Color backgroundColor)
   {
      print(label, labelColor, backgroundColor);
      print(value, format, valueColor, backgroundColor);
   }
   void println(const char* label, float value, const Format& format, Color labelColor, Color valueColor, Color backgroundColor)
   {
      print(label, value, format, labelColor, valueColor, backgroundColor);
      println();
   }
   void printR(const char* label, float value, const Format& format, Color labelColor, Color valueColor, Color backgroundColor)
   {
      std::string valueText = format.toString(value);
      std::string rowText = std::string(label) + valueText;
      uint16_t len = display.textWidth(rowText.c_str());
      setCursorX(-len);
      print(label, labelColor, backgroundColor);
      print(value, format, valueColor, backgroundColor);
   }
   void printlnR(const char* label, float value, const Format& format, Color labelColor, Color valueColor, Color backgroundColor)
   {
      printR(label, value, format, labelColor, valueColor, backgroundColor);
      println();
   }

   //
   // ----------- double variants
   //
   void print(double value, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      String str(value);
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(double value, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      String str(value);
      _println(str.c_str(), textColor, backgroundColor);
   }
   void print(double value, unsigned int precision, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      String str(value, precision);
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(double value, unsigned int precision, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      String str(value, precision);
      _println(str.c_str(), textColor, backgroundColor);
   }
   void print(double value, const Format& format, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(double value, const Format& format, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(value, format, textColor, backgroundColor);
      println();
   }
   void printR(double value, const Format& format, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      printR(str, textColor, backgroundColor);
   }
   void printlnR(double value, const Format& format, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      printlnR(str, textColor, backgroundColor);
   }
   void printC(double value, const Format& format, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      printC(str, textColor, backgroundColor);
   }
   void printlnC(double value, const Format& format, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      printlnC(str, textColor, backgroundColor);
   }
   void print(const char* label, double value, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(label, Color::LABEL, backgroundColor);
      print(value, valueColor, backgroundColor);
   }
   void println(const char* label, double value, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(label, value, valueColor, backgroundColor);
      println();
   }
   void print(const char* label, double value, const Format& format, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(label, Color::LABEL, backgroundColor);
      print(value, format, valueColor, backgroundColor);
   }
   void println(const char* label, double value, const Format& format, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(label, value, format, valueColor, backgroundColor);
      println();
   }
   void printR(const char* label, double value, const Format& format, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      std::string valueText = format.toString(value);
      std::string rowText = std::string(label) + valueText;
      uint16_t len = display.textWidth(rowText.c_str());
      setCursorX(-len);
      print(label, Color::LABEL, backgroundColor);
      print(value, format, valueColor, backgroundColor);
   }
   void printlnR(const char* label, double value, const Format& format, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      printR(label, value, format, valueColor, backgroundColor);
      println();
   }
   void print(const char* label, double value, Color labelColor, Color valueColor, Color backgroundColor)
   {
      print(label, labelColor, backgroundColor);
      print(value, valueColor, backgroundColor);
   }
   void println(const char* label, double value, Color labelColor, Color valueColor, Color backgroundColor)
   {
      print(label, value, labelColor, valueColor, backgroundColor);
      println();
   }
   void print(const char* label, double value, const Format& format, Color labelColor, Color valueColor, Color backgroundColor)
   {
      print(label, labelColor, backgroundColor);
      print(value, format, valueColor, backgroundColor);
   }
   void println(const char* label, double value, const Format& format, Color labelColor, Color valueColor, Color backgroundColor)
   {
      print(label, value, format, labelColor, valueColor, backgroundColor);
      println();
   }
   void printR(const char* label, double value, const Format& format, Color labelColor, Color valueColor, Color backgroundColor)
   {
      std::string valueText = format.toString(value);
      std::string rowText = std::string(label) + valueText;
      uint16_t len = display.textWidth(rowText.c_str());
      setCursorX(-len);
      print(label, labelColor, backgroundColor);
      print(value, format, valueColor, backgroundColor);
   }
   void printlnR(const char* label, double value, const Format& format, Color labelColor, Color valueColor, Color backgroundColor)
   {
      printR(label, value, format, labelColor, valueColor, backgroundColor);
      println();
   }

   //
   // ----------- uint8_t variants
   //
   void print(uint8_t value, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      String str(value);
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(uint8_t value, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      String str(value);
      _println(str.c_str(), textColor, backgroundColor);
   }
   void print(uint8_t value, uint8_t base, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      String str(value, base);
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(uint8_t value, uint8_t base, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      String str(value, base);
      _println(str.c_str(), textColor, backgroundColor);
   }
   void print(uint8_t value, const Format& format, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(uint8_t value, const Format& format, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(value, format, textColor, backgroundColor);
      println();
   }
   void printR(uint8_t value, const Format& format, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      printR(str, textColor, backgroundColor);
   }
   void printlnR(uint8_t value, const Format& format, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      printlnR(str, textColor, backgroundColor);
   }
   void print(const char* label, uint8_t value, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(label, Color::LABEL, backgroundColor);
      print(value, valueColor, backgroundColor);
   }
   void println(const char* label, uint8_t value, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(label, value, valueColor, backgroundColor);
      println();
   }
   void print(const char* label, uint8_t value, const Format& format, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(label, Color::LABEL, backgroundColor);
      print(value, format, valueColor, backgroundColor);
   }
   void println(const char* label, uint8_t value, const Format& format, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(label, value, format, valueColor, backgroundColor);
      println();
   }
   void print(const char* label, uint8_t value, Color labelColor, Color valueColor, Color backgroundColor)
   {
      print(label, labelColor, backgroundColor);
      print(value, valueColor, backgroundColor);
   }
   void println(const char* label, uint8_t value, Color labelColor, Color valueColor, Color backgroundColor)
   {
      print(label, value, labelColor, valueColor, backgroundColor);
      println();
   }
   void print(const char* label, uint8_t value, const Format& format, Color labelColor, Color valueColor, Color backgroundColor)
   {
      print(label, labelColor, backgroundColor);
      print(value, format, valueColor, backgroundColor);
   }
   void println(const char* label, uint8_t value, const Format& format, Color labelColor, Color valueColor, Color backgroundColor)
   {
      print(label, value, format, labelColor, valueColor, backgroundColor);
      println();
   }

   //
   // ----------- uint16_t variants
   //
   void print(uint16_t value, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      String str(value);
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(uint16_t value, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      String str(value);
      _println(str.c_str(), textColor, backgroundColor);
   }
   void print(uint16_t value, uint8_t base, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      String str(value, base);
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(uint16_t value, uint8_t base, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      String str(value, base);
      _println(str.c_str(), textColor, backgroundColor);
   }
   void print(uint16_t value, const Format& format, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(uint16_t value, const Format& format, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(value, format, textColor, backgroundColor);
      println();
   }
   void printR(uint16_t value, const Format& format, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      printR(str, textColor, backgroundColor);
   }
   void printlnR(uint16_t value, const Format& format, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      printlnR(str, textColor, backgroundColor);
   }
   void print(const char* label, uint16_t value, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(label, Color::LABEL, backgroundColor);
      print(value, valueColor, backgroundColor);
   }
   void println(const char* label, uint16_t value, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(label, value, valueColor, backgroundColor);
      println();
   }
   void print(const char* label, uint16_t value, const Format& format, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(label, Color::LABEL, backgroundColor);
      print(value, format, valueColor, backgroundColor);
   }
   void println(const char* label, uint16_t value, const Format& format, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(label, value, format, valueColor, backgroundColor);
      println();
   }
   void print(const char* label, uint16_t value, Color labelColor, Color valueColor, Color backgroundColor)
   {
      print(label, labelColor, backgroundColor);
      print(value, valueColor, backgroundColor);
   }
   void println(const char* label, uint16_t value, Color labelColor, Color valueColor, Color backgroundColor)
   {
      print(label, value, labelColor, valueColor, backgroundColor);
      println();
   }
   void print(const char* label, uint16_t value, const Format& format, Color labelColor, Color valueColor, Color backgroundColor)
   {
      print(label, labelColor, backgroundColor);
      print(value, format, valueColor, backgroundColor);
   }
   void println(const char* label, uint16_t value, const Format& format, Color labelColor, Color valueColor, Color backgroundColor)
   {
      print(label, value, format, labelColor, valueColor, backgroundColor);
      println();
   }
   void printR(const char* label, uint16_t value, const Format& format, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      std::string valueText = format.toString(value);
      std::string rowText = std::string(label) + valueText;
      uint16_t len = display.textWidth(rowText.c_str());
      setCursorX(-len);
      print(label, Color::LABEL, backgroundColor);
      print(value, format, valueColor, backgroundColor);
   }
   void printlnR(const char* label, uint16_t value, const Format& format, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      printR(label, value, format, valueColor, backgroundColor);
      println();
   }
   void printR(const char* label, uint16_t value, const Format& format, Color labelColor, Color valueColor, Color backgroundColor)
   {
      std::string valueText = format.toString(value);
      std::string rowText = std::string(label) + valueText;
      uint16_t len = display.textWidth(rowText.c_str());
      setCursorX(-len);
      print(label, labelColor, backgroundColor);
      print(value, format, valueColor, backgroundColor);
   }
   void printlnR(const char* label, uint16_t value, const Format& format, Color labelColor, Color valueColor, Color backgroundColor)
   {
      printR(label, value, format, labelColor, valueColor, backgroundColor);
      println();
   }
   void printR(const char* label, uint16_t value, const Format& format, Color labelColor, Color valueColor, Color labelBackgroundColor, Color valueBackgroundColor)
   {
      std::string valueText = format.toString(value);
      std::string rowText = std::string(label) + valueText;
      uint16_t len = display.textWidth(rowText.c_str());
      setCursorX(-len);
      print(label, labelColor, labelBackgroundColor);
      print(value, format, valueColor, valueBackgroundColor);
   }
   void printlnR(const char* label, uint16_t value, const Format& format, Color labelColor, Color valueColor, Color labelBackgroundColor, Color valueBackgroundColor)
   {
      printR(label, value, format, labelColor, valueColor, labelBackgroundColor, valueBackgroundColor);
      println();
   }

   //
   // ----------- int variants
   //
   void print(int value, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      String str(value);
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(int value, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      String str(value);
      _println(str.c_str(), textColor, backgroundColor);
   }
   void print(int value, uint8_t base, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      String str(value, base);
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(int value, uint8_t base, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      String str(value, base);
      _println(str.c_str(), textColor, backgroundColor);
   }
   void print(int value, const Format& format, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(int value, const Format& format, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(value, format, textColor, backgroundColor);
      println();
   }
   void printR(int value, const Format& format, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      printR(str, textColor, backgroundColor);
   }
   void printlnR(int value, const Format& format, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      printlnR(str, textColor, backgroundColor);
   }
   void print(const char* label, int value, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(label, Color::LABEL, backgroundColor);
      print(value, valueColor, backgroundColor);
   }
   void println(const char* label, int value, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(label, value, valueColor, backgroundColor);
      println();
   }
   void print(const char* label, int value, const Format& format, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(label, Color::LABEL, backgroundColor);
      print(value, format, valueColor, backgroundColor);
   }
   void println(const char* label, int value, const Format& format, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(label, value, format, valueColor, backgroundColor);
      println();
   }
   void print(const char* label, int value, Color labelColor, Color valueColor, Color backgroundColor)
   {
      print(label, labelColor, backgroundColor);
      print(value, valueColor, backgroundColor);
   }
   void println(const char* label, int value, Color labelColor, Color valueColor, Color backgroundColor)
   {
      print(label, value, labelColor, valueColor, backgroundColor);
      println();
   }
   void print(const char* label, int value, const Format& format, Color labelColor, Color valueColor, Color backgroundColor)
   {
      print(label, labelColor, backgroundColor);
      print(value, format, valueColor, backgroundColor);
   }
   void println(const char* label, int value, const Format& format, Color labelColor, Color valueColor, Color backgroundColor)
   {
      print(label, value, format, labelColor, valueColor, backgroundColor);
      println();
   }

   //
   // ----------- long variants
   //
   void print(long value, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      String str(value);
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(long value, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      String str(value);
      _println(str.c_str(), textColor, backgroundColor);
   }
   void print(long value, uint8_t base, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      String str(value, base);
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(long value, uint8_t base, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      String str(value, base);
      _println(str.c_str(), textColor, backgroundColor);
   }
   void print(long value, const Format& format, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(long value, const Format& format, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(value, format, textColor, backgroundColor);
      println();
   }
   void printR(long value, const Format& format, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      printR(str, textColor, backgroundColor);
   }
   void printlnR(long value, const Format& format, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      printlnR(str, textColor, backgroundColor);
   }
   void print(const char* label, long value, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(label, Color::LABEL, backgroundColor);
      print(value, valueColor, backgroundColor);
   }
   void println(const char* label, long value, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(label, value, valueColor, backgroundColor);
      println();
   }
   void print(const char* label, long value, const Format& format, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(label, Color::LABEL, backgroundColor);
      print(value, format, valueColor, backgroundColor);
   }
   void println(const char* label, long value, const Format& format, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(label, value, format, valueColor, backgroundColor);
      println();
   }
   void print(const char* label, long value, Color labelColor, Color valueColor, Color backgroundColor)
   {
      print(label, labelColor, backgroundColor);
      print(value, valueColor, backgroundColor);
   }
   void println(const char* label, long value, Color labelColor, Color valueColor, Color backgroundColor)
   {
      print(label, value, labelColor, valueColor, backgroundColor);
      println();
   }
   void print(const char* label, long value, const Format& format, Color labelColor, Color valueColor, Color backgroundColor)
   {
      print(label, labelColor, backgroundColor);
      print(value, format, valueColor, backgroundColor);
   }
   void println(const char* label, long value, const Format& format, Color labelColor, Color valueColor, Color backgroundColor)
   {
      print(label, value, format, labelColor, valueColor, backgroundColor);
      println();
   }

   //
   // ----------- unsigned long variants
   //
   void print(unsigned long value, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      String str(value);
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(unsigned long value, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      String str(value);
      _println(str.c_str(), textColor, backgroundColor);
   }
   void print(unsigned long value, uint8_t base, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      String str(value, base);
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(unsigned long value, uint8_t base, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      String str(value, base);
      _println(str.c_str(), textColor, backgroundColor);
   }
   void print(unsigned long value, const Format& format, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      _print(str.c_str(), textColor, backgroundColor);
   }
   void println(unsigned long value, const Format& format, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(value, format, textColor, backgroundColor);
      println();
   }
   void printR(unsigned long value, const Format& format, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      printR(str, textColor, backgroundColor);
   }
   void printlnR(unsigned long value, const Format& format, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      std::string str = format.toString(value);
      printlnR(str, textColor, backgroundColor);
   }
   void print(const char* label, unsigned long value, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(label, Color::LABEL, backgroundColor);
      print(value, valueColor, backgroundColor);
   }
   void println(const char* label, unsigned long value, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(label, value, valueColor, backgroundColor);
      println();
   }
   void print(const char* label, unsigned long value, const Format& format, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(label, Color::LABEL, backgroundColor);
      print(value, format, valueColor, backgroundColor);
   }
   void println(const char* label, unsigned long value, const Format& format, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(label, value, format, valueColor, backgroundColor);
      println();
   }
   void print(const char* label, unsigned long value, Color labelColor, Color valueColor, Color backgroundColor)
   {
      print(label, labelColor, backgroundColor);
      print(value, valueColor, backgroundColor);
   }
   void println(const char* label, unsigned long value, Color labelColor, Color valueColor, Color backgroundColor)
   {
      print(label, value, labelColor, valueColor, backgroundColor);
      println();
   }
   void print(const char* label, unsigned long value, const Format& format, Color labelColor, Color valueColor, Color backgroundColor)
   {
      print(label, labelColor, backgroundColor);
      print(value, format, valueColor, backgroundColor);
   }
   void println(const char* label, unsigned long value, const Format& format, Color labelColor, Color valueColor, Color backgroundColor)
   {
      print(label, value, format, labelColor, valueColor, backgroundColor);
      println();
   }

   //
   // ----------- size_t variants
   //
   // size_t is forwarded to the unsigned long overloads to avoid ambiguity with the
   // other built-in integer types on platforms where size_t is a distinct type.
   void print(size_t value, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print((unsigned long)value, textColor, backgroundColor);
   }
   void println(size_t value, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      println((unsigned long)value, textColor, backgroundColor);
   }
   void print(size_t value, uint8_t base, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print((unsigned long)value, base, textColor, backgroundColor);
   }
   void println(size_t value, uint8_t base, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      println((unsigned long)value, base, textColor, backgroundColor);
   }
   void print(size_t value, const Format& format, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print((unsigned long)value, format, textColor, backgroundColor);
   }
   void println(size_t value, const Format& format, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      println((unsigned long)value, format, textColor, backgroundColor);
   }
   void printR(size_t value, const Format& format, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      printR((unsigned long)value, format, textColor, backgroundColor);
   }
   void printlnR(size_t value, const Format& format, Color textColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      printlnR((unsigned long)value, format, textColor, backgroundColor);
   }
   void print(const char* label, size_t value, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(label, (unsigned long)value, valueColor, backgroundColor);
   }
   void println(const char* label, size_t value, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      println(label, (unsigned long)value, valueColor, backgroundColor);
   }
   void print(const char* label, size_t value, const Format& format, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      print(label, (unsigned long)value, format, valueColor, backgroundColor);
   }
   void println(const char* label, size_t value, const Format& format, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      println(label, (unsigned long)value, format, valueColor, backgroundColor);
   }
   void print(const char* label, size_t value, Color labelColor, Color valueColor, Color backgroundColor)
   {
      print(label, (unsigned long)value, labelColor, valueColor, backgroundColor);
   }
   void println(const char* label, size_t value, Color labelColor, Color valueColor, Color backgroundColor)
   {
      println(label, (unsigned long)value, labelColor, valueColor, backgroundColor);
   }
   void print(const char* label, size_t value, const Format& format, Color labelColor, Color valueColor, Color backgroundColor)
   {
      print(label, (unsigned long)value, format, labelColor, valueColor, backgroundColor);
   }
   void println(const char* label, size_t value, const Format& format, Color labelColor, Color valueColor, Color backgroundColor)
   {
      println(label, (unsigned long)value, format, labelColor, valueColor, backgroundColor);
   }

   ///
   /// <summary>
   /// Enables periodic (or on-demand) OTA firmware update checks, showing download progress
   /// on this display, and immediately performs one check (installing an update if
   /// available). The firmware and version-check URLs are both derived from sketchName per
   /// convention: this sketch publishes to a GitHub release tagged with its own name. WiFi
   /// must already be connected before calling this.
   /// </summary>
   /// <param name="version">This sketch's own version string (e.g. "v1.0").</param>
   /// <param name="sketchName">This sketch's name (e.g. "Wind_Publisher"), used to derive its release URLs.</param>
   /// <param name="status">Optional status indicator set to FAILED if no OTA download partition is found.</param>
   /// <param name="onUpdateAvailable">Optional handler, notified with the newly detected version string just before it's installed.</param>
   ///
   void enableOTA(const char* version, const char* sketchName,
      IStatus* status = nullptr, OTAUpdateEventHandler* onUpdateAvailable = nullptr)
   {
      _ota = new OTAUpdater(version, this);
      _otaLoggingHandler.next = onUpdateAvailable;
      _ota->setHandler(&_otaLoggingHandler);
      _ota->setStatus(status);
   }
};

// OTAUpdater's methods that call ArduinoWithDisplay's own methods must be defined
// out-of-line, after this class is fully defined; see OTAUpdaterImpl.h.
#include "OTAUpdaterImpl.h"

// Scrolling initialization display used by printInitLabel()/printInitValue().
#include "InitializingDisplay.h"

inline void ArduinoWithDisplay::printInitLabel(const char* label, Color color)
{
   Util::setSetupStep(label);

   if (_initDisplay == nullptr)
   {
      _initDisplay = new InitializingDisplay(this);
      _initDisplayPending = true;
   }

   if (_initDisplayPending)
   {
      _initDisplay->begin();
      _initDisplayPending = false;
   }

   _initDisplay->printLabel(label, color);
}

inline void ArduinoWithDisplay::printInitValue(const char* value, Color color)
{
   ASSERT(_initDisplay != nullptr);

   Util::feedSetupWatchdog();
   _initDisplay->setValue(value, color);
}

inline void ArduinoWithDisplay::printDisplayInitLabel(const char* label, Color color)
{
   printInitLabel(label, color);
}

inline void ArduinoWithDisplay::printDisplayInitValue(const char* value, Color color)
{
   printInitValue(value, color);
}

inline void ArduinoWithDisplay::restoreInitDisplay(const char* header)
{
   clearDisplay();
   setTextSize(headerTextSize());
   ArduinoBase::printInitHeader(header, Color::HEADING);
   moveCursorY(charH() / 4);
   _initDisplayPending = false;
   if (_initDisplay != nullptr)
   {
      _initDisplay->redraw();
   }
}

#ifdef ARDUINO_TOUCH_SUPPORTED
inline size_t ArduinoWithDisplay::promptTouchOption(
   const char* title,
   const std::vector<String>& options,
   size_t defaultIndex,
   uint32_t timeoutMs)
{
   ASSERT(!options.empty());

   clearDisplay();
   uint8_t titleSize = headerTextSize();
   setTextSize(titleSize);
   setCursor(0, 0);
   _println(title, Color::HEADING, Color::BLACK);

   int16_t topY = getCursorY() + 4;
   int16_t availH = height() - topY - 4;
   size_t count = options.size();

   constexpr int16_t MARGIN_X = 8;
   constexpr int16_t GAP_Y = 6;
   int16_t buttonW = width() - 2 * MARGIN_X;
   int16_t maxButtonH = height() >= LARGE_DISPLAY_MIN_HEIGHT ? 52 : 36;
   int16_t totalGaps = (int16_t)(count > 1 ? (count - 1) * GAP_Y : 0);
   int16_t buttonH = (availH - totalGaps) / (int16_t)count;
   if (buttonH > maxButtonH)
   {
      buttonH = maxButtonH;
   }
   if (buttonH < charH(2) + 4)
   {
      buttonH = charH(2) + 4;
   }

   uint8_t btnTextSize = (height() >= LARGE_DISPLAY_MIN_HEIGHT && buttonH >= charH(3) + 10) ? 3 : 2;

   auto drawButton = [&](size_t idx, bool pressed)
   {
      int16_t by = topY + (int16_t)idx * (buttonH + GAP_Y);
      Color bg = pressed ? Color::LIGHTBLUE : (idx == defaultIndex ? Color::DARKGREEN : Color::BLUE);
      display.fillRoundRect(MARGIN_X, by, buttonW, buttonH, 6, (uint16_t)bg);
      display.drawRoundRect(MARGIN_X, by, buttonW, buttonH, 6, (uint16_t)Color::WHITE);

      setTextSize(btnTextSize);
      display.setTextColor((uint16_t)Color::WHITE, (uint16_t)bg);
      int16_t tw = display.textWidth(options[idx].c_str());
      int16_t tx = MARGIN_X + (buttonW - tw) / 2;
      if (tx < MARGIN_X + 4)
      {
         tx = MARGIN_X + 4;
      }
      int16_t ty = by + (buttonH - charH(btnTextSize)) / 2;
      display.setCursor(tx, ty);
      display.print(options[idx].c_str());
   };

   for (size_t i = 0; i < count; i++)
   {
      drawButton(i, false);
   }

   lgfx::touch_point_t tp;
   while (display.getTouch(&tp) > 0)
   {
      delay(10);
   }

   while (Serial.available())
   {
      Serial.read();
   }

   if (timeoutMs > 0 && defaultIndex < count)
   {
      Serial.print("Enter selection (1-");
      Serial.print(count);
      Serial.print("), or tap screen (defaults to ");
      Serial.print(defaultIndex + 1);
      Serial.print(" after ");
      Serial.print(timeoutMs / 1000);
      Serial.println("s): ");
   }
   else
   {
      Serial.print("Enter selection (1-");
      Serial.print(count);
      Serial.print(") or tap screen: ");
   }

   size_t pressedIdx = SIZE_MAX;
   uint32_t start = millis();
   uint32_t effectiveTimeoutMs = timeoutMs > 0 ? timeoutMs : (Util::REQUIRED_PROMPT_TIMEOUT_S * 1000UL);
   Util::setSetupStep(title);

   while (true)
   {
      Util::feedSetupWatchdog();

      if (millis() - start >= effectiveTimeoutMs)
      {
         if (defaultIndex < count)
         {
            Serial.println("No input - using default.");
            restoreInitDisplay();
            return defaultIndex;
         }

         Serial.println("No input - entering safe mode.");
         Util::reset(0.0f, Util::PROMPT_TIMEOUT_HALT_REASON);
      }

      bool touched = display.getTouch(&tp) > 0;
      if (touched)
      {
         start = millis();
         Util::feedSetupWatchdog();
         size_t hitIdx = SIZE_MAX;
         for (size_t i = 0; i < count; i++)
         {
            int16_t by = topY + (int16_t)i * (buttonH + GAP_Y);
            if (tp.x >= MARGIN_X && tp.x < MARGIN_X + buttonW && tp.y >= by && tp.y < by + buttonH)
            {
               hitIdx = i;
               break;
            }
         }

         if (hitIdx != pressedIdx)
         {
            if (pressedIdx != SIZE_MAX)
            {
               drawButton(pressedIdx, false);
            }
            pressedIdx = hitIdx;
            if (pressedIdx != SIZE_MAX)
            {
               drawButton(pressedIdx, true);
            }
         }
      }
      else if (pressedIdx != SIZE_MAX)
      {
         size_t chosen = pressedIdx;
         Serial.println(chosen + 1);
         restoreInitDisplay();
         return chosen;
      }

      if (Serial.available())
      {
         start = millis();
         Util::feedSetupWatchdog();
         String input = Serial.readStringUntil('\n');
         input.trim();
         Serial.println(input);

         if (input.length() == 0 && defaultIndex < count)
         {
            restoreInitDisplay();
            return defaultIndex;
         }

         bool isNumeric = input.length() > 0;
         for (size_t i = 0; i < input.length(); i++)
         {
            if (!isDigit(input[i]))
            {
               isNumeric = false;
               break;
            }
         }

         if (isNumeric)
         {
            long value = input.toInt();
            if (value >= 1 && value <= (long)count)
            {
               restoreInitDisplay();
               return (size_t)(value - 1);
            }
         }

         Serial.print("Invalid selection, enter 1-");
         Serial.print(count);
         Serial.print(": ");
      }

      delay(10);
   }
}

///
/// <summary>
/// Prompts the user to enter a non-empty text string via an on-screen touch QWERTY
/// keyboard (or over Serial if connected). Supports Shift/Caps toggle, Space,
/// Backspace, and OK, and optionally pre-populates the input box with an initial value.
/// Restores the initialization display before returning.
/// </summary>
/// <param name="title">Heading text drawn at the top of the screen.</param>
/// <param name="initialValue">Optional initial text pre-populated into the input box.</param>
/// <returns>The trimmed non-empty string entered by the user.</returns>
///
inline String ArduinoWithDisplay::promptTouchText(
   const char* title,
   const String& initialValue)
{
   enum class KeyAction : uint8_t
   {
      CHAR,
      SHIFT,
      BACKSPACE,
      SPACE,
      OK,
   };

   struct KeyDef
   {
      int16_t x;
      int16_t y;
      int16_t w;
      int16_t h;
      KeyAction action;
      char ch;
   };

   String value = initialValue;
   value.trim();

   // Start shifted when empty so the first letter is capitalized by default.
   bool shifted = (value.length() == 0);
   bool capsLock = false;

   clearDisplay();
   uint8_t titleSize = headerTextSize();
   setTextSize(titleSize);
   setCursor(0, 0);
   _println(title, Color::HEADING, Color::BLACK);

   constexpr int16_t MARGIN_X = 6;
   constexpr int16_t GAP_X = 4;
   constexpr int16_t GAP_Y = 4;
   constexpr size_t MAX_KEYS = 30;

   uint8_t keyTextSize = (height() >= LARGE_DISPLAY_MIN_HEIGHT) ? 3 : 2;
   int16_t boxY = getCursorY() + 2;
   int16_t boxW = width() - 2 * MARGIN_X;
   int16_t boxH = charH(keyTextSize) + 10;

   int16_t kbTopY = boxY + boxH + 6;
   int16_t availH = height() - kbTopY - 4;
   int16_t keyH = (availH - 3 * GAP_Y) / 4;
   int16_t maxKeyH = (height() >= LARGE_DISPLAY_MIN_HEIGHT) ? 54 : 38;
   if (keyH > maxKeyH)
   {
      keyH = maxKeyH;
   }
   if (keyH < charH(2) + 4)
   {
      keyH = charH(2) + 4;
   }
   if (keyH < charH(keyTextSize) + 4)
   {
      keyTextSize = 2;
   }

   int16_t availW = width() - 2 * MARGIN_X;
   int16_t stdKeyW = (availW - 9 * GAP_X) / 10;
   int16_t row0W = 10 * stdKeyW + 9 * GAP_X;
   int16_t row0StartX = (width() - row0W) / 2;

   int16_t row1W = 9 * stdKeyW + 8 * GAP_X;
   int16_t row1StartX = (width() - row1W) / 2;

   int16_t row2LettersW = 7 * stdKeyW + 6 * GAP_X;
   int16_t sideKeyW = (row0W - row2LettersW - 2 * GAP_X) / 2;
   int16_t row2LettersStartX = row0StartX + sideKeyW + GAP_X;

   int16_t okKeyW = (row0W - 5 * stdKeyW - GAP_X) / 2;
   int16_t spaceKeyW = row0W - okKeyW - GAP_X;

   KeyDef keys[MAX_KEYS];
   size_t keyCount = 0;

   static constexpr const char* ROW0 = "QWERTYUIOP";
   static constexpr const char* ROW1 = "ASDFGHJKL";
   static constexpr const char* ROW2 = "ZXCVBNM";

   int16_t y0 = kbTopY;
   for (size_t i = 0; i < 10; i++)
   {
      keys[keyCount++] = { (int16_t)(row0StartX + (int16_t)i * (stdKeyW + GAP_X)), y0, stdKeyW, keyH, KeyAction::CHAR, ROW0[i] };
   }

   int16_t y1 = y0 + keyH + GAP_Y;
   for (size_t i = 0; i < 9; i++)
   {
      keys[keyCount++] = { (int16_t)(row1StartX + (int16_t)i * (stdKeyW + GAP_X)), y1, stdKeyW, keyH, KeyAction::CHAR, ROW1[i] };
   }

   int16_t y2 = y1 + keyH + GAP_Y;
   keys[keyCount++] = { row0StartX, y2, sideKeyW, keyH, KeyAction::SHIFT, 0 };
   for (size_t i = 0; i < 7; i++)
   {
      keys[keyCount++] = { (int16_t)(row2LettersStartX + (int16_t)i * (stdKeyW + GAP_X)), y2, stdKeyW, keyH, KeyAction::CHAR, ROW2[i] };
   }
   int16_t backX = row2LettersStartX + 7 * stdKeyW + 7 * GAP_X;
   int16_t backW = row0StartX + row0W - backX;
   keys[keyCount++] = { backX, y2, backW, keyH, KeyAction::BACKSPACE, 0 };

   int16_t y3 = y2 + keyH + GAP_Y;
   keys[keyCount++] = { row0StartX, y3, spaceKeyW, keyH, KeyAction::SPACE, ' ' };
   keys[keyCount++] = { (int16_t)(row0StartX + spaceKeyW + GAP_X), y3, okKeyW, keyH, KeyAction::OK, 0 };

   auto hasNonSpace = [&]() -> bool
   {
      for (size_t i = 0; i < value.length(); i++)
      {
         if (value[i] != ' ')
         {
            return true;
         }
      }
      return false;
   };

   auto drawTextBox = [&]()
   {
      display.fillRoundRect(MARGIN_X, boxY, boxW, boxH, 4, (uint16_t)Color::BLACK);
      display.drawRoundRect(MARGIN_X, boxY, boxW, boxH, 4, (uint16_t)Color::WHITE);

      setTextSize(keyTextSize);
      display.setTextColor((uint16_t)Color::VALUE, (uint16_t)Color::BLACK);

      String displayStr = value + "_";
      int16_t maxTextW = boxW - 12;
      while (displayStr.length() > 1 && display.textWidth(displayStr.c_str()) > maxTextW)
      {
         displayStr.remove(0, 1);
      }

      int16_t ty = boxY + (boxH - charH(keyTextSize)) / 2;
      display.setCursor(MARGIN_X + 6, ty);
      display.print(displayStr.c_str());
   };

   auto drawKey = [&](size_t idx, bool pressed)
   {
      const KeyDef& k = keys[idx];
      Color bg = Color::BLUE;
      Color fg = Color::WHITE;

      if (pressed)
      {
         bg = Color::LIGHTBLUE;
      }
      else if (k.action == KeyAction::OK)
      {
         bg = hasNonSpace() ? Color::DARKGREEN : Color::DARKSLATEGRAY;
         fg = hasNonSpace() ? Color::WHITE : Color::GRAY;
      }
      else if (k.action == KeyAction::SHIFT)
      {
         bg = (shifted || capsLock) ? Color::DARKCYAN : Color::DARKSLATEGRAY;
      }
      else if (k.action == KeyAction::BACKSPACE)
      {
         bg = Color::DARKSLATEGRAY;
      }

      display.fillRoundRect(k.x, k.y, k.w, k.h, 5, (uint16_t)bg);
      display.drawRoundRect(k.x, k.y, k.w, k.h, 5, (uint16_t)Color::WHITE);

      char labelBuf[8] = {};
      if (k.action == KeyAction::CHAR)
      {
         bool upper = shifted || capsLock;
         labelBuf[0] = upper ? k.ch : (char)(k.ch - 'A' + 'a');
         labelBuf[1] = '\0';
      }
      else if (k.action == KeyAction::SHIFT)
      {
         strcpy(labelBuf, capsLock ? "CAPS" : (shifted ? "SHFT" : "shft"));
      }
      else if (k.action == KeyAction::BACKSPACE)
      {
         strcpy(labelBuf, "<-");
      }
      else if (k.action == KeyAction::SPACE)
      {
         strcpy(labelBuf, "Space");
      }
      else if (k.action == KeyAction::OK)
      {
         strcpy(labelBuf, "OK");
      }

      uint8_t labelSize = keyTextSize;
      setTextSize(labelSize);
      if (labelSize > 2 && display.textWidth(labelBuf) > k.w - 4)
      {
         labelSize = 2;
         setTextSize(labelSize);
      }

      display.setTextColor((uint16_t)fg, (uint16_t)bg);
      int16_t tw = display.textWidth(labelBuf);
      int16_t tx = k.x + (k.w - tw) / 2;
      int16_t ty = k.y + (k.h - charH(labelSize)) / 2;
      display.setCursor(tx, ty);
      display.print(labelBuf);
   };

   auto drawAllKeys = [&]()
   {
      for (size_t i = 0; i < keyCount; i++)
      {
         drawKey(i, false);
      }
   };

   drawTextBox();
   drawAllKeys();

   lgfx::touch_point_t tp;
   while (display.getTouch(&tp) > 0)
   {
      delay(10);
   }

   while (Serial.available())
   {
      Serial.read();
   }

   if (value.length() > 0)
   {
      Serial.print("Enter ");
      Serial.print(title);
      Serial.print(" (blank to keep \"");
      Serial.print(value);
      Serial.print("\") or use touch keyboard: ");
   }
   else
   {
      Serial.print("Enter ");
      Serial.print(title);
      Serial.print(" or use touch keyboard: ");
   }

   size_t pressedIdx = SIZE_MAX;
   uint32_t start = millis();
   constexpr uint32_t TIMEOUT_MS = Util::REQUIRED_PROMPT_TIMEOUT_S * 1000UL;
   Util::setSetupStep(title);

   while (true)
   {
      Util::feedSetupWatchdog();

      if (millis() - start >= TIMEOUT_MS)
      {
         Serial.println("No input - entering safe mode.");
         Util::reset(0.0f, Util::PROMPT_TIMEOUT_HALT_REASON);
      }

      bool touched = display.getTouch(&tp) > 0;
      if (touched)
      {
         start = millis();
         Util::feedSetupWatchdog();
         size_t hitIdx = SIZE_MAX;
         for (size_t i = 0; i < keyCount; i++)
         {
            const KeyDef& k = keys[i];
            if (tp.x >= k.x && tp.x < k.x + k.w && tp.y >= k.y && tp.y < k.y + k.h)
            {
               hitIdx = i;
               break;
            }
         }

         if (hitIdx != pressedIdx)
         {
            if (pressedIdx != SIZE_MAX)
            {
               drawKey(pressedIdx, false);
            }
            pressedIdx = hitIdx;
            if (pressedIdx != SIZE_MAX)
            {
               drawKey(pressedIdx, true);
            }
         }
      }
      else if (pressedIdx != SIZE_MAX)
      {
         size_t idx = pressedIdx;
         pressedIdx = SIZE_MAX;
         const KeyDef& k = keys[idx];
         bool prevValid = hasNonSpace();

         if (k.action == KeyAction::CHAR)
         {
            bool upper = shifted || capsLock;
            char c = upper ? k.ch : (char)(k.ch - 'A' + 'a');
            value += c;
            drawTextBox();

            if (shifted && !capsLock)
            {
               shifted = false;
               drawAllKeys();
            }
            else
            {
               drawKey(idx, false);
               if (hasNonSpace() != prevValid)
               {
                  drawKey(keyCount - 1, false);
               }
            }
         }
         else if (k.action == KeyAction::SPACE)
         {
            if (value.length() > 0 && value[value.length() - 1] != ' ')
            {
               value += ' ';
               drawTextBox();
            }
            drawKey(idx, false);
            if (!capsLock && !shifted)
            {
               shifted = true;
               drawAllKeys();
            }
         }
         else if (k.action == KeyAction::BACKSPACE)
         {
            if (value.length() > 0)
            {
               value.remove(value.length() - 1);
               drawTextBox();
            }
            bool autoShift = (value.length() == 0 || value[value.length() - 1] == ' ');
            if (!capsLock && shifted != autoShift)
            {
               shifted = autoShift;
               drawAllKeys();
            }
            else
            {
               drawKey(idx, false);
               if (hasNonSpace() != prevValid)
               {
                  drawKey(keyCount - 1, false);
               }
            }
         }
         else if (k.action == KeyAction::SHIFT)
         {
            if (!shifted && !capsLock)
            {
               shifted = true;
            }
            else if (shifted && !capsLock)
            {
               capsLock = true;
               shifted = false;
            }
            else
            {
               capsLock = false;
               shifted = false;
            }
            drawAllKeys();
         }
         else if (k.action == KeyAction::OK)
         {
            String trimmed = value;
            trimmed.trim();
            if (trimmed.length() > 0)
            {
               Serial.println(trimmed);
               restoreInitDisplay();
               return trimmed;
            }
                  drawKey(idx, false);
               }
            }

            if (Serial.available())
            {
               start = millis();
               Util::feedSetupWatchdog();
               String input = Serial.readStringUntil('\n');
               input.trim();
               Serial.println(input);

               if (input.length() == 0 && value.length() > 0)
         {
            String trimmed = value;
            trimmed.trim();
            if (trimmed.length() > 0)
            {
               restoreInitDisplay();
               return trimmed;
            }
         }
         else if (input.length() > 0)
         {
            restoreInitDisplay();
            return input;
         }

         Serial.print("Enter ");
         Serial.print(title);
         Serial.print(": ");
      }

      delay(10);
   }
}
#endif
