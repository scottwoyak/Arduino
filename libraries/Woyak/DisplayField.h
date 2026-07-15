#pragma once

#include "ArduinoWithDisplay.h"
#include "Format.h"
#include "Color.h"

///
/// <summary>
/// Draws a single "label: value" pair on a display where the value updates frequently.
/// On the first draw(), the full label and value are rendered directly to the display.
/// On subsequent calls, only the value is redrawn, using an off-screen sprite that is
/// pushed over the value region. This avoids reprinting the label and reduces flicker
/// compared to redrawing the whole string every frame. The value is formatted through
/// a Format object, which keeps its rendered width fixed.
/// </summary>
/// <remarks>
/// The value sprite is created in the constructor so it is ready before the first draw().
/// The field stores its own text size and mono flag, applying them automatically at the
/// start of every draw(). The label and position are captured on the first draw() and are
/// assumed to stay constant afterward. Call invalidate() if the display area was cleared,
/// or the text size/font changed via setTextSize(), so the next draw() rebuilds everything.
/// </remarks>
///
class DisplayField
{
private:
   ArduinoWithDisplay* _display;
   int16_t _x;
   int16_t _y;
   String _label;
   const Format* _format;
   Color _labelColor;
   Color _valueColor;
   String _value;
   String _drawnValue;
   Color _drawnValueColor;
   bool _labelDrawn = false;
   int16_t _valueX = 0;
   LGFX_Sprite _sprite;
   bool _spriteCreated = false;
   uint8_t _textSize;
   bool _mono;

   ///
   /// <summary>
   /// Creates the off-screen sprite (if not already created), sized to fit the format's
   /// fixed width and the current font height, and loads the field's font into it.
   /// </summary>
   ///
   void _createSprite()
   {
      if (_spriteCreated)
      {
         return;
      }

      LGFX* display = &_display->display;

      std::string widthSample(_format->length(), '0');
      int16_t spriteWidth = (int16_t)display->textWidth(widthSample.c_str());
      int16_t spriteHeight = (int16_t)display->fontHeight();

      _sprite.setColorDepth(16);
      _sprite.createSprite(spriteWidth, spriteHeight);

      // load our own copy of the font rather than sharing the display's runtime font
      // pointer, which can be freed out from under us if the display later loads a
      // different font (e.g. another DisplayField or the sketch switching modes)
      uint8_t size = constrain(_textSize, (uint8_t)1, (uint8_t)7);
      _sprite.loadFont(_mono ? RobotoMonoBold[size] : Roboto[size]);

      _spriteCreated = true;
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
   /// that varies from value to value (e.g. between two fields with different digit
   /// counts), which pixel-based alignment avoids.
   /// </remarks>
   ///
   void _drawValueSprite()
   {
      _createSprite();

      std::string trimmed = _trimmedValue();
      int16_t textWidth = (int16_t)_sprite.textWidth(trimmed.c_str());
      int16_t spriteWidth = _sprite.width();
      int16_t textX = _alignedTextX(textWidth, spriteWidth);

      _sprite.fillScreen((uint16_t)Color::BLACK);
      _sprite.setTextColor((uint16_t)_valueColor, (uint16_t)Color::BLACK);
      _sprite.setCursor(textX, 0);
      _sprite.print(trimmed.c_str());
      _sprite.pushSprite(_valueX, _y);
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
   /// text of the given pixel width, honoring the field's Format alignment.
   /// </summary>
   /// <param name="textWidth">Measured pixel width of the text to draw.</param>
   /// <param name="totalWidth">Total pixel width of the region to align within.</param>
   /// <returns>X offset for the text's left edge.</returns>
   ///
   int16_t _alignedTextX(int16_t textWidth, int16_t totalWidth) const
   {
      switch (_format->alignment())
      {
      case Format::Alignment::RIGHT:
         return max(static_cast<int16_t>(0), static_cast<int16_t>(totalWidth - textWidth));

      case Format::Alignment::CENTER:
         return max(static_cast<int16_t>(0), static_cast<int16_t>((totalWidth - textWidth) / 2));

      case Format::Alignment::LEFT:
      default:
         return 0;
      }
   }

public:
   ///
   /// <summary>
   /// Initializes a new instance of the DisplayField class.
   /// </summary>
   /// <param name="display">The display interface to draw onto.</param>
   /// <param name="x">The X coordinate of the label's top-left corner.</param>
   /// <param name="y">The Y coordinate of the label's top-left corner.</param>
   /// <param name="label">The label text drawn before the value (a ": " separator is added).
   /// Pass an empty string to draw only the value, with no label or separator.</param>
   /// <param name="format">The formatter applied to the value, controlling its fixed width.</param>
   /// <param name="textSize">The text size applied automatically before each draw().</param>
   /// <param name="mono">If true, uses a monospaced font; if false, uses a proportional font.</param>
   /// <param name="labelColor">The color used to draw the label text.</param>
   /// <param name="valueColor">The color used to draw the value text.</param>
   ///
   DisplayField(ArduinoWithDisplay* display, int16_t x, int16_t y,
                const char* label, const Format& format,
                uint8_t textSize, bool mono = true,
                Color labelColor = Color::LABEL, Color valueColor = Color::VALUE)
      : _display(display), _x(x), _y(y), _label(label), _format(&format),
        _labelColor(labelColor), _valueColor(valueColor), _drawnValueColor(valueColor),
        _sprite(&display->display), _textSize(textSize), _mono(mono)
   {
      _display->setTextSize(_textSize, _mono);
      _createSprite();
   }

   ///
   /// <summary>
   /// Sets the value to display, formatting it through the field's Format object.
   /// </summary>
   /// <param name="value">The new value to display.</param>
   ///
   void setValue(double value)
   {
      _value = _format->toString(value).c_str();
   }

   ///
   /// <summary>
   /// Sets the value to display, formatting it through the field's Format object.
   /// </summary>
   /// <param name="value">The new value to display.</param>
   ///
   void setValue(float value)
   {
      _value = _format->toString(value).c_str();
   }

   ///
   /// <summary>
   /// Sets the value to display, formatting it through the field's Format object.
   /// </summary>
   /// <param name="value">The new value to display.</param>
   ///
   void setValue(int value)
   {
      _value = _format->toString(value).c_str();
   }

   ///
   /// <summary>
   /// Sets the value to display, formatting it through the field's Format object.
   /// </summary>
   /// <param name="value">The new value to display.</param>
   ///
   void setValue(long value)
   {
      _value = _format->toString(value).c_str();
   }

   ///
   /// <summary>
   /// Sets the value to display, formatting it through the field's Format object.
   /// </summary>
   /// <param name="value">The new value to display.</param>
   ///
   void setValue(unsigned long value)
   {
      _value = _format->toString(value).c_str();
   }

   ///
   /// <summary>
   /// Sets the value to display, formatting it through the field's Format object.
   /// </summary>
   /// <param name="value">The new value to display.</param>
   ///
   void setValue(const String& value)
   {
      _value = _format->toString(value).c_str();
   }

   ///
   /// <summary>
   /// Sets the value to display, formatting it through the field's Format object.
   /// </summary>
   /// <param name="value">The new value to display.</param>
   ///
   void setValue(const char* value)
   {
      _value = _format->toString(value).c_str();
   }

   ///
   /// <summary>
   /// Sets the text size (and mono/proportional font mode) applied automatically before
   /// each draw(). Invalidates the field so the next draw() rebuilds the label and sprite
   /// using the new size.
   /// </summary>
   /// <param name="textSize">The text size to apply before drawing.</param>
   /// <param name="mono">If true, uses a monospaced font; if false, uses a proportional font.</param>
   ///
   void setTextSize(uint8_t textSize, bool mono = true)
   {
      if (textSize != _textSize || mono != _mono)
      {
         _textSize = textSize;
         _mono = mono;
         invalidate();
      }
   }

   ///
   /// <summary>
   /// Sets the color used to draw the value text on the next draw().
   /// </summary>
   /// <param name="color">The value text color.</param>
   ///
   void setValueColor(Color color)
   {
      _valueColor = color;
   }

   ///
   /// <summary>
   /// Draws the field. The first call renders the label directly to the display and the
   /// value via the sprite (see _drawValueSprite()); later calls redraw only the value via
   /// the sprite, and only when the value text or color changed since the last draw.
   /// </summary>
   ///
   void draw()
   {
      if (_display == nullptr)
      {
         return;
      }

      // the display's text size only matters for drawing the label directly; the sprite
      // already has its own font loaded and doesn't need the display's text size set
      if (!_labelDrawn)
      {
         _display->setTextSize(_textSize, _mono);
      }

      if (!_labelDrawn)
      {
         _display->setCursor(_x, _y);

         if (_label.length() > 0)
         {
            _display->print(_label.c_str(), _labelColor);
            _display->print(": ", _labelColor);
         }

         _valueX = _display->getCursorX();

         _drawValueSprite();

         _drawnValue = _value;
         _drawnValueColor = _valueColor;
         _labelDrawn = true;
         return;
      }

      if ((_value != _drawnValue) || (_valueColor != _drawnValueColor))
      {
         _drawValueSprite();
         _drawnValue = _value;
         _drawnValueColor = _valueColor;
      }
   }

   ///
   /// <summary>
   /// Forces the next draw() to redraw the label from scratch, and rebuilds the value
   /// sprite immediately (e.g. after the display area was cleared or the font/size changed).
   /// </summary>
   ///
   void invalidate()
   {
      _labelDrawn = false;
      if (_spriteCreated)
      {
         _sprite.unloadFont();
         _sprite.deleteSprite();
         _spriteCreated = false;
      }
      _display->setTextSize(_textSize, _mono);
      _createSprite();
   }
};
