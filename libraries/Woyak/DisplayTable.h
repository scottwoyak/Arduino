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
      String drawnValue;
      Color drawnValueColor;

      Row(const char* lbl, const Format* fmt, Color lc, Color vc)
         : label(lbl), format(fmt), value(""), labelColor(lc), valueColor(vc),
           drawnValue(""), drawnValueColor(vc)
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
      _sprite.fillScreen((uint16_t)Color::BLACK);
      _sprite.setTextColor((uint16_t)row.valueColor, (uint16_t)Color::BLACK);
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
   /// Updates the value of an existing row in the table.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="value">The new double value to display.</param>
   /// <param name="valueColor">The color to draw the updated value (default: Color::VALUE).</param>
   ///
   void updateValue(size_t rowIndex, double value, Color valueColor = Color::VALUE)
   {
      if (rowIndex < _rows.size())
      {
         _rows[rowIndex].value = _rows[rowIndex].format->toString(value).c_str();
         _rows[rowIndex].valueColor = valueColor;
      }
   }

   ///
   /// <summary>
   /// Updates the value of an existing row in the table.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="value">The new float value to display.</param>
   /// <param name="valueColor">The color to draw the updated value (default: Color::VALUE).</param>
   ///
   void updateValue(size_t rowIndex, float value, Color valueColor = Color::VALUE)
   {
      if (rowIndex < _rows.size())
      {
         _rows[rowIndex].value = _rows[rowIndex].format->toString(value).c_str();
         _rows[rowIndex].valueColor = valueColor;
      }
   }

   ///
   /// <summary>
   /// Updates the value of an existing row in the table.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="value">The new int value to display.</param>
   /// <param name="valueColor">The color to draw the updated value (default: Color::VALUE).</param>
   ///
   void updateValue(size_t rowIndex, int value, Color valueColor = Color::VALUE)
   {
      if (rowIndex < _rows.size())
      {
         _rows[rowIndex].value = _rows[rowIndex].format->toString(value).c_str();
         _rows[rowIndex].valueColor = valueColor;
      }
   }

   ///
   /// <summary>
   /// Updates the value of an existing row in the table.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="value">The new unsigned long value to display.</param>
   /// <param name="valueColor">The color to draw the updated value (default: Color::VALUE).</param>
   ///
   void updateValue(size_t rowIndex, unsigned long value, Color valueColor = Color::VALUE)
   {
      if (rowIndex < _rows.size())
      {
         _rows[rowIndex].value = _rows[rowIndex].format->toString(value).c_str();
         _rows[rowIndex].valueColor = valueColor;
      }
   }

   ///
   /// <summary>
   /// Updates the value of an existing row in the table.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="value">The new size_t value to display.</param>
   /// <param name="valueColor">The color to draw the updated value (default: Color::VALUE).</param>
   ///
   void updateValue(size_t rowIndex, size_t value, Color valueColor = Color::VALUE)
   {
      if (rowIndex < _rows.size())
      {
         _rows[rowIndex].value = _rows[rowIndex].format->toString(value).c_str();
         _rows[rowIndex].valueColor = valueColor;
      }
   }

   ///
   /// <summary>
   /// Updates the value of an existing row in the table.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="value">The new String value to display.</param>
   /// <param name="valueColor">The color to draw the updated value (default: Color::VALUE).</param>
   ///
   void updateValue(size_t rowIndex, const String& value, Color valueColor = Color::VALUE)
   {
      if (rowIndex < _rows.size())
      {
         _rows[rowIndex].value = _rows[rowIndex].format->toString(value).c_str();
         _rows[rowIndex].valueColor = valueColor;
      }
   }

   ///
   /// <summary>
   /// Updates the value of an existing row in the table.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="value">The new std::string value to display.</param>
   /// <param name="valueColor">The color to draw the updated value (default: Color::VALUE).</param>
   ///
   void updateValue(size_t rowIndex, const std::string& value, Color valueColor = Color::VALUE)
   {
      if (rowIndex < _rows.size())
      {
         _rows[rowIndex].value = _rows[rowIndex].format->toString(value).c_str();
         _rows[rowIndex].valueColor = valueColor;
      }
   }

   ///
   /// <summary>
   /// Updates the value of an existing row in the table.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="value">The new character array value to display.</param>
   /// <param name="valueColor">The color to draw the updated value (default: Color::VALUE).</param>
   ///
   void updateValue(size_t rowIndex, const char* value, Color valueColor = Color::VALUE)
   {
      if (rowIndex < _rows.size())
      {
         _rows[rowIndex].value = _rows[rowIndex].format->toString(value).c_str();
         _rows[rowIndex].valueColor = valueColor;
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

      if (!_labelsDrawn || !_spriteCreated)
      {
         _display->setTextSize(_textSize, _mono);
      }

      _valueX = _x + (_labelWidth * _display->charW());
      int16_t y = _y;

      for (auto& row : _rows)
      {
         if (!_labelsDrawn)
         {
            _display->setCursor(_x, y);

            int16_t labelLen = row.label.length();
            int16_t padding = _labelWidth - labelLen - 2;

            for (int16_t i = 0; i < padding; i++)
            {
               _display->print(" ", row.labelColor);
            }

            _display->print(row.label.c_str(), row.labelColor);
            _display->print(": ", row.labelColor);
            _display->print(row.value, row.valueColor);

            row.drawnValue = row.value;
            row.drawnValueColor = row.valueColor;
         }
         else
         {
            if (!_spriteCreated)
            {
               _createSprite();
            }

            if ((row.value != row.drawnValue) || (row.valueColor != row.drawnValueColor))
            {
               _drawValueSprite(row, y);

               row.drawnValue = row.value;
               row.drawnValueColor = row.valueColor;
            }
         }

         y += _display->charH();
      }

      _labelsDrawn = true;
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
};
