#pragma once

#include "ArduinoWithDisplay.h"
#include "Format.h"
#include "Color.h"
#include <vector>

///
/// <summary>
/// A utility class to display a formatted key-value table on an Arduino display.
/// Allows adding rows with specific formats and updating values dynamimcally.
/// Row values are redrawn through a single shared off-screen sprite, sized to fit the
/// widest formatted value, which avoids reprinting labels and reduces flicker compared
/// to drawing each value directly to the display every frame.
/// </summary>
///
class DisplayTable
{
private:
   struct Row
   {
      String label;
      const Format* format;
      String value;
      Color labelColor;
      Color valueColor;
      Color valueBackgroundColor;
      String drawnValue;
      Color drawnValueColor;
      Color drawnValueBackgroundColor;
      Color drawnLabelColor;
      const char* section = nullptr;

      Row(const char* lbl, const Format* fmt, Color lc, Color vc)
         : label(lbl), format(fmt), value(""), labelColor(lc), valueColor(vc),
           valueBackgroundColor(Color::BLACK), drawnValue(""), drawnValueColor(vc),
           drawnValueBackgroundColor(Color::BLACK), drawnLabelColor(lc)
      {
      }
   };

   std::vector<Row> _rows;
   ArduinoWithDisplay* _display;
   int16_t _x;
   int16_t _y;
   int16_t _labelWidth;
   bool _labelsDrawn = false;
   uint8_t _textSize;
   bool _mono;
   LGFX_Sprite _sprite;
   bool _spriteCreated = false;
   int16_t _valueX = 0;
   int16_t _valueSpriteWidth = 0;
   bool _showSections = true;

   ///
   /// <summary>
   /// Computes the pixel Y offset (relative to _y) of a given row index, accounting for
   /// section header rows and the half-height blank row separating sections. A row starts
   /// a new section - and gets a header row (plus a separating half-row gap, unless it's the
   /// first row) drawn above it - whenever its section text is non-null. Passing _rows.size()
   /// computes the table's total pixel height.
   /// </summary>
   /// <param name="index">Zero-based row index, or _rows.size() for the total height.</param>
   /// <returns>Pixel offset from the table's Y position.</returns>
   ///
   int16_t _rowY(size_t index) const
   {
      int16_t rowHeight = _display->charH();
      int16_t halfRow = rowHeight / 2;
      int16_t y = 0;
      for (size_t i = 0; i < index; i++)
      {
         if (_showSections && _rows[i].section != nullptr)
         {
            if (i > 0)
            {
               y += halfRow;
            }
            y += rowHeight;
         }
         y += rowHeight;
      }

      if (_showSections && index < _rows.size() && _rows[index].section != nullptr)
      {
         if (index > 0)
         {
            y += halfRow;
         }
         y += rowHeight;
      }
      return y;
   }

   ///
   /// <summary>
   /// Creates the shared value sprite (if not already created), sized to fit the widest
   /// row's formatted value and the current font height, then loads a stable copy of the
   /// current font into the sprite so it is not affected by later display font changes.
   /// </summary>
   ///
   void _createSprite()
   {
      if (_spriteCreated)
      {
         return;
      }

      LGFX* display = &_display->display;

      size_t maxLength = 0;
      for (const auto& row : _rows)
      {
         if (row.format->length() > maxLength)
         {
            maxLength = row.format->length();
         }
      }

      std::string widthSample(maxLength, '0');
      _valueSpriteWidth = (int16_t)display->textWidth(widthSample.c_str());
      int16_t spriteHeight = (int16_t)display->fontHeight();

      _sprite.setColorDepth(16);
      _sprite.createSprite(_valueSpriteWidth, spriteHeight);

      // load our own copy of the font rather than sharing the display's runtime font
      // pointer, which can be freed out from under us if the display later loads a
      // different font
      uint8_t size = constrain(_textSize, (uint8_t)1, (uint8_t)7);
      _sprite.loadFont(_mono ? RobotoMonoBold[size] : Roboto[size]);

      _spriteCreated = true;
   }

   ///
   /// <summary>
   /// Renders a row's value into the shared sprite and pushes it over the row's value
   /// region at the given Y coordinate.
   /// </summary>
   ///
   void _drawValueSprite(const Row& row, int16_t y)
   {
      _sprite.fillScreen((uint16_t)row.valueBackgroundColor);
      _sprite.setTextColor((uint16_t)row.valueColor, (uint16_t)row.valueBackgroundColor);
      _sprite.setCursor(0, 0);
      _sprite.print(row.value.c_str());
      _sprite.pushSprite(_valueX, y);
   }

public:
   ///
   /// <summary>
   /// Initializes a new instance of the DisplayTable class.
   /// </summary>
   /// <param name="display">The display interface to draw onto.</param>
   /// <param name="x">The X coordinate for the top-left corner of the table.</param>
   /// <param name="y">The Y coordinate for the top-left corner of the table.</param>
   /// <param name="textSize">The text size applied automatically before drawing labels and values.</param>
   /// <param name="mono">If true, uses a monospaced font; if false, uses a proportional font.</param>
   ///
   DisplayTable(ArduinoWithDisplay* display, int16_t x, int16_t y, uint8_t textSize = 2, bool mono = true)
      : _display(display), _x(x), _y(y), _labelWidth(0),
        _textSize(textSize), _mono(mono), _sprite(&display->display)
   {
   }

   ///
   /// <summary>
   /// Removes all rows from the table and forces the next draw() call to rebuild the value
   /// sprite and redraw everything from scratch, e.g. so addRow() can be called again to
   /// build a completely different set of rows (see DisplayTableEditor::setFields()).
   /// </summary>
   ///
   void clearRows()
   {
      _rows.clear();
      _labelWidth = 0;
      invalidate();
   }

   ///
   /// <summary>
   /// Adds a new row to the table.
   /// </summary>
   /// <param name="label">The text label for the row.</param>
   /// <param name="format">The formatter to apply to the row's value.</param>
   /// <param name="labelColor">The color to draw the label text (default: Color::LABEL).</param>
   /// <param name="valueColor">The color to draw the value text (default: Color::VALUE).</param>
   ///
   void addRow(const char* label, const Format& format, Color labelColor = Color::LABEL, Color valueColor = Color::VALUE)
   {
      _rows.emplace_back(label, &format, labelColor, valueColor);
      int16_t labelLen = strlen(label) + 2;
      if (labelLen > _labelWidth)
      {
         _labelWidth = labelLen;
      }
   }

   ///
   /// <summary>
   /// Sets the section header text drawn above a row, e.g. "Plot" or "Measured". A row with
   /// no section set (the default) is drawn as part of the previous row's section. Set
   /// showSections(false) to suppress drawing/reserving space for section headers entirely.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="section">Section header text, or nullptr for no header.</param>
   ///
   void setSection(size_t rowIndex, const char* section)
   {
      if (rowIndex < _rows.size())
      {
         _rows[rowIndex].section = section;
         _labelsDrawn = false;
      }
   }

   ///
   /// <summary>
   /// Sets whether section header rows (see setSection()) are drawn and reserved space for.
   /// Defaults to true.
   /// </summary>
   /// <param name="showSections">True to draw section headers, false to suppress them.</param>
   ///
   void setShowSections(bool showSections)
   {
      _showSections = showSections;
      _labelsDrawn = false;
   }

   ///
   /// <summary>
   /// Sets the value of an existing row in the table.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="value">The new double value to display.</param>
   /// <param name="valueColor">The color to draw the updated value (default: Color::VALUE).</param>
   ///
   void setValue(size_t rowIndex, double value, Color valueColor = Color::VALUE)
   {
      if (rowIndex < _rows.size())
      {
         _rows[rowIndex].value = _rows[rowIndex].format->toString(value).c_str();
         _rows[rowIndex].valueColor = valueColor;
      }
   }

   ///
   /// <summary>
   /// Sets the value of an existing row in the table.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="value">The new float value to display.</param>
   /// <param name="valueColor">The color to draw the updated value (default: Color::VALUE).</param>
   ///
   void setValue(size_t rowIndex, float value, Color valueColor = Color::VALUE)
   {
      if (rowIndex < _rows.size())
      {
         _rows[rowIndex].value = _rows[rowIndex].format->toString(value).c_str();
         _rows[rowIndex].valueColor = valueColor;
      }
   }

   ///
   /// <summary>
   /// Sets the value of an existing row in the table.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="value">The new int value to display.</param>
   /// <param name="valueColor">The color to draw the updated value (default: Color::VALUE).</param>
   ///
   void setValue(size_t rowIndex, int value, Color valueColor = Color::VALUE)
   {
      if (rowIndex < _rows.size())
      {
         _rows[rowIndex].value = _rows[rowIndex].format->toString(value).c_str();
         _rows[rowIndex].valueColor = valueColor;
      }
   }

   ///
   /// <summary>
   /// Sets the value of an existing row in the table.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="value">The new unsigned long value to display.</param>
   /// <param name="valueColor">The color to draw the updated value (default: Color::VALUE).</param>
   ///
   void setValue(size_t rowIndex, unsigned long value, Color valueColor = Color::VALUE)
   {
      if (rowIndex < _rows.size())
      {
         _rows[rowIndex].value = _rows[rowIndex].format->toString(value).c_str();
         _rows[rowIndex].valueColor = valueColor;
      }
   }

   ///
   /// <summary>
   /// Sets the value of an existing row in the table.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="value">The new size_t value to display.</param>
   /// <param name="valueColor">The color to draw the updated value (default: Color::VALUE).</param>
   ///
   void setValue(size_t rowIndex, size_t value, Color valueColor = Color::VALUE)
   {
      if (rowIndex < _rows.size())
      {
         _rows[rowIndex].value = _rows[rowIndex].format->toString(value).c_str();
         _rows[rowIndex].valueColor = valueColor;
      }
   }

   ///
   /// <summary>
   /// Sets the value of an existing row in the table.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="value">The new String value to display.</param>
   /// <param name="valueColor">The color to draw the updated value (default: Color::VALUE).</param>
   ///
   void setValue(size_t rowIndex, const String& value, Color valueColor = Color::VALUE)
   {
      if (rowIndex < _rows.size())
      {
         _rows[rowIndex].value = _rows[rowIndex].format->toString(value).c_str();
         _rows[rowIndex].valueColor = valueColor;
      }
   }

   ///
   /// <summary>
   /// Sets the value of an existing row in the table.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="value">The new std::string value to display.</param>
   /// <param name="valueColor">The color to draw the updated value (default: Color::VALUE).</param>
   ///
   void setValue(size_t rowIndex, const std::string& value, Color valueColor = Color::VALUE)
   {
      if (rowIndex < _rows.size())
      {
         _rows[rowIndex].value = _rows[rowIndex].format->toString(value).c_str();
         _rows[rowIndex].valueColor = valueColor;
      }
   }

   ///
   /// <summary>
   /// Sets the value of an existing row in the table.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="value">The new character array value to display.</param>
   /// <param name="valueColor">The color to draw the updated value (default: Color::VALUE).</param>
   ///
   void setValue(size_t rowIndex, const char* value, Color valueColor = Color::VALUE)
   {
      if (rowIndex < _rows.size())
      {
         _rows[rowIndex].value = _rows[rowIndex].format->toString(value).c_str();
         _rows[rowIndex].valueColor = valueColor;
      }
   }

   ///
   /// <summary>
   /// Sets a row's value to a placeholder string (via the row's Format::toNoValueString())
   /// to indicate no value is currently available, as opposed to a legitimate NaN reading.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="valueColor">The color to draw the placeholder value (default: Color::VALUE).</param>
   /// <param name="noValueChar">Character used to fill each digit position.</param>
   ///
   void setNoValue(size_t rowIndex, Color valueColor = Color::VALUE, char noValueChar = '-')
   {
      if (rowIndex < _rows.size())
      {
         _rows[rowIndex].value = _rows[rowIndex].format->toNoValueString(noValueChar).c_str();
         _rows[rowIndex].valueColor = valueColor;
      }
   }

   ///
   /// <summary>
   /// Sets the background color drawn behind a row's value, e.g. to highlight a currently
   /// selected/editable row. Defaults to Color::BLACK for every row.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="valueBackgroundColor">The background color to draw behind the value.</param>
   ///
   void setValueBackgroundColor(size_t rowIndex, Color valueBackgroundColor)
   {
      if (rowIndex < _rows.size())
      {
         _rows[rowIndex].valueBackgroundColor = valueBackgroundColor;
      }
   }

   ///
   /// <summary>
   /// Sets the color drawn for a row's label, e.g. to dim a disabled row's label. Triggers a
   /// redraw of that row's label on the next draw() call if the color changed.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="labelColor">The color to draw the label text.</param>
   ///
   void setLabelColor(size_t rowIndex, Color labelColor)
   {
      if (rowIndex < _rows.size())
      {
         _rows[rowIndex].labelColor = labelColor;
      }
   }

   ///
   /// <summary>
   /// Draws the table onto the configured display. On the first call (or after
   /// invalidate()), draws labels and values for every row; on subsequent calls, only
   /// redraws the value of a row when its text or color has changed, since labels never
   /// change and formatted values share a fixed width, avoiding unnecessary display writes.
   /// </summary>
   ///
   void draw()
   {
      if (_display == nullptr)
      {
         return;
      }

      if (_display->getTextSize() != _textSize)
      {
         _display->setTextSize(_textSize, _mono);
      }

      _valueX = _x + (_labelWidth * _display->charW());

      for (size_t i = 0; i < _rows.size(); i++)
      {
         Row& row = _rows[i];
         int16_t y = _y + _rowY(i);

         if (!_spriteCreated)
         {
            _createSprite();
         }

         bool labelNeedsDraw = !_labelsDrawn || (row.labelColor != row.drawnLabelColor);

         if (labelNeedsDraw)
         {
            if (_showSections && row.section != nullptr && !_labelsDrawn)
            {
               int16_t rowHeight = _display->charH();
               _display->setCursor(_x, y - rowHeight);
               _display->println(row.section, Color::SUB_HEADING, Color::BLACK);
            }

            _display->setCursor(_x, y);

            int16_t labelLen = row.label.length();
            int16_t padding = _labelWidth - labelLen - 2;

            for (int16_t p = 0; p < padding; p++)
            {
               _display->print(" ", row.labelColor);
            }

            _display->print(row.label.c_str(), row.labelColor);
            _display->print(": ", row.labelColor);

            row.drawnLabelColor = row.labelColor;
         }

         if (!_labelsDrawn)
         {
            _drawValueSprite(row, y);

            row.drawnValue = row.value;
            row.drawnValueColor = row.valueColor;
            row.drawnValueBackgroundColor = row.valueBackgroundColor;
         }
         else
         {
            if ((row.value != row.drawnValue) || (row.valueColor != row.drawnValueColor) ||
               (row.valueBackgroundColor != row.drawnValueBackgroundColor))
            {
               _drawValueSprite(row, y);

               row.drawnValue = row.value;
               row.drawnValueColor = row.valueColor;
               row.drawnValueBackgroundColor = row.valueBackgroundColor;
            }
         }
      }

            _labelsDrawn = true;
         }

   ///
   /// <summary>
   /// Moves the table to a new top-left position and forces the next draw() call to
   /// redraw every label and value from scratch at the new location.
   /// </summary>
   /// <param name="x">The new X coordinate for the top-left corner of the table.</param>
   /// <param name="y">The new Y coordinate for the top-left corner of the table.</param>
   ///
   void setPosition(int16_t x, int16_t y)
   {
      _x = x;
      _y = y;
      _labelsDrawn = false;
   }

   ///
   /// <summary>
   /// Moves the table to a new top-left position and forces the next draw() call to
   /// redraw every label and value from scratch at the new location.
   /// </summary>
   /// <param name="pos">The new top-left coordinate of the table.</param>
   ///
   void setPosition(Point16 pos)
   {
      setPosition(pos.x, pos.y);
   }

   ///
   /// <summary>
   /// Computes the table's bounding rectangle, based on its top-left position, its
   /// content width (see getWidth()), and the total height of all its rows. Useful for
   /// laying out other content (e.g. a value field) relative to the table's actual bounds.
   /// </summary>
   /// <returns>The table's bounding rectangle, in pixels.</returns>
   ///
   Rect16 getRect()
   {
      int16_t width = getWidth();
      int16_t height = _rowY(_rows.size());
      return Rect16{ (uint16_t)_x, (uint16_t)_y, (uint16_t)width, (uint16_t)height };
   }

   ///
   /// <summary>
   /// Forces the next draw() call to redraw every label and value from scratch and rebuild
   /// the shared value sprite (e.g. after the display area was cleared, the table's
   /// position changed, or the font/size changed).
   /// </summary>
   ///
   void invalidate()
   {
      _labelsDrawn = false;
      if (_spriteCreated)
      {
         _sprite.unloadFont();
         _sprite.deleteSprite();
         _spriteCreated = false;
      }
   }

   ///
   /// <summary>
   /// Computes the total pixel width the table needs to display its labels and values,
   /// based on the longest label and the widest formatted value across all rows. Useful
   /// for laying out other content (e.g. a plot) relative to the table's actual content
   /// width rather than a guessed fixed width.
   /// </summary>
   /// <returns>The required width, in pixels.</returns>
   ///
   int16_t getWidth()
   {
      if (!_spriteCreated)
      {
         _display->setTextSize(_textSize, _mono);
         _createSprite();
      }

      return (int16_t)(_labelWidth * _display->charW()) + _valueSpriteWidth;
   }
   };
