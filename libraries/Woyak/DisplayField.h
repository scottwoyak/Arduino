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
   /// Renders the current value into the off-screen sprite and pushes it over the value
   /// region. The sprite is created lazily on first use, sized to fit the format's fixed
   /// width and the current font height.
   /// </summary>
   ///
   void _drawValueSprite()
   {
      LGFX* display = &_display->display;

      if (!_spriteCreated)
      {
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

      _sprite.fillScreen((uint16_t)Color::BLACK);
      _sprite.setTextColor((uint16_t)_valueColor, (uint16_t)Color::BLACK);
      _sprite.setCursor(0, 0);
      _sprite.print(_value.c_str());
      _sprite.pushSprite(_valueX, _y);
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
   /// Draws the field. The first call renders the full "label: value" string directly to
   /// the display; later calls redraw only the value via a sprite, and only when the value
   /// text or color changed since the last draw.
   /// </summary>
   ///
   void draw()
   {
      if (_display == nullptr)
      {
         return;
      }

      // the display's text size only matters for drawing the label directly and for
      // capturing the font when the sprite is (lazily) created; once the sprite exists,
      // later draws only touch the sprite and don't need the display's text size set
      if (!_labelDrawn || !_spriteCreated)
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

         _display->print(_value.c_str(), _valueColor);

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
   /// Forces the next draw() to redraw the label and value from scratch and rebuild the
   /// value sprite (e.g. after the display area was cleared or the font/size changed).
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
   }
};
