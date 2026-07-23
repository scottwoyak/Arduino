#pragma once

#include "ArduinoWithDisplay.h"
#include "DisplayValue.h"
#include "Format.h"
#include "Color.h"

///
/// <summary>
/// Draws a single "label: value" pair on a display where the value updates frequently.
/// On the first draw(), the full label and value are rendered directly to the display.
/// On subsequent calls, only the value is redrawn, via a DisplayValue that keeps its own
/// off-screen sprite pushed over the value region. This avoids reprinting the label and
/// reduces flicker compared to redrawing the whole string every frame. The value is
/// formatted through a Format object, which keeps its rendered width fixed.
/// </summary>
/// <remarks>
/// The value's DisplayValue is created in the constructor so it is ready before the
/// first draw(). The field stores its own text size, applying it automatically at the
/// start of every draw(). The position is set via the constructor or setPosition() and
/// is assumed to stay constant until changed. Call invalidate() if the display area was
/// cleared, or the text size/font changed via setTextSize(), so the next draw() rebuilds
/// everything.
/// </remarks>
///
class DisplayField
{
public:
   ///
   /// <summary>
   /// Controls how the field's pos X coordinate is interpreted.
   /// </summary>
   ///
   enum class Alignment
   {
      LEFT,   // pos.x is the label's left edge
      RIGHT,  // pos.x is the value's right edge
      COLON   // pos.x is the position of the ':' character separating label and value
   };

private:
   ArduinoWithDisplay* _display;
   int16_t _x;
   int16_t _y;
   String _label;
   Color _labelColor;
   Color _drawnLabelColor;
   bool _labelDrawn = false;
   uint8_t _textSize;
   Alignment _alignment;
   DisplayValue _value;

public:
   ///
   /// <summary>
   /// Initializes a new instance of the DisplayField class.
   /// </summary>
   /// <param name="display">The display interface to draw onto.</param>
   /// <param name="pos">The X/Y coordinate of the label's top-left corner.</param>
   /// <param name="label">The label text drawn before the value (a ": " separator is added).
   /// Pass an empty string to draw only the value, with no label or separator.</param>
   /// <param name="format">The formatter applied to the value, controlling its fixed width.</param>
   /// <param name="textSize">The font size used to draw the label and value text.</param>
   /// <param name="labelColor">The color used to draw the label text.</param>
   /// <param name="valueColor">The color used to draw the value text.</param>
   /// <param name="alignment">Controls whether x is the field's left edge (LEFT), right
   /// edge (RIGHT), or the position of the ':' character separating label and value (COLON).</param>
   ///
   DisplayField(ArduinoWithDisplay* display, Point16 pos,
                const char* label, const Format& format, uint8_t textSize,
                Color labelColor = Color::LABEL, Color valueColor = Color::VALUE,
                Alignment alignment = Alignment::LEFT)
      : _display(display), _label(label),
        _labelColor(labelColor), _drawnLabelColor(labelColor),
        _textSize(textSize), _alignment(alignment),
        _value(display, format, _textSize, valueColor)
   {
      setPosition(pos);
   }

   ///
   /// <summary>
   /// Initializes a new instance of the DisplayField class using the default label and
   /// value colors, so an alignment can be specified without also specifying colors.
   /// </summary>
   /// <param name="display">The display interface to draw onto.</param>
   /// <param name="pos">The X/Y coordinate of the label's top-left corner.</param>
   /// <param name="label">The label text drawn before the value (a ": " separator is added).
   /// Pass an empty string to draw only the value, with no label or separator.</param>
   /// <param name="format">The formatter applied to the value, controlling its fixed width.</param>
   /// <param name="textSize">The font size used to draw the label and value text.</param>
   /// <param name="alignment">Controls whether x is the field's left edge (LEFT), right
   /// edge (RIGHT), or the position of the ':' character separating label and value (COLON).</param>
   ///
   DisplayField(ArduinoWithDisplay* display, Point16 pos,
                const char* label, const Format& format, uint8_t textSize,
                Alignment alignment)
      : DisplayField(display, pos, label, format, textSize, Color::LABEL, Color::VALUE, alignment)
   {
   }

   ///
   /// <summary>
   /// Initializes a new instance of the DisplayField class with no label, drawing only the value.
   /// </summary>
   /// <param name="display">The display interface to draw onto.</param>
   /// <param name="pos">The X/Y coordinate of the value's top-left corner.</param>
   /// <param name="format">The formatter applied to the value, controlling its fixed width.</param>
   /// <param name="textSize">The font size used to draw the value text.</param>
   /// <param name="valueColor">The color used to draw the value text.</param>
   /// <param name="alignment">Controls whether x is the field's left edge (LEFT), right
   /// edge (RIGHT), or the position of the ':' character separating label and value (COLON);
   /// COLON has no effect when there is no label.</param>
   ///
   DisplayField(ArduinoWithDisplay* display, Point16 pos,
                const Format& format, uint8_t textSize,
                Color valueColor = Color::VALUE,
                Alignment alignment = Alignment::LEFT)
      : DisplayField(display, pos, "", format, textSize, Color::LABEL, valueColor, alignment)
   {
   }

   ///
   /// <summary>
   /// Initializes a new instance of the DisplayField class without specifying a position,
   /// e.g. so a global/member field can be constructed before its final layout is known.
   /// Call setPosition() once the position is known.
   /// </summary>
   /// <param name="display">The display interface to draw onto.</param>
   /// <param name="label">The label text drawn before the value (a ": " separator is added).
   /// Pass an empty string to draw only the value, with no label or separator.</param>
   /// <param name="format">The formatter applied to the value, controlling its fixed width.</param>
   /// <param name="textSize">The font size used to draw the label and value text.</param>
   /// <param name="labelColor">The color used to draw the label text.</param>
   /// <param name="valueColor">The color used to draw the value text.</param>
   /// <param name="alignment">Controls whether x is the field's left edge (LEFT), right
   /// edge (RIGHT), or the position of the ':' character separating label and value (COLON).</param>
   ///
   DisplayField(ArduinoWithDisplay* display,
                const char* label, const Format& format, uint8_t textSize,
                Color labelColor = Color::LABEL, Color valueColor = Color::VALUE,
                Alignment alignment = Alignment::LEFT)
      : DisplayField(display, Point16(0, 0), label, format, textSize, labelColor, valueColor, alignment)
   {
   }

   ///
   /// <summary>
   /// Initializes a new instance of the DisplayField class without specifying a position,
   /// using the default label and value colors, so an alignment can be specified without
   /// also specifying colors. Call setPosition() once the position is known.
   /// </summary>
   /// <param name="display">The display interface to draw onto.</param>
   /// <param name="label">The label text drawn before the value (a ": " separator is added).
   /// Pass an empty string to draw only the value, with no label or separator.</param>
   /// <param name="format">The formatter applied to the value, controlling its fixed width.</param>
   /// <param name="textSize">The font size used to draw the label and value text.</param>
   /// <param name="alignment">Controls whether x is the field's left edge (LEFT), right
   /// edge (RIGHT), or the position of the ':' character separating label and value (COLON).</param>
   ///
   DisplayField(ArduinoWithDisplay* display,
                const char* label, const Format& format, uint8_t textSize,
                Alignment alignment)
      : DisplayField(display, Point16(0, 0), label, format, textSize, Color::LABEL, Color::VALUE, alignment)
   {
   }

   ///
   /// <summary>
   /// Sets the field's position, recomputing the label/value layout for the field's
   /// alignment. Forces the next draw() to redraw the label from scratch.
   /// </summary>
   /// <param name="x">The X coordinate of the label's top-left corner.</param>
   /// <param name="y">The Y coordinate of the label's top-left corner.</param>
   ///
   void setPosition(int16_t x, int16_t y)
   {
      setPosition(Point16(x, y));
   }

   ///
   /// <summary>
   /// Sets the field's position, recomputing the label/value layout for the field's
   /// alignment. Forces the next draw() to redraw the label from scratch.
   /// </summary>
   /// <param name="pos">The X/Y coordinate of the label's top-left corner.</param>
   ///
   void setPosition(Point16 pos)
   {
      int16_t x = pos.x;
      int16_t y = pos.y;

      if (y < 0)
      {
         y = _display->display.height() + y;
      }

      if (_alignment == Alignment::RIGHT)
      {
         int16_t totalWidth = _value.width();
         if (_label.length() > 0)
         {
            std::string labelWithSep = std::string(_label.c_str()) + ": ";
            totalWidth += (int16_t)_display->display.textWidth(labelWithSep.c_str());
         }
         x = x - totalWidth;
      }
      else if (_alignment == Alignment::COLON && _label.length() > 0)
      {
         int16_t labelWidth = (int16_t)_display->display.textWidth(_label.c_str());
         x = x - labelWidth;
      }

      _x = x;
      _y = y;
      _value.setPosition(_x, _y);
      _labelDrawn = false;
   }

   ///
   /// <summary>
   /// Sets the color used to draw the value text on the next draw().
   /// </summary>
   /// <param name="color">The value text color.</param>
   ///
   void setValueColor(Color color)
   {
      _value.setColor(color);
   }

   ///
   /// <summary>
   /// Sets the color used to draw the label text on the next draw().
   /// </summary>
   /// <param name="color">The label text color.</param>
   ///
   void setLabelColor(Color color)
   {
      _labelColor = color;
   }

   ///
   /// <summary>
   /// Sets the background color drawn behind the value, e.g. to highlight a currently
   /// selected/editable field. Defaults to Color::BLACK.
   /// </summary>
   /// <param name="color">The background color to draw behind the value.</param>
   ///
   void setValueBackgroundColor(Color color)
   {
      _value.setBackgroundColor(color);
   }

   ///
   /// <summary>
   /// Sets the value to display and draws the field. The first call renders the label
   /// directly to the display and the value via the DisplayValue's sprite; later calls
   /// redraw only the value, and only when the value text or color changed since the
   /// last draw.
   /// </summary>
   /// <param name="value">The new value to display, formatted through the field's Format object.</param>
   ///
   template <typename T>
   void draw(const T& value)
   {
      if (_display == nullptr)
      {
         return;
      }

      // the display's text size only matters for drawing the label directly; the value's
      // sprite already has its own font loaded and doesn't need the display's text size set
      _display->setTextSize(_textSize, true);

      if (!_labelDrawn)
      {
         _display->setCursor(_x, _y);

         if (_label.length() > 0)
         {
            _display->print(_label.c_str(), _labelColor);
            _display->print(": ", _labelColor);
         }

         _value.setPosition(_display->getCursorX(), _y);

         _drawnLabelColor = _labelColor;
         _labelDrawn = true;
      }
      else if (_labelColor != _drawnLabelColor)
      {
         _display->setCursor(_x, _y);

         if (_label.length() > 0)
         {
            _display->print(_label.c_str(), _labelColor);
            _display->print(": ", _labelColor);
         }

         _drawnLabelColor = _labelColor;
      }

      _value.draw(value);
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
      _display->setTextSize(_textSize, true);
      _value.invalidate();
   }
};
