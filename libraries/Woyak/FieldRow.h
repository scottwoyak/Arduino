#pragma once

#include <span>
#include <vector>

#include "ColorX.h"
#include "Field.h"
#include "Util.h"

///
/// <summary>
/// Lays out and manages a horizontal strip of inline "Label Value" Field widgets on a
/// single row: the first field is left-aligned at the row's left edge, the last field is
/// right-aligned against the row's right edge, and any interior fields are spaced with
/// equal horizontal gaps between neighbors.
/// </summary>
///
class FieldRow
{
private:
   std::vector<Field*> _fields;
   Color _labelColor = Color::LABEL;

public:
   ///
   /// <summary>
   /// Initializes a horizontal row containing the given span of non-null Field pointers.
   /// </summary>
   /// <param name="fields">Fields to arrange left-to-right across the row.</param>
   /// <param name="labelColor">Default label color used when drawing fields through this row.</param>
   ///
   FieldRow(std::span<Field* const> fields, Color labelColor = Color::LABEL)
      : _fields(fields.begin(), fields.end()),
        _labelColor(labelColor)
   {
      ASSERT(!_fields.empty());

      for (Field* field : _fields)
      {
         ASSERT(field != nullptr);
      }
   }

   ///
   /// <summary>
   /// Initializes a two-field horizontal row (left-aligned first field, right-aligned second field).
   /// </summary>
   /// <param name="field0">Leftmost field.</param>
   /// <param name="field1">Rightmost field.</param>
   /// <param name="labelColor">Default label color used when drawing fields through this row.</param>
   ///
   FieldRow(Field* field0, Field* field1, Color labelColor = Color::LABEL)
      : _labelColor(labelColor)
   {
      ASSERT(field0 != nullptr);
      ASSERT(field1 != nullptr);

      _fields.reserve(2);
      _fields.push_back(field0);
      _fields.push_back(field1);
   }

   ///
   /// <summary>
   /// Initializes a three-field horizontal row (left, evenly-centered middle, and right).
   /// </summary>
   /// <param name="field0">Leftmost field.</param>
   /// <param name="field1">Middle field.</param>
   /// <param name="field2">Rightmost field.</param>
   /// <param name="labelColor">Default label color used when drawing fields through this row.</param>
   ///
   FieldRow(
      Field* field0,
      Field* field1,
      Field* field2,
      Color labelColor = Color::LABEL)
      : _labelColor(labelColor)
   {
      ASSERT(field0 != nullptr);
      ASSERT(field1 != nullptr);
      ASSERT(field2 != nullptr);

      _fields.reserve(3);
      _fields.push_back(field0);
      _fields.push_back(field1);
      _fields.push_back(field2);
   }

   ///
   /// <summary>
   /// Initializes a four-field horizontal row with equal spacing between adjacent fields.
   /// </summary>
   /// <param name="field0">First (leftmost) field.</param>
   /// <param name="field1">Second field.</param>
   /// <param name="field2">Third field.</param>
   /// <param name="field3">Fourth (rightmost) field.</param>
   /// <param name="labelColor">Default label color used when drawing fields through this row.</param>
   ///
   FieldRow(
      Field* field0,
      Field* field1,
      Field* field2,
      Field* field3,
      Color labelColor = Color::LABEL)
      : _labelColor(labelColor)
   {
      ASSERT(field0 != nullptr);
      ASSERT(field1 != nullptr);
      ASSERT(field2 != nullptr);
      ASSERT(field3 != nullptr);

      _fields.reserve(4);
      _fields.push_back(field0);
      _fields.push_back(field1);
      _fields.push_back(field2);
      _fields.push_back(field3);
   }

   ///
   /// <summary>
   /// Gets the number of fields in this row.
   /// </summary>
   /// <returns>Field count.</returns>
   ///
   size_t count() const
   {
      return _fields.size();
   }

   ///
   /// <summary>
   /// Gets the field at the given zero-based index.
   /// </summary>
   /// <param name="index">Zero-based field index.</param>
   /// <returns>Pointer to the field at index.</returns>
   ///
   Field* field(size_t index) const
   {
      ASSERT(index < _fields.size());

      return _fields[index];
   }

   ///
   /// <summary>
   /// Positions all fields in the row across the horizontal span [x, x + width) at vertical
   /// coordinate y. The first field is left-aligned at x, the last field is right-aligned at
   /// x + width, and any interior fields are distributed with equal gaps between neighbors.
   /// </summary>
   /// <param name="x">Left X coordinate of the row region.</param>
   /// <param name="y">Top Y coordinate of the row.</param>
   /// <param name="width">Total horizontal pixel width available to the row.</param>
   ///
   void setBounds(int16_t x, int16_t y, int16_t width)
   {
      ASSERT(!_fields.empty());

      size_t n = _fields.size();
      if (n == 1)
      {
         _fields[0]->setPosition(x, y);
         return;
      }

      int32_t totalFieldWidth = 0;
      for (Field* f : _fields)
      {
         totalFieldWidth += f->width();
      }

      int32_t totalGap = static_cast<int32_t>(width) - totalFieldWidth;
      int32_t numGaps = static_cast<int32_t>(n - 1);

      _fields[0]->setPosition(x, y);

      int32_t cursorX = x + _fields[0]->width();
      for (size_t i = 1; i + 1 < n; i++)
      {
         int32_t gap = (totalGap * static_cast<int32_t>(i)) / numGaps -
                       (totalGap * static_cast<int32_t>(i - 1)) / numGaps;
         cursorX += gap;
         _fields[i]->setPosition(static_cast<int16_t>(cursorX), y);
         cursorX += _fields[i]->width();
      }

      int16_t rightFieldX = static_cast<int16_t>(x + width - _fields[n - 1]->width());
      _fields[n - 1]->setPosition(rightFieldX, y);
   }

   ///
   /// <summary>
   /// Forces every field in the row to redraw both its label and value on the next draw().
   /// </summary>
   ///
   void invalidate()
   {
      for (Field* f : _fields)
      {
         f->invalidate();
      }
   }

   ///
   /// <summary>
   /// Clears the value regions of all fields in the row with the specified background color.
   /// </summary>
   /// <param name="backgroundColor">Fill color painted over each field's value sprite.</param>
   ///
   void clear(Color backgroundColor = Color::BLACK)
   {
      for (Field* f : _fields)
      {
         f->clear(backgroundColor);
      }
   }

   ///
   /// <summary>
   /// Draws a two-field row with per-field float values and active value colors (NAN values
   /// automatically render as gray "---" placeholders).
   /// </summary>
   /// <param name="value0">First field value.</param>
   /// <param name="color0">First field active value color.</param>
   /// <param name="value1">Second field value.</param>
   /// <param name="color1">Second field active value color.</param>
   /// <param name="backgroundColor">Background color behind each value.</param>
   ///
   void draw(
      float value0,
      Color color0,
      float value1,
      Color color1,
      Color backgroundColor = Color::BLACK)
   {
      ASSERT(_fields.size() == 2);

      _fields[0]->draw(value0, _labelColor, color0, backgroundColor);
      _fields[1]->draw(value1, _labelColor, color1, backgroundColor);
   }

   ///
   /// <summary>
   /// Draws a three-field row with per-field float values and active value colors (NAN values
   /// automatically render as gray "---" placeholders).
   /// </summary>
   /// <param name="value0">First field value.</param>
   /// <param name="color0">First field active value color.</param>
   /// <param name="value1">Second field value.</param>
   /// <param name="color1">Second field active value color.</param>
   /// <param name="value2">Third field value.</param>
   /// <param name="color2">Third field active value color.</param>
   /// <param name="backgroundColor">Background color behind each value.</param>
   ///
   void draw(
      float value0,
      Color color0,
      float value1,
      Color color1,
      float value2,
      Color color2,
      Color backgroundColor = Color::BLACK)
   {
      ASSERT(_fields.size() == 3);

      _fields[0]->draw(value0, _labelColor, color0, backgroundColor);
      _fields[1]->draw(value1, _labelColor, color1, backgroundColor);
      _fields[2]->draw(value2, _labelColor, color2, backgroundColor);
   }

   ///
   /// <summary>
   /// Draws a four-field row with per-field float values and active value colors (NAN values
   /// automatically render as gray "---" placeholders).
   /// </summary>
   /// <param name="value0">First field value.</param>
   /// <param name="color0">First field active value color.</param>
   /// <param name="value1">Second field value.</param>
   /// <param name="color1">Second field active value color.</param>
   /// <param name="value2">Third field value.</param>
   /// <param name="color2">Third field active value color.</param>
   /// <param name="value3">Fourth field value.</param>
   /// <param name="color3">Fourth field active value color.</param>
   /// <param name="backgroundColor">Background color behind each value.</param>
   ///
   void draw(
      float value0,
      Color color0,
      float value1,
      Color color1,
      float value2,
      Color color2,
      float value3,
      Color color3,
      Color backgroundColor = Color::BLACK)
   {
      ASSERT(_fields.size() == 4);

      _fields[0]->draw(value0, _labelColor, color0, backgroundColor);
      _fields[1]->draw(value1, _labelColor, color1, backgroundColor);
      _fields[2]->draw(value2, _labelColor, color2, backgroundColor);
      _fields[3]->draw(value3, _labelColor, color3, backgroundColor);
   }
};
