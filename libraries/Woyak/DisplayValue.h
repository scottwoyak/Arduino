#pragma once

#include "ArduinoWithDisplay.h"
#include "Format.h"
#include "Color.h"
#include "Util.h"

///
/// <summary>
/// Draws a single formatted value on a display using an off-screen sprite, so repeated
/// draw() calls only repaint the sprite's small region instead of the whole display.
/// The value is formatted through a Format object, which keeps its rendered width fixed,
/// and is aligned within that fixed width per the value's own Alignment.
/// </summary>
/// <remarks>
/// The sprite is created lazily on first use (the first draw() call), so it picks up
/// the display's font metrics once the caller has set up its font size.
/// The position is set via setPosition() and is assumed to stay constant until changed.
/// Every call to draw() unconditionally redraws the sprite, so callers are free to pass
/// a different value and/or color on each call.
/// </remarks>
///
class DisplayValue
{
public:
   ///
   /// <summary>
   /// Controls how the value's text is horizontally aligned within its fixed-width sprite.
   /// </summary>
   ///
   enum class Alignment
   {
      LEFT,     // text's left edge is aligned to the sprite's left edge
      RIGHT,    // text's right edge is aligned to the sprite's right edge
      CENTER,   // text is centered within the sprite
      DECIMAL   // text's decimal point is centered within the sprite
    };

private:
   ArduinoWithDisplay* _display;
   int16_t _x = 0;
   int16_t _y = 0;
   Format _format;
   Alignment _alignment;
   LGFX_Sprite _sprite;
   bool _spriteCreated = false;
   uint8_t _textSize;

   ///
   /// <summary>
   /// Creates the off-screen sprite, sized to fit the format's fixed width and the current
   /// font height, and loads the value's font into it. Must not be called if the sprite
   /// has already been created.
   /// </summary>
   ///
   void _createSprite()
   {
      int16_t spriteWidth = (int16_t)_display->charW(_textSize) * _format.length();
      int16_t spriteHeight = (int16_t)_display->charH(_textSize);

      _display->createSprite(_sprite, spriteWidth, spriteHeight, _textSize);

      _spriteCreated = true;
   }

   ///
   /// <summary>
   /// Strips the Format's literal space padding from the given value, leaving just the
   /// significant text, so its width can be measured and positioned in pixels.
   /// </summary>
   /// <param name="value">The formatted value to trim.</param>
   /// <returns>The value with leading/trailing space padding removed.</returns>
   ///
   std::string _trimmedValue(const std::string& value) const
   {
      size_t start = value.find_first_not_of(' ');
      if (start == std::string::npos)
      {
         return std::string();
      }
      size_t end = value.find_last_not_of(' ');
      return value.substr(start, end - start + 1);
   }

   ///
   /// <summary>
   /// Computes the X offset (within a region of the given total width) at which to draw
   /// text of the given pixel width, honoring the value's alignment.
   /// </summary>
   /// <param name="trimmed">The trimmed value text being drawn.</param>
   /// <param name="textWidth">Measured pixel width of the text to draw.</param>
   /// <param name="totalWidth">Total pixel width of the region to align within.</param>
   /// <returns>X offset for the text's left edge.</returns>
   ///
   int16_t _alignedTextX(const std::string& trimmed, int16_t textWidth, int16_t totalWidth)
   {
      switch (_alignment)
      {
         case Alignment::RIGHT:
            return max(static_cast<int16_t>(0), static_cast<int16_t>(totalWidth - textWidth));

         case Alignment::CENTER:
            return max(static_cast<int16_t>(0), static_cast<int16_t>((totalWidth - textWidth) / 2));

         case Alignment::DECIMAL:
         case Alignment::LEFT:
         default:
            return 0;
      }
   }

   ///
   /// <summary>
   /// Gets the number of characters preceding the decimal point in the given trimmed
   /// value text, or the full character count if no decimal point is present.
   /// </summary>
   /// <param name="trimmed">The trimmed value text being drawn.</param>
   /// <returns>The count of characters before the decimal point.</returns>
   ///
   size_t _charsBeforeDecimal(const std::string& trimmed) const
   {
      size_t dotPos = trimmed.find('.');
      return dotPos == std::string::npos ? trimmed.length() : dotPos;
   }

   ///
   /// <summary>
   /// Renders the current value into the off-screen sprite (created in the constructor)
   /// and pushes it over the value region.
   /// </summary>
   /// <param name="value">The formatted value to draw.</param>
   /// <param name="valueColor">The color used to draw the value text.</param>
   /// <param name="backgroundColor">The background color drawn behind the value.</param>
   /// <remarks>
   /// The value string is right/left/center-aligned using its actual measured pixel
   /// width rather than relying on the Format's literal space padding, since a space
   /// glyph's advance width does not always exactly match a digit's advance width in
   /// bitmap fonts. Using space padding for alignment can therefore leave a visible gap
   /// that varies from value to value (e.g. between two values with different digit
   /// counts), which pixel-based alignment avoids.
   /// </remarks>
   ///
   void _drawSprite(const std::string& value, Color valueColor, Color backgroundColor)
   {
      if (!_spriteCreated)
      {
         _createSprite();
      }

      std::string trimmed = _trimmedValue(value);
      int16_t textWidth = (int16_t)_sprite.textWidth(trimmed.c_str());
      int16_t spriteWidth = _sprite.width();
      int16_t textX = _alignedTextX(trimmed, textWidth, spriteWidth);

      int16_t pushX = _x;
      if (_alignment == Alignment::DECIMAL)
      {
         // shift the sprite left by the width of the characters preceding the decimal
         // point, so the decimal point itself lands at the stored anchor position _x
         size_t charsBeforeDecimal = _charsBeforeDecimal(trimmed);
         pushX -= (int16_t)_display->charW(_textSize) * (int16_t)charsBeforeDecimal;
      }

      _sprite.fillScreen((uint16_t)backgroundColor);
      _sprite.setTextColor((uint16_t)valueColor, (uint16_t)backgroundColor);
      _sprite.setCursor(textX, 0);
      _sprite.print(trimmed.c_str());
      _sprite.pushSprite(pushX, _y);
   }

public:
   ///
   /// <summary>
   /// Initializes a new instance of the DisplayValue class.
   /// </summary>
   /// <param name="display">The display interface to draw onto.</param>
   /// <param name="format">The formatter applied to the value, controlling its fixed width.</param>
   /// <param name="textSize">The font size used to draw the value text.</param>
   /// <param name="alignment">Controls how the value's text is aligned within its fixed width.</param>
   ///
   DisplayValue(ArduinoWithDisplay* display,
      const Format& format,
      uint8_t textSize,
      Alignment alignment = Alignment::LEFT)
      : _display(display),
      _format(format),
      _alignment(alignment),
      _sprite(&display->display)
   {
      _textSize = constrain(textSize, (uint8_t)1, (uint8_t)7);
   }

   ///
   /// <summary>
   /// Returns the pixel width of the value's fixed-width sprite, e.g. so a caller can
   /// lay out other content (such as a label) relative to it.
   /// </summary>
   /// <returns>The sprite's width in pixels.</returns>
   ///
   int16_t width() const
   {
      return (int16_t)_display->charW(_textSize) * _format.length();
   }

   ///
   /// <summary>
   /// Sets the position at which the value sprite is pushed on the next draw(). The
   /// meaning of x depends on the value's alignment:
   /// <list type="bullet">
   /// <item>Alignment::LEFT: x is the position of the sprite's left edge.</item>
   /// <item>Alignment::RIGHT: x is the position of the sprite's right edge rather than
   /// its left edge, so a right-aligned value can be anchored to a fixed point (e.g.
   /// the display's right edge) without the caller needing to know the sprite's width.</item>
   /// <item>Alignment::CENTER: x is the position of the sprite's horizontal midpoint,
   /// so a centered value can be anchored to a fixed point (e.g. the display's center)
   /// without the caller needing to know the sprite's width.</item>
   /// <item>Alignment::DECIMAL: x is the position where the decimal point is drawn; the
   /// stored position is shifted left at draw time by the width of the characters
   /// preceding the decimal point (see _drawSprite()), so multiple DisplayValues with
   /// different format widths can share the same x and have their decimal points line
   /// up on screen.</item>
   /// </list>
   /// </summary>
   /// <param name="x">The X coordinate of the value's anchor (left edge; right edge for Alignment::RIGHT; horizontal midpoint for Alignment::CENTER; decimal point position for Alignment::DECIMAL); negative values offset from the right edge.</param>
   /// <param name="y">The Y coordinate of the value's top-left corner; negative values offset from the bottom edge.</param>
   ///
   void setPosition(int16_t x, int16_t y)
   {
      _display->normalizeCoords(x, y);

      if (_alignment == Alignment::RIGHT)
      {
         x = x - width();
      }
      else if (_alignment == Alignment::CENTER)
      {
         x = x - width() / 2;
      }

      _x = x;
      _y = y;
   }

   ///
   /// <summary>
   /// Sets the top-left position at which the value sprite is pushed on the next draw().
   /// </summary>
   /// <param name="pos">The top-left coordinate of the value.</param>
   ///
   void setPosition(Point16 pos)
   {
      setPosition(pos.x, pos.y);
   }

   ///
   /// <summary>
   /// Sets the value to display and draws it via the sprite.
   /// </summary>
   /// <param name="value">The new value to display, formatted through the Format object.</param>
   /// <param name="valueColor">The color used to draw the value text.</param>
   /// <param name="backgroundColor">The background color drawn behind the value, e.g. to
   /// highlight a currently selected/editable value.</param>
   ///
   template <typename T>
   void draw(const T& value, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      ASSERT(_display != nullptr);

      std::string formattedValue = _format.toString(value);
      _drawSprite(formattedValue, valueColor, backgroundColor);
   }

   ///
   /// <summary>
   /// Blanks the value's region by filling the sprite with the given color and pushing
   /// it, without measuring or drawing any text. Cheaper than draw(""), so the next
   /// draw() still measures/aligns its own text normally.
   /// </summary>
   /// <param name="backgroundColor">The color painted over the value's region.</param>
   ///
   void clear(Color backgroundColor = Color::BLACK)
   {
      ASSERT(_display != nullptr);

      if (!_spriteCreated)
      {
         _createSprite();
      }

      _sprite.fillScreen((uint16_t)backgroundColor);
      _sprite.pushSprite(_x, _y);
   }
};
