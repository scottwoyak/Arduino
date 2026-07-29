#pragma once

#include "ArduinoWithDisplay.h"
#include "Anchor.h"
#include "ValueEditor.h"
#include "Field.h"
#include "Format.h"
#include "Color.h"
#include "Util.h"
#include <cmath>
#include <cstring>
#include <span>
#include <string>
#include <vector>

// Forward declaration only - FieldTableEditor is built on top of FieldTable the same
// way the old DisplayTableEditor was built on top of Table (see FieldTableEditor.h).
class FieldTableEditor;

///
/// <summary>
/// A utility class to display a single-column "label value" table on an Arduino
/// display, built from a stack of Field objects rather than Table's per-column
/// sprites. Every row is drawn GAP-aligned, so every row's value lines up
/// at the same X position regardless of how long each row's own label is. Like Table,
/// rows can be grouped under section header rows (see addSection()), which are stored
/// and indexed as rows in their own right - counting toward rowIndex arguments the same
/// as any other row - drawn with a half-row gap above them separating sections.
/// </summary>
/// <remarks>
/// Each data row's Field owns its own off-screen sprite for its value, so redrawing a
/// row (see draw()) only repaints that row's value region, avoiding flicker. Rows
/// must be added in their final display order via addRow()/addRows()/addSection()
/// before the table is first drawn; there is no support for reordering or removing
/// individual rows other than clearRows(), which removes them all.
/// </remarks>
///
class FieldTable
{
public:
   ///
   /// <summary>
   /// Describes a single row's label, format string, and colors, for use with addRows()
   /// to define every row up front in a single caller-owned array, instead of a manual
   /// loop over addRow(). A Row constructed with just a label and no format string is a
   /// section header instead of a data row: addRows() adds it as its own section row
   /// (see addSection()) at that position in the array, so sections can be declared
   /// inline alongside the data rows they group, instead of via a separate call after
   /// the fact.
   /// </summary>
   ///
   struct Row
   {
      const char* label;
      const char* formatStr = nullptr;
      Color labelColor = Color::LABEL;
      Color valueColor = Color::VALUE;

      constexpr Row(const char* label, const char* formatStr,
         Color labelColor = Color::LABEL, Color valueColor = Color::VALUE)
         : label(label), formatStr(formatStr), labelColor(labelColor), valueColor(valueColor) {}

      ///
      /// <summary>
      /// Constructs a data row backed directly by a caller-owned float, rather than a
      /// ValueBase or a value pushed in per-call: draw() reads *value itself, only
      /// redrawing the row when the value has actually changed since the last draw().
      /// </summary>
      /// <param name="label">The text label for the row.</param>
      /// <param name="formatStr">Pattern containing optional prefix/postfix and # placeholders.</param>
      /// <param name="value">Caller-owned variable that holds the current value.</param>
      /// <param name="labelColor">The color to draw the label text (default: Color::LABEL).</param>
      ///
      Row(const char* label, const char* formatStr, float* value, Color labelColor = Color::LABEL)
         : label(label), formatStr(formatStr), labelColor(labelColor), valuePtr(value) {}

      ///
      /// <summary>
      /// Constructs a data row backed by a ValueBase rather than a value pushed in
      /// per-call: draw() reads the value's current valueText() itself, only redrawing the
      /// row when its formatted text actually changes.
      /// </summary>
      /// <param name="label">The text label for the row.</param>
      /// <param name="value">The ValueBase providing this row's format and current value.</param>
      /// <param name="labelColor">The color to draw the label text (default: Color::LABEL).</param>
      ///
      Row(const char* label, ValueBase* value, Color labelColor = Color::LABEL)
         : label(label), labelColor(labelColor), value(value) {}

      ///
      /// <summary>
      /// Constructs a section header row rather than a data row: addRows() adds section
      /// as its own section row (see addSection()) at that position in the array.
      /// </summary>
      /// <param name="section">The section header text.</param>
      ///
      constexpr Row(const char* section) : label(section) {}

      ValueBase* value = nullptr;
      float* valuePtr = nullptr;
   };

private:
   friend class FieldTableEditor;

   ///
   /// <summary>
   /// A single row's contents: either a data row's Field (its label and value
   /// rendering) together with its label color, or - if isSection is true - a section
   /// header row's label text drawn on its own, with no Field/value. Also stores the
   /// row's currently laid-out Y offset (relative to the table's own Y position) and
   /// whether the row has been drawn since the last layout change.
   /// </summary>
   ///
   struct RowContents
   {
      bool isSection;
      const char* label;
      Field* field;
      Color labelColor;
      uint8_t labelLength;
      bool drawn = false;
      int16_t yOffset = 0;

      // Set only for rows backed by a ValueBase (see the Row(label, value) constructor);
      // draw() reads this value's current valueText() and lastValue caches the most recently
      // drawn text so draw() can skip repainting rows whose formatted text hasn't changed.
      ValueBase* value = nullptr;
      std::string lastValue;

      // Set only for rows backed directly by a caller-owned float (see the Row(label,
      // formatStr, value) constructor); draw() reads *valuePtr itself and lastFloatValue
      // caches the most recently drawn value so draw() can skip repainting rows whose
      // value hasn't changed.
      float* valuePtr = nullptr;
      float lastFloatValue = NAN;

      // Data row constructor: owns a Field for GAP-aligned label/value rendering.
      RowContents(ArduinoWithDisplay* display, const char* label, const char* formatStr, uint8_t textSize, Color labelColor)
         : isSection(false), label(label), field(new Field(display, label, formatStr, textSize, Field::Alignment::GAP)),
           labelColor(labelColor), labelLength((uint8_t)strlen(label))
      {
      }

      // Data row constructor taking an already-parsed Format: used internally by
      // FieldTableEditor, which owns parsed Format instances per field.
      RowContents(ArduinoWithDisplay* display, const char* label, const Format& format, uint8_t textSize, Color labelColor)
         : isSection(false), label(label), field(new Field(display, label, format, textSize, Field::Alignment::GAP)),
           labelColor(labelColor), labelLength((uint8_t)strlen(label))
      {
      }

      // Data row constructor backed by a ValueBase: draw() pulls the value from the
      // value itself instead of the caller pushing it in via a per-row draw call.
      RowContents(ArduinoWithDisplay* display, const char* label, ValueBase* value, uint8_t textSize, Color labelColor)
         : isSection(false), label(label), field(new Field(display, label, value->format(), textSize, Field::Alignment::GAP)),
           labelColor(labelColor), labelLength((uint8_t)strlen(label)), value(value)
      {
      }

      // Data row constructor backed directly by a caller-owned float: draw() pulls the
      // value from *valuePtr itself instead of the caller pushing it in via a per-row draw call.
      RowContents(ArduinoWithDisplay* display, const char* label, const char* formatStr, float* value, uint8_t textSize, Color labelColor)
         : isSection(false), label(label), field(new Field(display, label, formatStr, textSize, Field::Alignment::GAP)),
           labelColor(labelColor), labelLength((uint8_t)strlen(label)), valuePtr(value)
      {
      }

      // Section header row constructor: no Field, just the header label text.
      RowContents(const char* label)
         : isSection(true), label(label), field(nullptr), labelColor(Color::SECTION_HEADER), labelLength((uint8_t)strlen(label))
      {
      }

      ~RowContents()
      {
         delete field;
      }
   };

   ArduinoWithDisplay* _display;
   int16_t _x;
   int16_t _y;
   uint8_t _textSize;
   bool _showSections = true;
   bool _layoutDirty = true;
   int16_t _totalHeight = 0;
   uint8_t _maxLabelLength = 0;

   // Rows are heap-allocated and stored by pointer, rather than by value, so growing
   // the vector never moves or copies existing RowContents instances, which each own a
   // Field/sprite; only the pointers themselves are relocated on reallocation. Rows are
   // deleted manually (see clearRows() and ~FieldTable()) since FieldTable is their sole
   // owner and never shares them.
   std::vector<RowContents*> _rows;

   ///
   /// <summary>
   /// Adds a new row, parsing the given format string into a Format owned internally by
   /// the row's Field. Only reachable internally; sketches should use one of the public
   /// addRow() overloads instead.
   /// </summary>
   /// <param name="label">The text label for the row.</param>
   /// <param name="formatStr">Pattern containing optional prefix/postfix and # placeholders.</param>
   /// <param name="labelColor">The color to draw the label text (default: Color::LABEL).</param>
   /// <param name="valueColor">The color to draw the value text (default: Color::VALUE); unused,
   /// kept for symmetry with addRow(label, formatStr, valueColor) since the value color is
   /// actually supplied per-call by the row's value during draw().</param>
   ///
   void addRow(const char* label, const char* formatStr, Color labelColor, Color valueColor)
   {
      (void)valueColor;

      RowContents* row = new RowContents(_display, label, formatStr, _textSize, labelColor);
      if (row->labelLength > _maxLabelLength)
      {
         _maxLabelLength = row->labelLength;
      }

      _rows.push_back(row);
      _layoutDirty = true;
   }

   ///
   /// <summary>
   /// Adds a new row, using the given already-parsed Format for its value. Only
   /// reachable internally (e.g. via FieldTableEditor, which owns parsed Format
   /// instances per field); sketches should use the format-string overload of addRow()
   /// instead.
   /// </summary>
   /// <param name="label">The text label for the row.</param>
   /// <param name="format">The formatter to apply to the row's value.</param>
   /// <param name="labelColor">The color to draw the label text (default: Color::LABEL).</param>
   ///
   void addRow(const char* label, const Format& format, Color labelColor = Color::LABEL)
   {
      RowContents* row = new RowContents(_display, label, format, _textSize, labelColor);
      if (row->labelLength > _maxLabelLength)
      {
         _maxLabelLength = row->labelLength;
      }

      _rows.push_back(row);
      _layoutDirty = true;
   }

   ///
   /// <summary>
   /// Adds a new section header row, drawn as its own row (with a half-row gap above it
   /// separating it from the previous row/section) rather than as metadata on another
   /// row. Counts toward rowIndex arguments the same as a data row added via addRow().
   /// </summary>
   /// <param name="section">The section header text.</param>
   ///
   void addSection(const char* section)
   {
      _rows.push_back(new RowContents(section));
      _layoutDirty = true;
   }

   ///
   /// <summary>
   /// Recomputes every row's Y offset - accounting for section header rows and the
   /// half-row gap separating sections - and repositions each row's Field, if the
   /// layout is currently dirty (a row/section was added or changed since the last
   /// call). Also marks every section header as needing to be redrawn, since a
   /// repositioned row's field forces its label to redraw anyway.
   /// </summary>
   ///
   void _relayout()
   {
      if (!_layoutDirty)
      {
         return;
      }

      int16_t rowHeight = _display->charH(_textSize);
      int16_t halfRow = rowHeight / 2;
      int16_t y = 0;
      int16_t gapX = _x + (int16_t)(_maxLabelLength * _display->charW(_textSize));

      for (size_t i = 0; i < _rows.size(); i++)
      {
         RowContents& row = *_rows[i];

         if (_showSections && row.isSection && i > 0)
         {
            y += halfRow;
         }

         row.yOffset = y;
         row.drawn = false;

         if (!row.isSection)
         {
            row.field->setPosition(Point16(gapX, _y + y));
         }

         if (!_showSections && row.isSection)
         {
            continue;
         }

         y += rowHeight;
      }

      _totalHeight = y;
      _layoutDirty = false;
   }

public:
   // FieldTable owns its RowContents pointers outright and deletes them itself (see
   // clearRows()/~FieldTable()), so it must not be copied or moved, which would
   // otherwise leave two FieldTables deleting the same RowContents instances.
   FieldTable(const FieldTable&) = delete;
   FieldTable& operator=(const FieldTable&) = delete;

   ///
   /// <summary>
   /// Initializes a new instance of the FieldTable class with no rows.
   /// </summary>
   /// <param name="display">The display interface to draw onto.</param>
   /// <param name="x">The X coordinate of the label column's left edge, shared by every row.</param>
   /// <param name="y">The Y coordinate of the table's top-left corner.</param>
   /// <param name="textSize">The text size applied to every row's label and value.</param>
   ///
   FieldTable(ArduinoWithDisplay* display, int16_t x, int16_t y, uint8_t textSize = 2)
      : _display(display), _x(x), _y(y), _textSize(textSize)
   {
   }

   ///
   /// <summary>
   /// Initializes a new instance of the FieldTable class, adding the given rows
   /// immediately (equivalent to calling addRows(rows) right after construction).
   /// </summary>
   /// <param name="display">The display interface to draw onto.</param>
   /// <param name="x">The X coordinate of the label column's left edge, shared by every row.</param>
   /// <param name="y">The Y coordinate of the table's top-left corner.</param>
   /// <param name="rows">Row label/format metadata, one entry per row to add.</param>
   /// <param name="textSize">The text size applied to every row's label and value.</param>
   ///
   FieldTable(ArduinoWithDisplay* display, int16_t x, int16_t y, std::span<const Row> rows, uint8_t textSize = 2)
      : _display(display), _x(x), _y(y), _textSize(textSize)
   {
      addRows(rows);
   }

   ///
   /// <summary>
   /// Deletes every row's RowContents instance, since FieldTable owns them outright.
   /// </summary>
   ///
   ~FieldTable()
   {
      clearRows();
   }

   ///
   /// <summary>
   /// Adds a new row, parsing the given format string into a Format owned internally by
   /// the row's Field, using a non-default label color.
   /// </summary>
   /// <param name="label">The text label for the row.</param>
   /// <param name="labelColor">The color to draw the label text.</param>
   /// <param name="formatStr">Pattern containing optional prefix/postfix and # placeholders.</param>
   /// <param name="valueColor">Unused; kept only so callers can mirror Table::addRow()'s
   /// parameter order. The value color is supplied per-call by the row's value during draw() instead.</param>
   ///
   void addRow(const char* label, Color labelColor, const char* formatStr, Color valueColor = Color::VALUE)
   {
      addRow(label, formatStr, labelColor, valueColor);
   }

   ///
   /// <summary>
   /// Adds a new row, parsing the given format string into a Format owned internally by
   /// the row's Field, using the default label color (Color::LABEL).
   /// </summary>
   /// <param name="label">The text label for the row.</param>
   /// <param name="formatStr">Pattern containing optional prefix/postfix and # placeholders.</param>
   /// <param name="valueColor">Unused; kept only so callers can mirror Table::addRow()'s
   /// parameter order. The value color is supplied per-call by the row's value during draw() instead.</param>
   ///
   void addRow(const char* label, const char* formatStr, Color valueColor = Color::VALUE)
   {
      addRow(label, Color::LABEL, formatStr, valueColor);
   }

   ///
   /// <summary>
   /// Adds a new row backed by a ValueBase instead of a value pushed in per-call:
   /// draw() reads the value's current valueText() itself, only redrawing the row when its
   /// formatted text actually changes.
   /// </summary>
   /// <param name="label">The text label for the row.</param>
   /// <param name="value">The ValueBase providing this row's format and current value.</param>
   /// <param name="labelColor">The color to draw the label text (default: Color::LABEL).</param>
   ///
   void addRow(const char* label, ValueBase* value, Color labelColor = Color::LABEL)
   {
      RowContents* row = new RowContents(_display, label, value, _textSize, labelColor);
      if (row->labelLength > _maxLabelLength)
      {
         _maxLabelLength = row->labelLength;
      }

      _rows.push_back(row);
      _layoutDirty = true;
   }

   ///
   /// <summary>
   /// Adds a new row backed directly by a caller-owned float instead of a ValueBase
   /// or a value pushed in per-call: draw() reads *value itself, only redrawing the row
   /// when the value has actually changed since the last draw().
   /// </summary>
   /// <param name="label">The text label for the row.</param>
   /// <param name="formatStr">Pattern containing optional prefix/postfix and # placeholders.</param>
   /// <param name="value">Caller-owned variable that holds the current value.</param>
   /// <param name="labelColor">The color to draw the label text (default: Color::LABEL).</param>
   ///
   void addRow(const char* label, const char* formatStr, float* value, Color labelColor = Color::LABEL)
   {
      RowContents* row = new RowContents(_display, label, formatStr, value, _textSize, labelColor);
      if (row->labelLength > _maxLabelLength)
      {
         _maxLabelLength = row->labelLength;
      }

      _rows.push_back(row);
      _layoutDirty = true;
   }

   ///
   /// <summary>
   /// Adds every row described in the given array, in order, equivalent to calling
   /// addRow(row.label, row.labelColor, row.formatStr, row.valueColor) - or
   /// addRow(row.label, row.value, row.labelColor) for a value-backed Row, or
   /// addRow(row.label, row.formatStr, row.valuePtr, row.labelColor) for a float-backed
   /// Row - once per entry. A Row entry with no format string, no value, and no value
   /// pointer (see Row's section-header constructor) is added as its own section row via
   /// addSection() instead.
   /// </summary>
   /// <param name="rows">Row metadata, one entry per row to add.</param>
   ///
   void addRows(std::span<const Row> rows)
   {
      for (const auto& row : rows)
      {
         if (row.value != nullptr)
         {
            addRow(row.label, row.value, row.labelColor);
         }
         else if (row.valuePtr != nullptr)
         {
            addRow(row.label, row.formatStr, row.valuePtr, row.labelColor);
         }
         else if (row.formatStr == nullptr)
         {
            addSection(row.label);
         }
         else
         {
            addRow(row.label, row.labelColor, row.formatStr, row.valueColor);
         }
      }
   }

   ///
   /// <summary>
   /// Removes all rows from the table, e.g. so addRow() can be called again to build a
   /// completely different set of rows.
   /// </summary>
   ///
   void clearRows()
   {
      for (RowContents* row : _rows)
      {
         delete row;
      }
      _rows.clear();
      _layoutDirty = true;
   }

   ///
   /// <summary>
   /// Gets the number of rows currently in the table.
   /// </summary>
   /// <returns>The current row count.</returns>
   ///
   size_t rowCount() const
   {
      return _rows.size();
   }

   ///
   /// <summary>
   /// Sets whether section header rows (see addSection()) are drawn and reserved space
   /// for. Defaults to true.
   /// </summary>
   /// <param name="showSections">True to draw section headers, false to suppress them.</param>
   ///
   void setShowSections(bool showSections)
   {
      _showSections = showSections;
      _layoutDirty = true;
   }

   ///
   /// <summary>
   /// Sets the color drawn for a row's label text, applied the next time that row's
   /// label is drawn (e.g. after the row is repositioned or invalidate() is called).
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="labelColor">The color to draw the label text.</param>
   ///
   void setLabelColor(size_t rowIndex, Color labelColor)
   {
      if (rowIndex < _rows.size())
      {
         _rows[rowIndex]->labelColor = labelColor;
      }
   }

   ///
   /// <summary>
   /// Moves the table so that the given anchor point of its bounding rectangle is placed
   /// at (x, y), and forces every row to be repositioned (and therefore fully redrawn) on
   /// the next draw() call. The table's width/height (see getWidth()/getHeight()) are used
   /// to compute the top-left position for anchors other than TOP_LEFT, so this reflects
   /// the table's current rows/sections at the time of the call.
   /// </summary>
   /// <param name="x">The X coordinate of the anchor point; negative values offset from the right edge.</param>
   /// <param name="y">The Y coordinate of the anchor point; negative values offset from the bottom edge.</param>
   /// <param name="anchor">Which point of the table's bounding rectangle (x, y) refers to (default: TOP_LEFT).</param>
   ///
   void setPosition(int16_t x, int16_t y, Anchor anchor = Anchor::TOP_LEFT)
   {
      _display->normalizeCoords(x, y);

      if (anchor != Anchor::TOP_LEFT)
      {
         int16_t width = getWidth();
         int16_t height = getHeight();

         if (anchor == Anchor::TOP_RIGHT || anchor == Anchor::BOTTOM_RIGHT || anchor == Anchor::MIDDLE_RIGHT)
         {
            x -= width;
         }
         else if (anchor == Anchor::CENTER || anchor == Anchor::TOP_CENTER || anchor == Anchor::BOTTOM_CENTER)
         {
            x -= width / 2;
         }

         if (anchor == Anchor::BOTTOM_LEFT || anchor == Anchor::BOTTOM_RIGHT || anchor == Anchor::BOTTOM_CENTER)
         {
            y -= height;
         }
         else if (anchor == Anchor::CENTER || anchor == Anchor::MIDDLE_LEFT || anchor == Anchor::MIDDLE_RIGHT)
         {
            y -= height / 2;
         }
      }

      _x = x;
      _y = y;
      _layoutDirty = true;
   }

   ///
   /// <summary>
   /// Moves the table so that the given anchor point of its bounding rectangle is placed
   /// at pos, and forces every row to be repositioned (and therefore fully redrawn) on
   /// the next draw() call.
   /// </summary>
   /// <param name="pos">The coordinate of the anchor point.</param>
   /// <param name="anchor">Which point of the table's bounding rectangle pos refers to (default: TOP_LEFT).</param>
   ///
   void setPosition(Point16 pos, Anchor anchor = Anchor::TOP_LEFT)
   {
      setPosition(pos.x, pos.y, anchor);
   }

   ///
   /// <summary>
   /// Computes the total pixel width the table needs to display its label and value
   /// column, based on the longest label and the widest row's formatted value. Useful
   /// for laying out other content (e.g. a plot) relative to the table's actual content
   /// width, or for anchoring the table itself (see setPosition()).
   /// </summary>
   /// <returns>The required width, in pixels.</returns>
   ///
   int16_t getWidth()
   {
      int16_t labelWidth = (int16_t)(_maxLabelLength * _display->charW(_textSize));

      int16_t maxValueWidth = 0;
      for (const RowContents* row : _rows)
      {
         if (row->isSection)
         {
            continue;
         }

         int16_t valueWidth = row->field->valueWidth();
         if (valueWidth > maxValueWidth)
         {
            maxValueWidth = valueWidth;
         }
      }

      return labelWidth + maxValueWidth;
   }

   ///
   /// <summary>
   /// Computes the total pixel height needed to display every row, including section
   /// header rows and the half-row gaps separating sections. Useful for laying out
   /// other content relative to the table, or for anchoring the table itself (see
   /// setPosition()).
   /// </summary>
   /// <returns>The required height, in pixels.</returns>
   ///
   int16_t getHeight()
   {
      _relayout();
      return _totalHeight;
   }

   ///
   /// <summary>
   /// Computes the table's bounding rectangle, based on its top-left position, its
   /// content width (see getWidth()), and its total height (see getHeight()). Useful for
   /// laying out other content (e.g. a value field) relative to the table's actual bounds.
   /// </summary>
   /// <returns>The table's bounding rectangle, in pixels.</returns>
   ///
   Rect16 getRect()
   {
      return Rect16{ (uint16_t)_x, (uint16_t)_y, (uint16_t)getWidth(), (uint16_t)getHeight() };
   }

   ///
   /// <summary>
   /// Forces every row's Field to redraw its label from scratch on the next draw() call
   /// (e.g. after the display area was cleared, or the table's font/size changed).
   /// </summary>
   ///
   void invalidate()
   {
      for (auto& row : _rows)
      {
         if (!row->isSection)
         {
            row->field->invalidate();
         }
         row->drawn = false;
      }
   }

   ///
   /// <summary>
   /// Draws a single data row's label (the first time it's drawn, or after
   /// invalidate()/repositioning) and its current value. Only reachable internally (e.g.
   /// via FieldTableEditor, which supplies its own per-row selection/disabled colors
   /// instead of pulling them from a value); sketches should use draw() instead.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to draw; must refer to a
   /// data row added via addRow(), not a section row added via addSection().</param>
   /// <param name="value">The new value to display, formatted through the row's Format object.</param>
   /// <param name="valueColor">The color used to draw the value text (default: Color::VALUE).</param>
   /// <param name="backgroundColor">The background color drawn behind the value (default: Color::BLACK).</param>
   ///
   template <typename T>
   void _drawDataRow(size_t rowIndex, const T& value, Color valueColor = Color::VALUE, Color backgroundColor = Color::BLACK)
   {
      ASSERT(rowIndex < _rows.size());

      RowContents& row = *_rows[rowIndex];

      ASSERT(!row.isSection);

      row.field->draw(value, row.labelColor, valueColor, backgroundColor);
      row.drawn = true;
   }

   ///
   /// <summary>
   /// Draws a single section header row's text, the first time it's drawn since the
   /// last layout change (e.g. after a row/section was added or the table was
   /// repositioned/invalidated). Does nothing if showSections(false) was set. Only
   /// reachable internally (e.g. via FieldTableEditor); sketches should use draw() instead.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to draw; must refer to a
   /// section row added via addSection(), not a data row added via addRow().</param>
   ///
   void _drawSectionRow(size_t rowIndex)
   {
      ASSERT(rowIndex < _rows.size());

      RowContents& row = *_rows[rowIndex];

      ASSERT(row.isSection);

      if (!_showSections || row.drawn)
      {
         return;
      }

      _display->setTextSize(_textSize, true);
      _display->setCursor(_x, _y + row.yOffset);
      _display->println(row.label, row.labelColor, Color::BLACK);
      row.drawn = true;
   }

public:
   ///
   /// <summary>
   /// Draws every row: section headers the first time they're drawn since the last
   /// layout change, every row backed by a DisplayTableValue (see addRow(label, value)) by
   /// reading its current valueText(), and every row backed directly by a caller-owned
   /// float (see addRow(label, formatStr, value)) by reading *value - only repainting
   /// rows whose value has changed since the last call (or that haven't been drawn yet,
   /// e.g. after invalidate() or repositioning). Rows added via the plain formatStr
   /// overloads of addRow() have no value or value pointer to pull a value from and are
   /// skipped.
   /// </summary>
   ///
   void draw()
   {
      _relayout();

      for (size_t i = 0; i < _rows.size(); i++)
      {
         RowContents& row = *_rows[i];

         if (row.isSection)
         {
            _drawSectionRow(i);
            continue;
         }

         if (row.value != nullptr)
         {
            std::string value = row.value->valueText();
            if (row.drawn && value == row.lastValue)
            {
               continue;
            }

            Color valueColor = row.value->hasColor() ? row.value->color() : Color::VALUE;
            _drawDataRow(i, value, valueColor, Color::BLACK);
            row.lastValue = value;
         }
         else if (row.valuePtr != nullptr)
         {
            if (row.drawn && *row.valuePtr == row.lastFloatValue)
            {
               continue;
            }

            _drawDataRow(i, *row.valuePtr, Color::VALUE, Color::BLACK);
            row.lastFloatValue = *row.valuePtr;
         }
      }
   }
};
