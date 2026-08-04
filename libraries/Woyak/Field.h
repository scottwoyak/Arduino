#pragma once

#include "ArduinoWithDisplay.h"
#include "DisplayValue.h"
#include "Format.h"
#include "Color.h"
#include "Util.h"

///
/// <summary>
/// Draws a single "label value" pair on a display where the value updates frequently.
/// On the first draw(), the full label and value are rendered directly to the display.
/// On subsequent calls, only the value is redrawn, via a DisplayValue that keeps its own
/// off-screen sprite pushed over the value region. This avoids reprinting the label and
/// reduces flicker compared to redrawing the whole string every frame. The value is
/// formatted through a format string, which keeps its rendered width fixed.
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
class Field
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
      GAP     // pos.x is the position of the gap separating label and value
   };

private:
   ArduinoWithDisplay* _display;
   int16_t _x;
   int16_t _y;
   String _label;
   bool _labelDirty = true;
   uint8_t _textSize;
   Alignment _alignment;
   DisplayValue _value;

public:
   ///
   /// <summary>
   /// Initializes a new instance of the Field class.
   /// </summary>
   /// <param name="display">The display interface to draw onto.</param>
   /// <param name="pos">The X/Y coordinate of the label's top-left corner.</param>
   /// <param name="label">The label text drawn before the value (a gap separator is added).
   /// Pass an empty string to draw only the value, with no label or separator.</param>
   /// <param name="formatStr">The format pattern applied to the value, controlling its fixed width.</param>
   /// <param name="textSize">The font size used to draw the label and value text.</param>
   /// <param name="alignment">Controls whether x is the field's left edge (LEFT), right
   /// edge (RIGHT), or the position of the gap separating label and value (GAP).</param>
   ///
   Field(ArduinoWithDisplay* display, Point16 pos,
                const char* label, const char* formatStr, uint8_t textSize,
                Alignment alignment = Alignment::LEFT)
      : _display(display), _label(label),
        _textSize(textSize), _alignment(alignment),
        _value(display, Format(formatStr), _textSize)
   {
      setPosition(pos);
   }

   ///
   /// <summary>
   /// Initializes a new instance of the Field class with no label, drawing only the value.
   /// </summary>
   /// <param name="display">The display interface to draw onto.</param>
   /// <param name="pos">The X/Y coordinate of the value's top-left corner.</param>
   /// <param name="formatStr">The format pattern applied to the value, controlling its fixed width.</param>
   /// <param name="textSize">The font size used to draw the value text.</param>
   /// <param name="alignment">Controls whether x is the field's left edge (LEFT), right
   /// edge (RIGHT), or the position of the gap separating label and value (GAP);
   /// GAP has no effect when there is no label.</param>
   ///
   Field(ArduinoWithDisplay* display, Point16 pos,
                const char* formatStr, uint8_t textSize,
                Alignment alignment = Alignment::LEFT)
      : Field(display, pos, "", formatStr, textSize, alignment)
   {
   }

   ///
   /// <summary>
   /// Initializes a new instance of the Field class without specifying a position,
   /// e.g. so a global/member field can be constructed before its final layout is known.
   /// Call setPosition() once the position is known.
   /// </summary>
   /// <param name="display">The display interface to draw onto.</param>
   /// <param name="label">The label text drawn before the value (a gap separator is added).
   /// Pass an empty string to draw only the value, with no label or separator.</param>
   /// <param name="formatStr">The format pattern applied to the value, controlling its fixed width.</param>
   /// <param name="textSize">The font size used to draw the label and value text.</param>
   /// <param name="alignment">Controls whether x is the field's left edge (LEFT), right
   /// edge (RIGHT), or the position of the gap separating label and value (GAP).</param>
   ///
   Field(ArduinoWithDisplay* display,
                const char* label, const char* formatStr, uint8_t textSize,
                Alignment alignment = Alignment::LEFT)
      : Field(display, Point16(0, 0), label, formatStr, textSize, alignment)
   {
   }

   ///
   /// <summary>
   /// Initializes a new instance of the Field class without specifying a position, using
   /// an already-parsed Format. Used internally by FieldTable/FieldTableEditor, which own
   /// parsed Format instances (e.g. from a ValueBase) rather than format strings.
   /// </summary>
   /// <param name="display">The display interface to draw onto.</param>
   /// <param name="label">The label text drawn before the value (a gap separator is added).
   /// Pass an empty string to draw only the value, with no label or separator.</param>
   /// <param name="format">The already-parsed formatter applied to the value.</param>
   /// <param name="textSize">The font size used to draw the label and value text.</param>
   /// <param name="alignment">Controls whether x is the field's left edge (LEFT), right
   /// edge (RIGHT), or the position of the gap separating label and value (GAP).</param>
   ///
   Field(ArduinoWithDisplay* display,
                const char* label, const Format& format, uint8_t textSize,
                Alignment alignment = Alignment::LEFT)
      : _display(display), _label(label),
        _textSize(textSize), _alignment(alignment),
        _value(display, format, _textSize)
   {
      setPosition(Point16(0, 0));
   }

   ///
   /// <summary>
   /// Initializes a new instance of the Field class using an already-parsed Format. Used
   /// internally for formats that cannot be expressed as a pattern string (e.g. a Format
   /// constructed from an explicit length rather than a pattern).
   /// </summary>
   /// <param name="display">The display interface to draw onto.</param>
   /// <param name="pos">The X/Y coordinate of the label's top-left corner.</param>
   /// <param name="label">The label text drawn before the value (a gap separator is added).
   /// Pass an empty string to draw only the value, with no label or separator.</param>
   /// <param name="format">The already-parsed formatter applied to the value.</param>
   /// <param name="textSize">The font size used to draw the label and value text.</param>
   /// <param name="alignment">Controls whether x is the field's left edge (LEFT), right
   /// edge (RIGHT), or the position of the gap separating label and value (GAP).</param>
   ///
   Field(ArduinoWithDisplay* display, Point16 pos,
                const char* label, const Format& format, uint8_t textSize,
                Alignment alignment = Alignment::LEFT)
      : _display(display), _label(label),
        _textSize(textSize), _alignment(alignment),
        _value(display, format, _textSize)
   {
      setPosition(pos);
   }

   ///
   /// <summary>
   /// Initializes a new instance of the Field class with no label, using an already-parsed
   /// Format. Used internally for formats that cannot be expressed as a pattern string
   /// (e.g. a Format constructed from an explicit length rather than a pattern).
   /// </summary>
   /// <param name="display">The display interface to draw onto.</param>
   /// <param name="pos">The X/Y coordinate of the value's top-left corner.</param>
   /// <param name="format">The already-parsed formatter applied to the value.</param>
   /// <param name="textSize">The font size used to draw the value text.</param>
   /// <param name="alignment">Controls whether x is the field's left edge (LEFT), right
   /// edge (RIGHT), or the position of the gap separating label and value (GAP);
   /// GAP has no effect when there is no label.</param>
   ///
   Field(ArduinoWithDisplay* display, Point16 pos,
                const Format& format, uint8_t textSize,
                Alignment alignment = Alignment::LEFT)
      : _display(display), _label(""),
        _textSize(textSize), _alignment(alignment),
        _value(display, format, _textSize)
   {
      setPosition(pos);
   }

   ///
   /// <summary>
   /// Sets the field's position, recomputing the label/value layout for the field's
   /// alignment. Forces the next draw() to redraw the label from scratch.
   /// </summary>
   /// <param name="x">The X coordinate of the label's top-left corner; negative values offset from the right edge.</param>
   /// <param name="y">The Y coordinate of the label's top-left corner; negative values offset from the bottom edge.</param>
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

      _display->normalizeCoords(x, y);

      // the label/value widths below must be measured using the field's own text size,
      // not whatever size the display happens to currently have set (e.g. from drawing
      // a differently-sized heading earlier)
      _display->setTextSize(_textSize, true);

      if (_alignment == Alignment::RIGHT)
      {
         int16_t totalWidth = _value.width();
         if (_label.length() > 0)
         {
            size_t labelWithSepLen = _label.length() + 1; // + gap
            totalWidth += (int16_t)(labelWithSepLen * _display->charW(_textSize));
         }
         x = x - totalWidth;
      }
      else if (_alignment == Alignment::GAP && _label.length() > 0)
      {
         int16_t labelWidth = (int16_t)(_label.length() * _display->charW(_textSize));
         x = x - labelWidth;
      }

      _x = x;
      _y = y;
      _value.setPosition(_x, _y);
      _labelDirty = true;
   }

   ///
   /// <summary>
   /// Gets the field's label text, e.g. so a caller pairing this Field with some other
   /// name-keyed data (like FieldEditor::FieldInfo) doesn't have to restate it separately.
   /// </summary>
   /// <returns>The label text passed to the constructor.</returns>
   ///
   const char* label() const
   {
      return _label.c_str();
   }

   ///
   /// <summary>
   /// Sets the value to display and draws the field. The first call renders the label
   /// directly to the display and the value via the DisplayValue's sprite; later calls
   /// redraw only the value.
   /// </summary>
   /// <param name="value">The new value to display, formatted through the field's Format object.</param>
   /// <param name="labelColor">The color used to draw the label text; only applies the first
   /// time the label is drawn (or after invalidate()), since the label is not redrawn on
   /// subsequent calls.</param>
   /// <param name="valueColor">The color used to draw the value text.</param>
   /// <param name="backgroundColor">The background color drawn behind the value.</param>
   ///
   template <typename T>
   void draw(const T& value, Color labelColor = Color::LABEL, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      ASSERT(_display != nullptr);

      // the display's text size only matters for drawing the label directly; the value's
      // sprite already has its own font loaded and doesn't need the display's text size set
      _display->setTextSize(_textSize, true);

      if (_labelDirty)
      {
         _display->setCursor(_x, _y);

         if (_label.length() > 0)
         {
            _display->print(_label.c_str(), labelColor);
            _display->print(" ", labelColor);
         }

         _value.setPosition(_display->getCursorX(), _y);

         _labelDirty = false;
      }

      _value.draw(value, valueColor, backgroundColor);
   }

   ///
   /// <summary>
   /// Returns the pixel width of the value's fixed-width sprite, e.g. so a caller (such
   /// as FieldTable) can lay out other content relative to the field's overall width.
   /// </summary>
   /// <returns>The value sprite's width in pixels.</returns>
   ///
   int16_t valueWidth() const
   {
      return _value.width();
   }

   ///
   /// <summary>
   /// Blanks the value region by filling its sprite with the given color and pushing it
   /// directly, leaving the label (if already drawn) untouched. Cheaper than
   /// draw("") since it skips measuring/aligning text.
   /// </summary>
   /// <param name="backgroundColor">The color painted over the value region.</param>
   ///
   void clear(Color backgroundColor = Color::BLACK)
   {
      _value.clear(backgroundColor);
   }

   ///
   /// <summary>
   /// Forces the next draw() to redraw the label from scratch, and rebuilds the value
   /// sprite immediately (e.g. after the display area was cleared or the font/size changed).
   /// </summary>
   ///
   void invalidate()
   {
      _labelDirty = true;
      _display->setTextSize(_textSize, true);
   }
};
