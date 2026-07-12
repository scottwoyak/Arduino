#pragma once

#include "ArduinoWithDisplay.h"
#include "Format.h"
#include "Color.h"
#include <vector>

///
/// <summary>
/// A utility class to display a formatted key-value table on an Arduino display.
/// Allows adding rows with specific formats and updating values dynamimcally.
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

public:
   ///
   /// <summary>
   /// Initializes a new instance of the DisplayTable class.
   /// </summary>
   /// <param name="display">The display interface to draw onto.</param>
   /// <param name="x">The X coordinate for the top-left corner of the table.</param>
   /// <param name="y">The Y coordinate for the top-left corner of the table.</param>
   ///
   DisplayTable(ArduinoWithDisplay* display, int16_t x, int16_t y)
      : _display(display), _x(x), _y(y), _labelWidth(0)
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
            _display->println();

            row.drawnValue = row.value;
            row.drawnValueColor = row.valueColor;
         }
         else if ((row.value != row.drawnValue) || (row.valueColor != row.drawnValueColor))
         {
            int16_t valueX = _x + (_labelWidth * _display->charW());
            _display->setCursor(valueX, y);
            _display->print(row.value, row.valueColor);

            row.drawnValue = row.value;
            row.drawnValueColor = row.valueColor;
         }

         y += _display->charH();
      }

      _labelsDrawn = true;
   }

   ///
   /// <summary>
   /// Forces the next draw() call to redraw every label and value from scratch (e.g.
   /// after the display area was cleared or the table's position changed).
   /// </summary>
   ///
   void invalidate()
   {
      _labelsDrawn = false;
   }
};
