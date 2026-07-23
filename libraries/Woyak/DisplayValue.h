#pragma once

#include "ArduinoWithDisplay.h"
#include "Format.h"
#include "Color.h"

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
/// Call invalidate() if the display area was cleared, so the next draw() repaints.
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
      DECIMAL   // text's decimal point is centered within the sprite
   };

private:
   ArduinoWithDisplay* _display;
   int16_t _x = 0;
   int16_t _y = 0;
   const Format* _format;
   Alignment _alignment;
   Color _valueColor;
   Color _valueBackgroundColor = Color::BLACK;
   bool _dirty = true;
   String _value;
   String _drawnValue;
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
      int16_t spriteWidth = (int16_t)_display->charW(_textSize) * _format->length();
      int16_t spriteHeight = (int16_t)_display->charH(_textSize);

      _sprite.setColorDepth(16);
      _sprite.createSprite(spriteWidth, spriteHeight);

      // load our own copy of the font rather than sharing the display's runtime font
      // pointer, which can be freed out from under us if the display later loads a
      // different font (e.g. another DisplayValue or the sketch switching modes)
      _sprite.loadFont(RobotoMonoBold[_textSize]);

      _spriteCreated = true;
   }

   ///
   /// <summary>
   /// Strips the Format's literal space padding from the current value, leaving just the
   /// significant text, so its width can be measured and positioned in pixels.
   /// </summary>
   /// <returns>The current value with leading/trailing space padding removed.</returns>
   ///
   std::string _trimmedValue() const
   {
      std::string str = _value.c_str();
      size_t start = str.find_first_not_of(' ');
      if (start == std::string::npos)
      {
         return std::string();
      }
      size_t end = str.find_last_not_of(' ');
      return str.substr(start, end - start + 1);
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

         case Alignment::DECIMAL:
         {
            size_t dotPos = trimmed.find('.');
            if (dotPos == std::string::npos)
            {
               return max(static_cast<int16_t>(0), static_cast<int16_t>(totalWidth - textWidth));
            }

            std::string beforeDot = trimmed.substr(0, dotPos);
            int16_t beforeWidth = (int16_t)_sprite.textWidth(beforeDot.c_str());
            return max(static_cast<int16_t>(0), static_cast<int16_t>((totalWidth / 2) - beforeWidth));
         }

         case Alignment::LEFT:
         default:
            return 0;
      }
   }

   ///
   /// <summary>
   /// Renders the current value into the off-screen sprite (created in the constructor)
   /// and pushes it over the value region.
   /// </summary>
   /// <remarks>
   /// The value string is right/left/center-aligned using its actual measured pixel
   /// width rather than relying on the Format's literal space padding, since a space
   /// glyph's advance width does not always exactly match a digit's advance width in
   /// bitmap fonts. Using space padding for alignment can therefore leave a visible gap
   /// that varies from value to value (e.g. between two values with different digit
   /// counts), which pixel-based alignment avoids.
   /// </remarks>
   ///
   void _drawSprite()
   {
      if (!_spriteCreated)
      {
         _createSprite();
      }

      std::string trimmed = _trimmedValue();
      int16_t textWidth = (int16_t)_sprite.textWidth(trimmed.c_str());
      int16_t spriteWidth = _sprite.width();
      int16_t textX = _alignedTextX(trimmed, textWidth, spriteWidth);

      _sprite.fillScreen((uint16_t)_valueBackgroundColor);
      _sprite.setTextColor((uint16_t)_valueColor, (uint16_t)_valueBackgroundColor);
      _sprite.setCursor(textX, 0);
      _sprite.print(trimmed.c_str());
      _sprite.pushSprite(_x, _y);
   }

public:
   ///
   /// <summary>
   /// Initializes a new instance of the DisplayValue class.
   /// </summary>
   /// <param name="display">The display interface to draw onto.</param>
   /// <param name="format">The formatter applied to the value, controlling its fixed width.</param>
   /// <param name="textSize">The font size used to draw the value text.</param>
   /// <param name="valueColor">The color used to draw the value text.</param>
   /// <param name="alignment">Controls how the value's text is aligned within its fixed width.</param>
   ///
   DisplayValue(ArduinoWithDisplay* display, const Format& format, uint8_t textSize,
      Color valueColor = Color::VALUE, Alignment alignment = Alignment::LEFT)
      : _display(display), _format(&format), _alignment(alignment),
      _valueColor(valueColor),
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
      return (int16_t)_display->charW(_textSize) * _format->length();
   }

   ///
   /// <summary>
   /// Sets the top-left position at which the value sprite is pushed on the next draw().
   /// </summary>
   /// <param name="x">The X coordinate of the value's top-left corner.</param>
   /// <param name="y">The Y coordinate of the value's top-left corner.</param>
   ///
   void setPosition(int16_t x, int16_t y)
   {
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
   /// Sets the color used to draw the value text on the next draw().
   /// </summary>
   /// <param name="color">The value text color.</param>
   ///
   void setColor(Color color)
   {
      if (color != _valueColor)
      {
         _valueColor = color;
         _dirty = true;
      }
   }

   ///
   /// <summary>
   /// Sets the background color drawn behind the value, e.g. to highlight a currently
   /// selected/editable value. Defaults to Color::BLACK.
   /// </summary>
   /// <param name="color">The background color to draw behind the value.</param>
   ///
   void setBackgroundColor(Color color)
   {
      if (color != _valueBackgroundColor)
      {
         _valueBackgroundColor = color;
         _dirty = true;
      }
   }

   ///
   /// <summary>
   /// Sets the value to display and redraws it via the sprite, but only when the value
   /// text or color changed since the last draw.
   /// </summary>
   /// <param name="value">The new value to display, formatted through the Format object.</param>
   ///
   template <typename T>
   void draw(const T& value)
   {
      _value = _format->toString(value).c_str();

      if (_display == nullptr)
      {
         return;
      }

      if ((_value != _drawnValue) || _dirty)
      {
         _drawSprite();
         _drawnValue = _value;
         _dirty = false;
      }
   }

   ///
   /// <summary>
   /// Forces the value to be redrawn immediately, e.g. after the display area was cleared,
   /// so the next draw() repaints from scratch.
   /// </summary>
   ///
   void invalidate()
   {
      _dirty = true;
   }
};
