#pragma once

#include <array>
#include <string>

#ifndef ARDUINO_DISPLAY_SUPPORTED
#define ARDUINO_DISPLAY_SUPPORTED
#endif

#include "ArduinoBoardId.h"

// Memory-constrained boards with small displays (e.g. Adafruit Feather M0 and Feather ESP32-S3 TFT)
// default to TEXT_SIZES_CUSTOM so unused large VLW fonts (sizes 5-7, ~185 KB) are excluded from flash.
// Boards with 16 MB flash / larger displays default to TEXT_SIZES_ALL unless a sketch explicitly
// defines TEXT_SIZES_CUSTOM before including ArduinoBoard.h.
#if !defined(TEXT_SIZES_ALL) && !defined(TEXT_SIZES_CUSTOM)
#if defined(ADAFRUIT_FEATHER_M0) || defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S3_TFT)
#define TEXT_SIZES_CUSTOM
#else
#define TEXT_SIZES_ALL
#endif
#endif

#ifdef TEXT_SIZES_CUSTOM
#ifndef TEXT_SIZE_2
#define TEXT_SIZE_2
#endif
#ifndef TEXT_SIZE_3
#define TEXT_SIZE_3
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

   void _printlnWrapped(const char* text, int16_t x, int16_t maxWidth, Color textColor, Color backgroundColor)
   {
      if (text == nullptr)
      {
         return;
      }

      ArduinoBase::println(text, textColor, backgroundColor);
      display.setTextColor(static_cast<uint16_t>(textColor), static_cast<uint16_t>(backgroundColor));

      if (text[0] == '\0' || maxWidth <= 0)
      {
         setCursor(x, getCursorY());
         display.println(text);
         return;
      }

      const char* p = text;
      while (*p != '\0')
      {
         std::string line;
         while (*p != '\0' && *p != '\n')
         {
            while (*p == ' ' && line.empty())
            {
               p++;
            }
            if (*p == '\0' || *p == '\n')
            {
               break;
            }

            const char* tokenStart = p;
            while (*p != '\0' && *p != ' ' && *p != '\n')
            {
               p++;
            }

            std::string token(tokenStart, static_cast<size_t>(p - tokenStart));
            std::string candidate = line.empty() ? token : (line + " " + token);

            if (display.textWidth(candidate.c_str()) <= maxWidth)
            {
               line = candidate;
               while (*p == ' ')
               {
                  p++;
               }
            }
            else if (!line.empty())
            {
               p = tokenStart;
               break;
            }
            else
            {
               size_t fitLen = 1;
               while (fitLen < token.length())
               {
                  std::string sub = token.substr(0, fitLen + 1);
                  if (display.textWidth(sub.c_str()) > maxWidth)
                  {
                     break;
                  }
                  fitLen++;
               }
               line = token.substr(0, fitLen);
               p = tokenStart + fitLen;
               break;
            }
         }

         if (*p == '\n')
         {
            p++;
         }

         setCursor(x, getCursorY());
         display.println(line.c_str());
      }
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
   static constexpr uint32_t TAP_DEBOUNCE_MS = 200;

   uint8_t _textSize = 1;
   DisplayRotation _rotation = DisplayRotation::LANDSCAPE;
   bool _tapWasTouching = false;
   bool _tapHasSeenTouch = false;
   Timer _tapDebounceTimer{ TAP_DEBOUNCE_MS };

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

      display.setRotation((uint8_t)_rotation);
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
      _rotation = rotation;
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
      // LGFX uses setTextSize to set a scaling factor. We instead load a properly
      // sized font. Make sure to set the scaling factor back to 1
      display.setTextSize(1);

      size = constrain(size, 0, 7);

      // A null entry means this size was compiled out via TEXT_SIZES_CUSTOM/TEXT_SIZE_n
      // in the sketch; fall back to size 2 (which is always compiled in) rather than
      // crashing when loadFont() tries to read through nullptr.
      if ((mono ? RobotoMonoBold[size] : PROPORTIONAL_FONT(size)) == nullptr)
      {
         size = 2;
      }

      _textSize = size;

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

      // See setTextSize() for why this falls back to size 2 when a font size was compiled out.
      if ((mono ? RobotoMonoBold[size] : PROPORTIONAL_FONT(size)) == nullptr)
      {
         size = 2;
      }

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
   /// Renders text into a temporary off-screen sprite at the given font size and pushes it
   /// rotated around (centerX, centerY). Useful for vertical section headings or axis titles
   /// (e.g. angleDeg = 270.0f for bottom-to-top text).
   /// </summary>
   /// <param name="text">Null-terminated string to render.</param>
   /// <param name="centerX">Destination X coordinate of the rotated text's center.</param>
   /// <param name="centerY">Destination Y coordinate of the rotated text's center.</param>
   /// <param name="angleDeg">Clockwise rotation angle in degrees (e.g. 270.0f for bottom-to-top).</param>
   /// <param name="size">Font size index (1-7) used to render the text.</param>
   /// <param name="textColor">Foreground text color (default: Color::WHITE).</param>
   /// <param name="backgroundColor">Background fill color behind the glyphs (default: Color::BLACK).</param>
   ///
   void drawRotatedText(
      const char* text,
      float centerX,
      float centerY,
      float angleDeg,
      uint8_t size,
      Color textColor = Color::WHITE,
      Color backgroundColor = Color::BLACK)
   {
      ASSERT(text != nullptr);

      if (text[0] == '\0')
      {
         return;
      }

      int16_t spriteW = static_cast<int16_t>(strlen(text)) * charW(size);
      int16_t spriteH = charH(size);

      LGFX_Sprite sprite(&display);
      createSprite(sprite, spriteW, spriteH, size);
      sprite.fillScreen(static_cast<uint16_t>(backgroundColor));
      sprite.setTextColor(static_cast<uint16_t>(textColor), static_cast<uint16_t>(backgroundColor));
      sprite.setCursor(0, 0);
      sprite.print(text);
      sprite.setPivot(spriteW / 2.0f, spriteH / 2.0f);
      sprite.pushRotateZoom(centerX, centerY, angleDeg, 1.0f, 1.0f);
      sprite.deleteSprite();
   }

   ///
   /// <summary>
   /// Polls the touch controller once and updates the debounced tap state machine. Returns
   /// whether a finger is currently touching the screen, optionally writing the active touch
   /// coordinates to touchOut and whether a new debounced tap began on this poll to tappedOut.
   /// Safe to call unconditionally on non-touch boards (always returns false).
   /// </summary>
   /// <param name="touchOut">Optional point receiving the current touch coordinates when touched.</param>
   /// <param name="tappedOut">Optional flag set to true when a new debounced tap began on this poll.</param>
   /// <returns>True while a finger is touching the display; otherwise false.</returns>
   ///
   bool pollTouch(Point16* touchOut = nullptr, bool* tappedOut = nullptr)
   {
#ifdef ARDUINO_TOUCH_SUPPORTED
      lgfx::touch_point_t tp;
      bool touched = display.getTouch(&tp) > 0;
      bool tapped = touched && !_tapWasTouching && (!_tapHasSeenTouch || _tapDebounceTimer.remaining() == 0);
      if (touched)
      {
         _tapHasSeenTouch = true;
         _tapDebounceTimer.reset();
         if (touchOut != nullptr)
         {
            *touchOut = Point16(tp.x, tp.y);
         }
      }
      _tapWasTouching = touched;

      if (tappedOut != nullptr)
      {
         *tappedOut = tapped;
      }

      return touched;
#else
      (void)touchOut;
      if (tappedOut != nullptr)
      {
         *tappedOut = false;
      }
      return false;
#endif
   }

   ///
   /// <summary>
   /// Polls the touch controller for a clean, debounced single tap, requiring the finger to
   /// stay off the screen for at least TAP_DEBOUNCE_MS before another tap can fire. Safe to
   /// call unconditionally on non-touch boards (always returns false).
   /// </summary>
   /// <param name="tapOut">Optional point receiving the tap's screen coordinates when true is returned.</param>
   /// <returns>True when a new debounced tap began on this poll; otherwise false.</returns>
   ///
   bool pollTap(Point16* tapOut = nullptr)
   {
      Point16 pt;
      bool tapped = false;
      pollTouch(&pt, &tapped);
      if (tapped && tapOut != nullptr)
      {
         *tapOut = pt;
      }
      return tapped;
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

   ///
   /// <summary>
   /// Prints text wrapped on word boundaries within the specified left margin and max width.
   /// </summary>
   /// <param name="text">Text to print</param>
   /// <param name="x">Left X coordinate for each wrapped line</param>
   /// <param name="maxWidth">Maximum pixel width per line</param>
   /// <param name="textColor">Text foreground color</param>
   /// <param name="backgroundColor">Text background color</param>
   ///
   void printlnWrapped(
      const char* text,
      int16_t x,
      int16_t maxWidth,
      Color textColor = Color::WHITE,
      Color backgroundColor = Color::BLACK)
   {
      _printlnWrapped(text, x, maxWidth, textColor, backgroundColor);
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
      const String& initialValue = "",
      uint32_t timeoutMs = 0,
      bool allowCancel = false,
      bool allowEmpty = false,
      bool* outCancelled = nullptr);
   String promptTouchNumber(
      const char* title,
      const String& initialValue = "",
      bool allowDecimal = true,
      uint32_t timeoutMs = 0,
      bool allowCancel = false,
      bool allowEmpty = false,
      bool* outCancelled = nullptr);
   bool promptTouchYesNo(
      const char* title,
      bool defaultYes = true,
      uint32_t timeoutMs = 0,
      bool allowCancel = false,
      bool* outCancelled = nullptr);
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

   constexpr int16_t MARGIN_X = 8;
   constexpr int16_t GAP_Y = 6;

   clearDisplay();
   uint8_t titleSize = headerTextSize();
   setTextSize(titleSize);
   setCursor(MARGIN_X, 0);
   _printlnWrapped(title, MARGIN_X, width() - 2 * MARGIN_X, Color::HEADING, Color::BLACK);

   int16_t topY = getCursorY() + 4;
   int16_t availH = height() - topY - 4;
   size_t count = options.size();

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
/// Prompts the user to enter a text string via an on-screen touch QWERTY keyboard
/// (or over Serial if connected). Supports Shift/Caps toggle, 123/ABC mode toggle
/// for numbers and symbols, Space, Backspace, Clear, OK, and optional Cancel, and
/// optionally pre-populates the input box with an initial value. Restores the
/// initialization display before returning.
/// </summary>
/// <param name="title">Heading text drawn at the top of the screen.</param>
/// <param name="initialValue">Optional initial text pre-populated into the input box.</param>
/// <param name="timeoutMs">Inactivity timeout in milliseconds (0 uses Util::REQUIRED_PROMPT_TIMEOUT_S).</param>
/// <param name="allowCancel">If true, shows a Cancel key and returns an empty string on cancel or timeout; if false, resets into safe mode on timeout.</param>
/// <param name="allowEmpty">If true, OK is enabled even when the input box is empty and returns an empty string.</param>
/// <param name="outCancelled">Optional pointer set to true if cancelled or timed out, false if submitted.</param>
/// <returns>The trimmed text entered by the user, or an empty string if empty, cancelled, or timed out.</returns>
///
inline String ArduinoWithDisplay::promptTouchText(
   const char* title,
   const String& initialValue,
   uint32_t timeoutMs,
   bool allowCancel,
   bool allowEmpty,
   bool* outCancelled)
{
   if (outCancelled != nullptr)
   {
      *outCancelled = false;
   }

   enum class KeyAction : uint8_t
   {
      CHAR,
      SHIFT,
      MODE,
      BACKSPACE,
      CLEAR,
      SPACE,
      CANCEL,
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
   bool numberMode = false;

   constexpr int16_t MARGIN_X = 6;
   constexpr int16_t GAP_X = 4;
   constexpr int16_t GAP_Y = 4;
   constexpr size_t MAX_KEYS = 34;

   clearDisplay();
   uint8_t titleSize = headerTextSize();
   setTextSize(titleSize);
   setCursor(MARGIN_X, 0);
   _printlnWrapped(title, MARGIN_X, width() - 2 * MARGIN_X, Color::HEADING, Color::BLACK);

   uint8_t keyTextSize = (height() >= LARGE_DISPLAY_MIN_HEIGHT) ? 3 : 2;
   int16_t boxY = getCursorY() + 2;
   setTextSize(keyTextSize);
   int16_t clearKeyW = display.textWidth("Clear") + 16;
   if (clearKeyW > width() / 3)
   {
      setTextSize(2);
      clearKeyW = display.textWidth("Clear") + 12;
   }
   int16_t boxW = width() - 2 * MARGIN_X - clearKeyW - GAP_X;
   int16_t clearKeyX = MARGIN_X + boxW + GAP_X;
   int16_t boxH = (height() >= LARGE_DISPLAY_MIN_HEIGHT) ? (charH(keyTextSize) + 18) : (charH(keyTextSize) + 10);

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

   int16_t modeKeyW = sideKeyW;
   int16_t okKeyW = (row0W - 5 * stdKeyW - GAP_X) / 2;
   int16_t cancelKeyW = allowCancel ? okKeyW : 0;
   int16_t spaceKeyW = allowCancel
      ? (row0W - cancelKeyW - modeKeyW - okKeyW - 3 * GAP_X)
      : (row0W - modeKeyW - okKeyW - 2 * GAP_X);

   KeyDef keys[MAX_KEYS];
   size_t keyCount = 0;
   size_t clearKeyIdx = 0;
   size_t okKeyIdx = 0;

   static constexpr const char* ALPHA_ROW0 = "QWERTYUIOP";
   static constexpr const char* ALPHA_ROW1 = "ASDFGHJKL";
   static constexpr const char* ALPHA_ROW2 = "ZXCVBNM";

   static constexpr const char* NUM_ROW0 = "1234567890";
   static constexpr const char* NUM_ROW1 = "-/:;()$&@";
   static constexpr const char* NUM_ROW2 = ".,?!'\"#";

   int16_t y0 = kbTopY;
   int16_t y1 = y0 + keyH + GAP_Y;
   int16_t y2 = y1 + keyH + GAP_Y;
   int16_t y3 = y2 + keyH + GAP_Y;

   auto buildKeys = [&]()
   {
      keyCount = 0;
      const char* r0 = numberMode ? NUM_ROW0 : ALPHA_ROW0;
      const char* r1 = numberMode ? NUM_ROW1 : ALPHA_ROW1;
      const char* r2 = numberMode ? NUM_ROW2 : ALPHA_ROW2;

      clearKeyIdx = keyCount;
      keys[keyCount++] = { clearKeyX, boxY, clearKeyW, boxH, KeyAction::CLEAR, 0 };

      for (size_t i = 0; i < 10; i++)
      {
         keys[keyCount++] = { static_cast<int16_t>(row0StartX + static_cast<int16_t>(i) * (stdKeyW + GAP_X)), y0, stdKeyW, keyH, KeyAction::CHAR, r0[i] };
      }

      for (size_t i = 0; i < 9; i++)
      {
         keys[keyCount++] = { static_cast<int16_t>(row1StartX + static_cast<int16_t>(i) * (stdKeyW + GAP_X)), y1, stdKeyW, keyH, KeyAction::CHAR, r1[i] };
      }

      if (numberMode)
      {
         keys[keyCount++] = { row0StartX, y2, sideKeyW, keyH, KeyAction::CHAR, '_' };
      }
      else
      {
         keys[keyCount++] = { row0StartX, y2, sideKeyW, keyH, KeyAction::SHIFT, 0 };
      }

      for (size_t i = 0; i < 7; i++)
      {
         keys[keyCount++] = { static_cast<int16_t>(row2LettersStartX + static_cast<int16_t>(i) * (stdKeyW + GAP_X)), y2, stdKeyW, keyH, KeyAction::CHAR, r2[i] };
      }

      int16_t backX = row2LettersStartX + 7 * stdKeyW + 7 * GAP_X;
      int16_t backW = row0StartX + row0W - backX;
      keys[keyCount++] = { backX, y2, backW, keyH, KeyAction::BACKSPACE, 0 };

      int16_t row3X = row0StartX;
      if (allowCancel)
      {
         keys[keyCount++] = { row3X, y3, cancelKeyW, keyH, KeyAction::CANCEL, 0 };
         row3X += cancelKeyW + GAP_X;
      }
      keys[keyCount++] = { row3X, y3, modeKeyW, keyH, KeyAction::MODE, 0 };
      int16_t spaceX = row3X + modeKeyW + GAP_X;
      keys[keyCount++] = { spaceX, y3, spaceKeyW, keyH, KeyAction::SPACE, ' ' };
      int16_t okX = spaceX + spaceKeyW + GAP_X;
      okKeyIdx = keyCount;
      keys[keyCount++] = { okX, y3, okKeyW, keyH, KeyAction::OK, 0 };
   };

   buildKeys();

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

   auto canSubmit = [&]() -> bool
   {
      return allowEmpty || hasNonSpace();
   };

   auto drawTextBox = [&]()
   {
      display.fillRoundRect(MARGIN_X, boxY, boxW, boxH, 4, static_cast<uint16_t>(Color::BLACK));
      display.drawRoundRect(MARGIN_X, boxY, boxW, boxH, 4, static_cast<uint16_t>(Color::WHITE));

      setTextSize(keyTextSize);
      display.setTextColor(static_cast<uint16_t>(Color::VALUE), static_cast<uint16_t>(Color::BLACK));

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
         bg = canSubmit() ? Color::DARKGREEN : Color::DARKSLATEGRAY;
         fg = canSubmit() ? Color::WHITE : Color::GRAY;
      }
      else if (k.action == KeyAction::CANCEL)
      {
         bg = Color::DARKRED;
      }
      else if (k.action == KeyAction::SHIFT)
      {
         bg = (shifted || capsLock) ? Color::DARKCYAN : Color::DARKSLATEGRAY;
      }
      else if (k.action == KeyAction::MODE)
      {
         bg = numberMode ? Color::DARKCYAN : Color::DARKSLATEGRAY;
      }
      else if (k.action == KeyAction::BACKSPACE)
      {
         bg = Color::DARKSLATEGRAY;
      }
      else if (k.action == KeyAction::CLEAR)
      {
         bg = Color::DARKSLATEGRAY;
         fg = (value.length() > 0) ? Color::WHITE : Color::GRAY;
      }

      display.fillRoundRect(k.x, k.y, k.w, k.h, 5, static_cast<uint16_t>(bg));
      display.drawRoundRect(k.x, k.y, k.w, k.h, 5, static_cast<uint16_t>(Color::WHITE));

      char labelBuf[8] = {};
      if (k.action == KeyAction::CHAR)
      {
         if (k.ch >= 'A' && k.ch <= 'Z')
         {
            bool upper = shifted || capsLock;
            labelBuf[0] = upper ? k.ch : static_cast<char>(k.ch - 'A' + 'a');
         }
         else
         {
            labelBuf[0] = k.ch;
         }
         labelBuf[1] = '\0';
      }
      else if (k.action == KeyAction::SHIFT)
      {
         strcpy(labelBuf, capsLock ? "CAPS" : (shifted ? "SHFT" : "shft"));
      }
      else if (k.action == KeyAction::MODE)
      {
         strcpy(labelBuf, numberMode ? "ABC" : "123");
      }
      else if (k.action == KeyAction::BACKSPACE)
      {
         strcpy(labelBuf, "<-");
      }
      else if (k.action == KeyAction::CLEAR)
      {
         strcpy(labelBuf, "Clear");
      }
      else if (k.action == KeyAction::SPACE)
      {
         strcpy(labelBuf, "Space");
      }
      else if (k.action == KeyAction::CANCEL)
      {
         setTextSize(2);
         strcpy(labelBuf, display.textWidth("Cancel") <= k.w - 4 ? "Cancel" : "Back");
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

      display.setTextColor(static_cast<uint16_t>(fg), static_cast<uint16_t>(bg));
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
   uint32_t effectiveTimeoutMs = timeoutMs > 0 ? timeoutMs : (Util::REQUIRED_PROMPT_TIMEOUT_S * 1000UL);
   if (!allowCancel)
   {
      Util::setSetupStep(title);
   }

   while (true)
   {
      Util::feedSetupWatchdog();

      if (millis() - start >= effectiveTimeoutMs)
      {
         if (allowCancel)
         {
            if (outCancelled != nullptr)
            {
               *outCancelled = true;
            }
            Serial.println("Prompt timed out.");
            return "";
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
         bool prevValid = canSubmit();
         bool prevHadValue = (value.length() > 0);

         if (k.action == KeyAction::CHAR)
         {
            char c = k.ch;
            if (c >= 'A' && c <= 'Z')
            {
               bool upper = shifted || capsLock;
               c = upper ? c : static_cast<char>(c - 'A' + 'a');
            }
            value += c;
            drawTextBox();

            if (!numberMode && shifted && !capsLock)
            {
               shifted = false;
               drawAllKeys();
            }
            else
            {
               drawKey(idx, false);
               if ((value.length() > 0) != prevHadValue)
               {
                  drawKey(clearKeyIdx, false);
               }
               if (canSubmit() != prevValid)
               {
                  drawKey(okKeyIdx, false);
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
               if (!numberMode)
               {
                  drawAllKeys();
               }
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
               if (!numberMode)
               {
                  drawAllKeys();
               }
               else
               {
                  drawKey(idx, false);
                  if ((value.length() > 0) != prevHadValue)
                  {
                     drawKey(clearKeyIdx, false);
                  }
                  if (canSubmit() != prevValid)
                  {
                     drawKey(okKeyIdx, false);
                  }
               }
            }
            else
            {
               drawKey(idx, false);
               if ((value.length() > 0) != prevHadValue)
               {
                  drawKey(clearKeyIdx, false);
               }
               if (canSubmit() != prevValid)
               {
                  drawKey(okKeyIdx, false);
               }
            }
         }
         else if (k.action == KeyAction::CLEAR)
         {
            if (value.length() > 0)
            {
               value = "";
               drawTextBox();
            }
            bool shiftChanged = (!capsLock && !shifted);
            if (shiftChanged)
            {
               shifted = true;
            }
            if (shiftChanged && !numberMode)
            {
               drawAllKeys();
            }
            else
            {
               drawKey(idx, false);
               if (canSubmit() != prevValid)
               {
                  drawKey(okKeyIdx, false);
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
         else if (k.action == KeyAction::MODE)
         {
            numberMode = !numberMode;
            buildKeys();
            drawAllKeys();
         }
         else if (k.action == KeyAction::CANCEL)
         {
            if (outCancelled != nullptr)
            {
               *outCancelled = true;
            }
            Serial.println("Cancelled.");
            return "";
         }
         else if (k.action == KeyAction::OK)
         {
            String trimmed = value;
            trimmed.trim();
            if (allowEmpty || trimmed.length() > 0)
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
         else if (input.length() == 0 && allowEmpty)
         {
            restoreInitDisplay();
            return "";
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

///
/// <summary>
/// Prompts the user to enter a number using a large-key on-screen numeric keypad (0-9, optional
/// decimal point, backspace, clear, OK, and optional Cancel), with Serial fallback.
/// </summary>
/// <param name="title">Heading text displayed at the top of the screen</param>
/// <param name="initialValue">Optional initial numeric text pre-populated into the input box</param>
/// <param name="allowDecimal">When true, includes a '.' key; when false, allows digits only</param>
/// <param name="timeoutMs">Inactivity timeout in milliseconds (0 uses Util::REQUIRED_PROMPT_TIMEOUT_S)</param>
/// <param name="allowCancel">When true, shows a Cancel key and returns "" on cancel or timeout instead of resetting</param>
/// <param name="allowEmpty">When true, OK is enabled even when the input box is empty and returns ""</param>
/// <param name="outCancelled">Optional pointer set to true if cancelled/timed out, false if submitted</param>
/// <returns>The trimmed numeric string entered by the user, or "" if empty/cancelled/timed out</returns>
///
inline String ArduinoWithDisplay::promptTouchNumber(
   const char* title,
   const String& initialValue,
   bool allowDecimal,
   uint32_t timeoutMs,
   bool allowCancel,
   bool allowEmpty,
   bool* outCancelled)
{
   if (outCancelled != nullptr)
   {
      *outCancelled = false;
   }

   enum class KeyAction : uint8_t
   {
      DIGIT,
      DECIMAL,
      BACKSPACE,
      CLEAR,
      CANCEL,
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

   constexpr int16_t MARGIN_X = 8;
   constexpr int16_t GAP_X = 8;
   constexpr int16_t GAP_Y = 8;
   constexpr size_t MAX_KEYS = 15;

   clearDisplay();
   uint8_t titleSize = headerTextSize();
   setTextSize(titleSize);
   setCursor(MARGIN_X, 0);
   _printlnWrapped(title, MARGIN_X, width() - 2 * MARGIN_X, Color::HEADING, Color::BLACK);

   uint8_t boxTextSize = (height() >= LARGE_DISPLAY_MIN_HEIGHT) ? 4 : 3;
   int16_t boxY = getCursorY() + 4;
   int16_t boxW = width() - 2 * MARGIN_X;
   int16_t boxH = charH(boxTextSize) + 12;

   int16_t kbTopY = boxY + boxH + 8;
   int16_t availH = height() - kbTopY - 6;
   int16_t keyH = (availH - 4 * GAP_Y) / 5;
   int16_t maxKeyH = (height() >= LARGE_DISPLAY_MIN_HEIGHT) ? 96 : 52;
   if (keyH > maxKeyH)
   {
      keyH = maxKeyH;
   }
   if (keyH < charH(2) + 4)
   {
      keyH = charH(2) + 4;
   }

   uint8_t keyTextSize = (height() >= LARGE_DISPLAY_MIN_HEIGHT) ? 4 : 3;
   while (keyTextSize > 2 && keyH < charH(keyTextSize) + 8)
   {
      keyTextSize--;
   }

   int16_t availW = width() - 2 * MARGIN_X;
   int16_t colW = (availW - 2 * GAP_X) / 3;
   int16_t gridW = 3 * colW + 2 * GAP_X;
   int16_t startX = (width() - gridW) / 2;

   std::array<KeyDef, MAX_KEYS> keys{};
   size_t keyCount = 0;

   static constexpr std::array<const char*, 3> DIGIT_ROWS = { "123", "456", "789" };
   for (int16_t row = 0; row < 3; row++)
   {
      int16_t ry = kbTopY + row * (keyH + GAP_Y);
      for (int16_t col = 0; col < 3; col++)
      {
         int16_t kx = startX + col * (colW + GAP_X);
         keys[keyCount++] = { kx, ry, colW, keyH, KeyAction::DIGIT, DIGIT_ROWS[row][col] };
      }
   }

   int16_t y3 = kbTopY + 3 * (keyH + GAP_Y);
   int16_t y4 = kbTopY + 4 * (keyH + GAP_Y);

   if (allowDecimal)
   {
      keys[keyCount++] = { startX, y3, colW, keyH, KeyAction::DECIMAL, '.' };
   }
   else
   {
      keys[keyCount++] = { startX, y3, colW, keyH, KeyAction::CLEAR, 0 };
   }
   keys[keyCount++] = { static_cast<int16_t>(startX + colW + GAP_X), y3, colW, keyH, KeyAction::DIGIT, '0' };
   keys[keyCount++] = { static_cast<int16_t>(startX + 2 * (colW + GAP_X)), y3, colW, keyH, KeyAction::BACKSPACE, 0 };

   if (allowDecimal && allowCancel)
   {
      keys[keyCount++] = { startX, y4, colW, keyH, KeyAction::CANCEL, 0 };
      keys[keyCount++] = { static_cast<int16_t>(startX + colW + GAP_X), y4, colW, keyH, KeyAction::CLEAR, 0 };
      keys[keyCount++] = { static_cast<int16_t>(startX + 2 * (colW + GAP_X)), y4, colW, keyH, KeyAction::OK, 0 };
   }
   else if (!allowDecimal && allowCancel)
   {
      int16_t halfW = (gridW - GAP_X) / 2;
      int16_t okW = gridW - halfW - GAP_X;
      keys[keyCount++] = { startX, y4, halfW, keyH, KeyAction::CANCEL, 0 };
      keys[keyCount++] = { static_cast<int16_t>(startX + halfW + GAP_X), y4, okW, keyH, KeyAction::OK, 0 };
   }
   else if (allowDecimal && !allowCancel)
   {
      int16_t okW = gridW - colW - GAP_X;
      keys[keyCount++] = { startX, y4, colW, keyH, KeyAction::CLEAR, 0 };
      keys[keyCount++] = { static_cast<int16_t>(startX + colW + GAP_X), y4, okW, keyH, KeyAction::OK, 0 };
   }
   else
   {
      keys[keyCount++] = { startX, y4, gridW, keyH, KeyAction::OK, 0 };
   }

   auto hasDigit = [&]() -> bool
   {
      for (size_t i = 0; i < value.length(); i++)
      {
         if (value[i] >= '0' && value[i] <= '9')
         {
            return true;
         }
      }
      return false;
   };

   auto canSubmit = [&]() -> bool
   {
      return hasDigit() || (allowEmpty && value.length() == 0);
   };

   auto drawTextBox = [&]()
   {
      display.fillRoundRect(MARGIN_X, boxY, boxW, boxH, 6, static_cast<uint16_t>(Color::BLACK));
      display.drawRoundRect(MARGIN_X, boxY, boxW, boxH, 6, static_cast<uint16_t>(Color::WHITE));

      setTextSize(boxTextSize);
      display.setTextColor(static_cast<uint16_t>(Color::VALUE), static_cast<uint16_t>(Color::BLACK));

      String displayStr = value + "_";
      int16_t maxTextW = boxW - 16;
      while (displayStr.length() > 1 && display.textWidth(displayStr.c_str()) > maxTextW)
      {
         displayStr.remove(0, 1);
      }

      int16_t ty = boxY + (boxH - charH(boxTextSize)) / 2;
      display.setCursor(MARGIN_X + 8, ty);
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
         bg = canSubmit() ? Color::DARKGREEN : Color::DARKSLATEGRAY;
         fg = canSubmit() ? Color::WHITE : Color::GRAY;
      }
      else if (k.action == KeyAction::CANCEL)
      {
         bg = Color::DARKRED;
      }
      else if (k.action == KeyAction::BACKSPACE || k.action == KeyAction::CLEAR)
      {
         bg = Color::DARKSLATEGRAY;
      }

      display.fillRoundRect(k.x, k.y, k.w, k.h, 8, static_cast<uint16_t>(bg));
      display.drawRoundRect(k.x, k.y, k.w, k.h, 8, static_cast<uint16_t>(Color::WHITE));

      char labelBuf[8] = {};
      if (k.action == KeyAction::DIGIT || k.action == KeyAction::DECIMAL)
      {
         labelBuf[0] = k.ch;
         labelBuf[1] = '\0';
      }
      else if (k.action == KeyAction::BACKSPACE)
      {
         strcpy(labelBuf, "<-");
      }
      else if (k.action == KeyAction::CLEAR)
      {
         strcpy(labelBuf, "Clr");
      }
      else if (k.action == KeyAction::CANCEL)
      {
         setTextSize(2);
         strcpy(labelBuf, display.textWidth("Cancel") <= k.w - 8 ? "Cancel" : "Back");
      }
      else if (k.action == KeyAction::OK)
      {
         strcpy(labelBuf, "OK");
      }

      uint8_t labelSize = keyTextSize;
      setTextSize(labelSize);
      while (labelSize > 2 && display.textWidth(labelBuf) > k.w - 8)
      {
         labelSize--;
         setTextSize(labelSize);
      }

      display.setTextColor(static_cast<uint16_t>(fg), static_cast<uint16_t>(bg));
      int16_t tw = display.textWidth(labelBuf);
      int16_t tx = k.x + (k.w - tw) / 2;
      int16_t ty = k.y + (k.h - charH(labelSize)) / 2;
      display.setCursor(tx, ty);
      display.print(labelBuf);
   };

   drawTextBox();
   for (size_t i = 0; i < keyCount; i++)
   {
      drawKey(i, false);
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

   if (value.length() > 0)
   {
      Serial.print("Enter ");
      Serial.print(title);
      Serial.print(" (blank to keep \"");
      Serial.print(value);
      Serial.print("\") or use touch keypad: ");
   }
   else
   {
      Serial.print("Enter ");
      Serial.print(title);
      Serial.print(" or use touch keypad: ");
   }

   size_t pressedIdx = SIZE_MAX;
   uint32_t start = millis();
   uint32_t effectiveTimeoutMs = timeoutMs > 0 ? timeoutMs : (Util::REQUIRED_PROMPT_TIMEOUT_S * 1000UL);
   if (!allowCancel)
   {
      Util::setSetupStep(title);
   }

   while (true)
   {
      Util::feedSetupWatchdog();

      if (millis() - start >= effectiveTimeoutMs)
      {
         if (allowCancel)
         {
            if (outCancelled != nullptr)
            {
               *outCancelled = true;
            }
            Serial.println("Prompt timed out.");
            return "";
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
         bool prevValid = canSubmit();

         if (k.action == KeyAction::DIGIT)
         {
            value += k.ch;
            drawTextBox();
            drawKey(idx, false);
            if (canSubmit() != prevValid)
            {
               drawKey(keyCount - 1, false);
            }
         }
         else if (k.action == KeyAction::DECIMAL)
         {
            if (value.indexOf('.') < 0)
            {
               value += (value.length() == 0) ? "0." : ".";
               drawTextBox();
            }
            drawKey(idx, false);
            if (canSubmit() != prevValid)
            {
               drawKey(keyCount - 1, false);
            }
         }
         else if (k.action == KeyAction::BACKSPACE)
         {
            if (value.length() > 0)
            {
               value.remove(value.length() - 1);
               drawTextBox();
            }
            drawKey(idx, false);
            if (canSubmit() != prevValid)
            {
               drawKey(keyCount - 1, false);
            }
         }
         else if (k.action == KeyAction::CLEAR)
         {
            if (value.length() > 0)
            {
               value = "";
               drawTextBox();
            }
            drawKey(idx, false);
            if (canSubmit() != prevValid)
            {
               drawKey(keyCount - 1, false);
            }
         }
         else if (k.action == KeyAction::CANCEL)
         {
            if (outCancelled != nullptr)
            {
               *outCancelled = true;
            }
            Serial.println("Cancelled.");
            return "";
         }
         else if (k.action == KeyAction::OK)
         {
            if (canSubmit())
            {
               Serial.println(value.length() > 0 ? value : String("(keep)"));
               restoreInitDisplay();
               return value;
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

         if (input.length() == 0 && canSubmit())
         {
            restoreInitDisplay();
            return value;
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

///
/// <summary>
/// Prompts the user to answer a yes/no question using two mutually exclusive on-screen checkboxes
/// with OK and optional Cancel buttons, with Serial fallback.
/// </summary>
/// <param name="title">Heading text displayed at the top of the screen</param>
/// <param name="defaultYes">When true, "Yes" starts checked; when false, "No" starts checked</param>
/// <param name="timeoutMs">Inactivity timeout in milliseconds (0 uses Util::REQUIRED_PROMPT_TIMEOUT_S)</param>
/// <param name="allowCancel">When true, shows a Cancel button and sets *outCancelled on cancel or timeout</param>
/// <param name="outCancelled">Optional pointer set to true if cancelled/timed out, false if confirmed</param>
/// <returns>True if "Yes" was confirmed, false if "No" was confirmed or if cancelled</returns>
///
inline bool ArduinoWithDisplay::promptTouchYesNo(
   const char* title,
   bool defaultYes,
   uint32_t timeoutMs,
   bool allowCancel,
   bool* outCancelled)
{
   if (outCancelled != nullptr)
   {
      *outCancelled = false;
   }

   enum class HitTarget : uint8_t
   {
      NONE,
      YES_BOX,
      NO_BOX,
      CANCEL,
      OK,
   };

   bool selectedYes = defaultYes;

   constexpr int16_t MARGIN_X = 12;
   constexpr int16_t GAP_Y = 16;

   clearDisplay();
   uint8_t titleSize = headerTextSize();
   setTextSize(titleSize);
   setCursor(MARGIN_X, 8);
   _printlnWrapped(title, MARGIN_X, width() - 2 * MARGIN_X, Color::HEADING, Color::BLACK);

   const bool isLarge = height() >= LARGE_DISPLAY_MIN_HEIGHT;
   const int16_t cardW = width() - 2 * MARGIN_X;
   const int16_t cardH = isLarge ? 84 : 52;
   const int16_t checkSize = isLarge ? 44 : 28;
   const uint8_t labelSize = isLarge ? 4 : 3;
   const int16_t btnH = isLarge ? 72 : 44;
   const int16_t btnY = height() - MARGIN_X - btnH;

   const int16_t yesY = getCursorY() + GAP_Y;
   const int16_t noY = yesY + cardH + GAP_Y;

   const int16_t gapX = 12;
   const int16_t cancelW = allowCancel ? (cardW - gapX) / 2 : 0;
   const int16_t okX = allowCancel ? (MARGIN_X + cancelW + gapX) : MARGIN_X;
   const int16_t okW = allowCancel ? (cardW - cancelW - gapX) : cardW;

   auto drawCheckmark = [&](int16_t cx, int16_t cy, int16_t size)
   {
      int16_t x1 = cx + size / 5;
      int16_t y1 = cy + size / 2;
      int16_t x2 = cx + (size * 44) / 100;
      int16_t y2 = cy + (size * 76) / 100;
      int16_t x3 = cx + (size * 82) / 100;
      int16_t y3 = cy + (size * 24) / 100;
      int16_t thickness = isLarge ? 4 : 3;
      for (int16_t t = 0; t < thickness; t++)
      {
         display.drawLine(x1, y1 + t, x2, y2 + t, static_cast<uint16_t>(Color::WHITE));
         display.drawLine(x2, y2 + t, x3, y3 + t, static_cast<uint16_t>(Color::WHITE));
      }
   };

   auto drawCheckboxRow = [&](int16_t y, const char* label, bool checked, bool pressed)
   {
      Color cardBg = pressed ? Color::LIGHTBLUE : (checked ? Color::DARKBLUE : Color::BLACK);
      Color cardBorder = checked ? Color::WHITE : Color::GRAY;
      display.fillRoundRect(MARGIN_X, y, cardW, cardH, 8, static_cast<uint16_t>(cardBg));
      display.drawRoundRect(MARGIN_X, y, cardW, cardH, 8, static_cast<uint16_t>(cardBorder));

      int16_t boxX = MARGIN_X + 18;
      int16_t boxY = y + (cardH - checkSize) / 2;
      Color boxBg = checked ? Color::DARKGREEN : Color::BLACK;
      display.fillRoundRect(boxX, boxY, checkSize, checkSize, 6, static_cast<uint16_t>(boxBg));
      display.drawRoundRect(boxX, boxY, checkSize, checkSize, 6, static_cast<uint16_t>(Color::WHITE));
      if (checked)
      {
         drawCheckmark(boxX, boxY, checkSize);
      }

      setTextSize(labelSize);
      display.setTextColor(static_cast<uint16_t>(Color::WHITE), static_cast<uint16_t>(cardBg));
      int16_t textX = boxX + checkSize + 20;
      int16_t textY = y + (cardH - charH(labelSize)) / 2;
      display.setCursor(textX, textY);
      display.print(label);
   };

   auto drawActionButton = [&](int16_t x, int16_t w, const char* label, Color baseBg, bool pressed)
   {
      Color bg = pressed ? Color::LIGHTBLUE : baseBg;
      display.fillRoundRect(x, btnY, w, btnH, 8, static_cast<uint16_t>(bg));
      display.drawRoundRect(x, btnY, w, btnH, 8, static_cast<uint16_t>(Color::WHITE));

      uint8_t sz = labelSize;
      setTextSize(sz);
      while (sz > 2 && display.textWidth(label) > w - 12)
      {
         sz--;
         setTextSize(sz);
      }
      display.setTextColor(static_cast<uint16_t>(Color::WHITE), static_cast<uint16_t>(bg));
      int16_t tx = x + (w - display.textWidth(label)) / 2;
      int16_t ty = btnY + (btnH - charH(sz)) / 2;
      display.setCursor(tx, ty);
      display.print(label);
   };

   auto drawAll = [&](HitTarget pressed)
   {
      drawCheckboxRow(yesY, "Yes", selectedYes, pressed == HitTarget::YES_BOX);
      drawCheckboxRow(noY, "No", !selectedYes, pressed == HitTarget::NO_BOX);
      if (allowCancel)
      {
         drawActionButton(MARGIN_X, cancelW, "Cancel", Color::DARKRED, pressed == HitTarget::CANCEL);
      }
      drawActionButton(okX, okW, "OK", Color::DARKGREEN, pressed == HitTarget::OK);
   };

   drawAll(HitTarget::NONE);

   lgfx::touch_point_t tp;
   while (display.getTouch(&tp) > 0)
   {
      delay(10);
   }

   while (Serial.available())
   {
      Serial.read();
   }

   Serial.print(title);
   Serial.print(" (yes/no, blank for ");
   Serial.print(selectedYes ? "yes" : "no");
   Serial.print(") or use touch: ");

   HitTarget pressed = HitTarget::NONE;
   uint32_t start = millis();
   uint32_t effectiveTimeoutMs = timeoutMs > 0 ? timeoutMs : (Util::REQUIRED_PROMPT_TIMEOUT_S * 1000UL);
   if (!allowCancel)
   {
      Util::setSetupStep(title);
   }

   while (true)
   {
      Util::feedSetupWatchdog();

      if (millis() - start >= effectiveTimeoutMs)
      {
         if (allowCancel)
         {
            if (outCancelled != nullptr)
            {
               *outCancelled = true;
            }
            Serial.println("Prompt timed out.");
            return false;
         }

         Serial.println("No input - entering safe mode.");
         Util::reset(0.0f, Util::PROMPT_TIMEOUT_HALT_REASON);
      }

      bool touched = display.getTouch(&tp) > 0;
      if (touched)
      {
         start = millis();
         Util::feedSetupWatchdog();

         HitTarget hit = HitTarget::NONE;
         if (tp.x >= MARGIN_X && tp.x < MARGIN_X + cardW && tp.y >= yesY && tp.y < yesY + cardH)
         {
            hit = HitTarget::YES_BOX;
         }
         else if (tp.x >= MARGIN_X && tp.x < MARGIN_X + cardW && tp.y >= noY && tp.y < noY + cardH)
         {
            hit = HitTarget::NO_BOX;
         }
         else if (allowCancel && tp.x >= MARGIN_X && tp.x < MARGIN_X + cancelW && tp.y >= btnY && tp.y < btnY + btnH)
         {
            hit = HitTarget::CANCEL;
         }
         else if (tp.x >= okX && tp.x < okX + okW && tp.y >= btnY && tp.y < btnY + btnH)
         {
            hit = HitTarget::OK;
         }

         if (hit != pressed)
         {
            pressed = hit;
            drawAll(pressed);
         }
      }
      else if (pressed != HitTarget::NONE)
      {
         HitTarget released = pressed;
         pressed = HitTarget::NONE;

         if (released == HitTarget::YES_BOX)
         {
            selectedYes = true;
            drawAll(HitTarget::NONE);
         }
         else if (released == HitTarget::NO_BOX)
         {
            selectedYes = false;
            drawAll(HitTarget::NONE);
         }
         else if (released == HitTarget::CANCEL)
         {
            if (outCancelled != nullptr)
            {
               *outCancelled = true;
            }
            Serial.println("Cancelled.");
            return false;
         }
         else if (released == HitTarget::OK)
         {
            Serial.println(selectedYes ? "yes" : "no");
            restoreInitDisplay();
            return selectedYes;
         }
      }

      if (Serial.available())
      {
         start = millis();
         Util::feedSetupWatchdog();
         String input = Serial.readStringUntil('\n');
         input.trim();
         Serial.println(input);

         if (input.length() == 0)
         {
            restoreInitDisplay();
            return selectedYes;
         }
         if (strcasecmp(input.c_str(), "yes") == 0 || strcasecmp(input.c_str(), "y") == 0)
         {
            restoreInitDisplay();
            return true;
         }
         if (strcasecmp(input.c_str(), "no") == 0 || strcasecmp(input.c_str(), "n") == 0)
         {
            restoreInitDisplay();
            return false;
         }

         Serial.print("Enter yes or no: ");
      }

      delay(10);
   }
}
#endif
