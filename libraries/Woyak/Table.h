#pragma once

#include "ArduinoWithDisplay.h"
#include "Anchor.h"
#include "Format.h"
#include "Color.h"
#include <vector>
#include <deque>
#include <algorithm>
#include <span>
#include <string>

///
/// <summary>
/// A utility class to display a formatted label/value(s) table on an Arduino display.
/// Supports either a simple single-value-per-row table (the legacy addRow()/setValue()
/// API, with no column headers) or a multi-column table constructed with an explicit
/// Column array, which draws a header row and a divider
/// line above the data rows. Each column - including the implicit single value column -
/// is redrawn through its own off-screen sprite, sized to fit that column's widest
/// formatted value, which avoids reprinting labels/headers and reduces flicker compared
/// to drawing each value directly to the display every frame.
/// </summary>
///
class Table
{
public:
   ///
   /// <summary>
   /// Controls horizontal alignment of a row label or column's header/values within its
   /// reserved width. Distinct from Format::Alignment since table alignment is a
   /// layout concept (tables don't support e.g. decimal alignment), not a
   /// value-formatting concept.
   /// </summary>
   ///
   enum class Alignment
   {
      LEFT,
      RIGHT,
      CENTER
   };

   ///
   /// <summary>
   /// Identifies which point of the table's bounding rectangle a position passed to
   /// setPosition() refers to. Defaults to TOP_LEFT, matching setPosition()'s original
   /// top-left-only behavior.
   /// </summary>
   ///

   ///
   /// <summary>
   /// Describes a single value column in a multi-column table: its header title, the
   /// format used to render its cell values, and an optional fixed color that overrides
   /// a cell's own value color for this column (e.g. for an ID column that should always
   /// be white regardless of row status).
   /// </summary>
   ///
   struct Column
   {
      const char* title;
      Format format;
      bool hasColor = false;
      Color color = Color::WHITE;

      ///
      /// <summary>
      /// Creates a row-label column: used only as the table's first column entry to
      /// describe the header title drawn above the row labels (e.g. "Item") and the
      /// alignment of that title/the row labels within their reserved column width. Pass
      /// "" for no title. Has no format/values of its own.
      /// </summary>
      ///
      Column(const char* title, Table::Alignment alignment = Table::Alignment::LEFT)
         : title(title), format("", _toFormatAlignment(alignment)) {}
      Column(const char* title, const char* formatStr, Table::Alignment alignment = Table::Alignment::LEFT)
         : title(title), format(formatStr, _toFormatAlignment(alignment)) {}
      Column(const char* title, const char* formatStr, Color color)
         : title(title), format(formatStr), hasColor(true), color(color) {}
      Column(const char* title, const std::string& formatStr, Table::Alignment alignment = Table::Alignment::LEFT)
         : Column(title, formatStr.c_str(), alignment) {}
      Column(const char* title, const std::string& formatStr, Color color)
         : Column(title, formatStr.c_str(), color) {}

   private:
      ///
      /// <summary>
      /// Converts a table Alignment to the Format::Alignment used internally to render
      /// the column's cell values.
      /// </summary>
      ///
      static Format::Alignment _toFormatAlignment(Table::Alignment alignment)
      {
         switch (alignment)
         {
            case Table::Alignment::RIGHT: return Format::Alignment::RIGHT;
            case Table::Alignment::CENTER: return Format::Alignment::CENTER;
            case Table::Alignment::LEFT:
            default: return Format::Alignment::LEFT;
         }
      }
   };

   ///
   /// <summary>
   /// Describes a single row's label metadata for a multi-column table: its label text and
   /// an optional fixed label color (default: Color::LABEL). Used with addRows() to define
   /// every row up front, in a single caller-owned array, instead of a manual loop over
   /// addRow() with a separately tracked row count.
   /// </summary>
   ///
   struct Row
   {
      const char* label;
      Color labelColor = Color::LABEL;

      constexpr Row(const char* label) : label(label) {}
      constexpr Row(const char* label, Color labelColor) : label(label), labelColor(labelColor) {}
   };

   ///
   /// <summary>
   /// Describes a single row's label, format string, and colors for a legacy single-value
   /// table. Used with addRows()/the matching constructor to define every row up front, in
   /// a single caller-owned array, instead of a manual loop over addRow().
   /// </summary>
   ///
   struct ValueRow
   {
      const char* label;
      const char* formatStr;
      Color labelColor = Color::LABEL;
      Color valueColor = Color::VALUE;

      constexpr ValueRow(const char* label, const char* formatStr,
         Color labelColor = Color::LABEL, Color valueColor = Color::VALUE)
         : label(label), formatStr(formatStr), labelColor(labelColor), valueColor(valueColor) {}
   };

private:
   ///
   /// <summary>
   /// Per-column cell state for a single row: the format used to render it (taken from
   /// the row's own format in legacy single-column mode, or from the table's Column
   /// array in multi-column mode), its current/drawn value text, and its current/drawn
   /// colors.
   /// </summary>
   ///
   struct Cell
   {
      Format format;
      String value;
      Color valueColor;
      Color valueBackgroundColor;
      String drawnValue;
      Color drawnValueColor;
      Color drawnValueBackgroundColor;

      Cell(const Format& fmt, Color vc)
         : format(fmt), value(""), valueColor(vc), valueBackgroundColor(Color::BLACK),
           drawnValue(""), drawnValueColor(vc), drawnValueBackgroundColor(Color::BLACK)
      {
      }
   };

   struct RowState
   {
      String label;
      Color labelColor;
      Color drawnLabelColor;
      const char* section = nullptr;
      std::vector<Cell> cells;

      RowState(const char* lbl, Color lc)
         : label(lbl), labelColor(lc), drawnLabelColor(lc)
      {
      }
   };

   std::vector<RowState> _rows;
   ArduinoWithDisplay* _display;
   int16_t _x;
   int16_t _y;
   int16_t _labelWidth;
   Alignment _labelAlignment;
   bool _labelsDrawn = false;
   uint8_t _textSize;
   bool _showSections = true;

     std::span<const Column> _columns;
     Color _headerColor;
     bool _headerDrawn = false;

    ///
    /// <summary>
    /// Converts a table Alignment to the Format::Alignment used internally to render
    /// legacy single-value row cells.
    /// </summary>
    ///
    static Format::Alignment _toFormatAlignment(Alignment alignment)
    {
       switch (alignment)
       {
          case Alignment::RIGHT: return Format::Alignment::RIGHT;
          case Alignment::CENTER: return Format::Alignment::CENTER;
          case Alignment::LEFT:
          default: return Format::Alignment::LEFT;
       }
    }

    ///
    /// <summary>
    /// Converts a Format::Alignment (as reported by a column's format) back to the
    /// table-layout Alignment used for row-label alignment.
    /// </summary>
    ///
    static Alignment _fromFormatAlignment(Format::Alignment alignment)
    {
       switch (alignment)
       {
          case Format::Alignment::RIGHT: return Alignment::RIGHT;
          case Format::Alignment::CENTER: return Alignment::CENTER;
          case Format::Alignment::LEFT:
          default: return Alignment::LEFT;
       }
    }

    ///
   /// <summary>
   /// Adds a new row to a legacy single-value table, using the given format for its one
   /// value cell. Only reachable internally (e.g. via FieldTableEditor, which owns
   /// parsed Format instances per field); sketches should use the format-string overload
   /// of addRow() instead.
   /// </summary>
   /// <param name="label">The text label for the row.</param>
   /// <param name="format">The formatter to apply to the row's value.</param>
   /// <param name="labelColor">The color to draw the label text (default: Color::LABEL).</param>
   /// <param name="valueColor">The color to draw the value text (default: Color::VALUE).</param>
   ///
   void addRow(const char* label, const Format& format, Color labelColor = Color::LABEL, Color valueColor = Color::VALUE)
   {
      RowState row(label, labelColor);
      row.cells.emplace_back(format, valueColor);
      _rows.push_back(std::move(row));

      int16_t labelLen = strlen(label);
      if (labelLen > _labelWidth)
      {
         _labelWidth = labelLen;
      }
   }

   std::vector<LGFX_Sprite> _sprites;
   std::vector<int16_t> _columnCharWidths;
   std::vector<int16_t> _columnX;
   std::vector<int16_t> _columnPixelWidths;
   bool _spritesCreated = false;
   int16_t _headerHeight = 0;

   ///
   /// <summary>
   /// Gets whether this table was constructed with an explicit Column array (and
   /// therefore draws a header row/divider line), as opposed to the legacy implicit
   /// single-value-column mode.
   /// </summary>
   ///
   bool _hasColumns() const
   {
      return !_columns.empty();
   }

   ///
   /// <summary>
   /// Gets the number of value columns this table draws (at least 1, even in legacy
   /// implicit single-column mode). In multi-column mode, _columns[0] is the row-label
   /// column, so the value column count is one less than _columns.size().
   /// </summary>
   ///
   size_t _valueColumnCount() const
   {
      return _hasColumns() ? _columns.size() - 1 : 1;
   }

   ///
   /// <summary>
   /// Computes the pixel Y offset (relative to _y) of a given row index, accounting for
   /// the header row (if any), section header rows, and the half-height blank row
   /// separating sections. A row starts a new section - and gets a header row (plus a
   /// separating half-row gap, unless it's the first row) drawn above it - whenever its
   /// section text is non-null. Passing _rows.size() computes the table's total pixel
   /// height.
   /// </summary>
   /// <param name="index">Zero-based row index, or _rows.size() for the total height.</param>
   /// <returns>Pixel offset from the table's Y position.</returns>
   ///
   int16_t _rowY(size_t index) const
   {
      int16_t rowHeight = _display->charH();
      int16_t halfRow = rowHeight / 2;
      int16_t y = _headerHeight;
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
   /// Computes the character width of a given column: the widest formatted value across
   /// all rows' cells at that column index, widened (in multi-column mode) to also fit
   /// the column's header title.
   /// </summary>
   ///
   int16_t _columnCharWidth(size_t columnIndex) const
   {
      size_t maxLength = 0;
      for (const auto& row : _rows)
      {
         if (columnIndex < row.cells.size())
         {
            size_t len = row.cells[columnIndex].format.length();
            if (len > maxLength)
            {
               maxLength = len;
            }
         }
      }

      if (_hasColumns())
      {
         size_t titleLength = strlen(_columns[columnIndex + 1].title);
         if (titleLength > maxLength)
         {
            maxLength = titleLength;
         }
      }

      return (int16_t)maxLength;
   }

   ///
   /// <summary>
   /// Creates one sprite per value column (if not already created), each sized to fit
   /// that column's widest formatted value/title and the current font height, then loads
   /// a stable copy of the current font into each sprite so they are not affected by
   /// later display font changes. Also computes each column's pixel width/X position and
   /// the header row height.
   /// </summary>
   ///
   void _createSprites()
   {
      if (_spritesCreated)
      {
         return;
      }

      if (_hasColumns() && _columns[0].title != nullptr)
      {
         int16_t titleLength = (int16_t)strlen(_columns[0].title);
         if (titleLength > _labelWidth)
         {
            _labelWidth = titleLength;
         }
      }

      LGFX* display = &_display->display;
      size_t columnCount = _valueColumnCount();

      _columnCharWidths.resize(columnCount);
      _columnPixelWidths.resize(columnCount);
      _columnX.resize(columnCount);

      _sprites.clear();
      _sprites.reserve(columnCount);

      int16_t currentX = _x + (int16_t)(_labelWidth * _display->charW()) + (int16_t)_display->charW();
      for (size_t i = 0; i < columnCount; i++)
      {
         int16_t charWidth = _columnCharWidth(i);
         _columnCharWidths[i] = charWidth;

         std::string widthSample((size_t)charWidth, '0');
         int16_t pixelWidth = (int16_t)display->textWidth(widthSample.c_str());
         _columnPixelWidths[i] = pixelWidth;
         _columnX[i] = currentX;
         currentX += pixelWidth + (int16_t)_display->charW();

         _sprites.emplace_back(display);
         int16_t spriteHeight = (int16_t)display->fontHeight();
         uint8_t size = constrain(_textSize, (uint8_t)1, (uint8_t)7);
         _display->createSprite(_sprites[i], pixelWidth, spriteHeight, size);
      }

      _headerHeight = _hasColumns() ? (_display->charH() + 2) : 0;

      _spritesCreated = true;
   }

   ///
   /// <summary>
   /// Renders a cell's value into its column's sprite and pushes it over the cell's
   /// region at the given position. The sprite/column may be wider than the cell's own
   /// formatted value (e.g. widened to fit a longer header title), so any extra padding
   /// beyond the formatted value's own width is applied here according to the cell's
   /// format alignment, rather than always left-justifying the value within the sprite.
   /// </summary>
   ///
   void _drawValueSprite(Cell& cell, size_t columnIndex, int16_t y)
   {
      LGFX_Sprite& sprite = _sprites[columnIndex];
      sprite.fillScreen((uint16_t)cell.valueBackgroundColor);
      sprite.setTextColor((uint16_t)cell.valueColor, (uint16_t)cell.valueBackgroundColor);

      std::string value = cell.value.c_str();
      int16_t charWidth = _columnCharWidths[columnIndex];
      if ((int16_t)value.length() < charWidth)
      {
         size_t fill = (size_t)charWidth - value.length();
         switch (cell.format.alignment())
         {
         case Format::Alignment::RIGHT:
            value = std::string(fill, ' ') + value;
            break;

         case Format::Alignment::CENTER:
         {
            size_t fillLeft = fill / 2;
            size_t fillRight = fill - fillLeft;
            value = std::string(fillLeft, ' ') + value + std::string(fillRight, ' ');
         }
         break;

         case Format::Alignment::LEFT:
         default:
            value = value + std::string(fill, ' ');
            break;
         }
      }
      sprite.setCursor(0, 0);
      sprite.print(value.c_str());
      sprite.pushSprite(_columnX[columnIndex], y);
   }

   ///
   /// <summary>
   /// Draws the column header row (right-aligned titles matching each column's width)
   /// and a thin divider line beneath it, once per invalidate()/construction.
   /// </summary>
   ///
   void _drawHeader()
   {
      if (!_hasColumns() || _headerDrawn)
      {
         return;
      }

      if (_columns[0].title != nullptr)
      {
         std::string title = _columns[0].title;
         int16_t padding = _labelWidth - (int16_t)title.length();

         _display->setCursor(_x, _y);

         if (_labelAlignment == Alignment::RIGHT)
         {
            for (int16_t p = 0; p < padding; p++)
            {
               _display->print(" ", _headerColor);
            }
         }

         _display->print(title.c_str(), _headerColor);

         if (_labelAlignment == Alignment::LEFT)
         {
            for (int16_t p = 0; p < padding; p++)
            {
               _display->print(" ", _headerColor);
            }
         }
      }

      size_t valueColumnCount = _valueColumnCount();
      for (size_t i = 0; i < valueColumnCount; i++)
      {
         Color color = _columns[i + 1].hasColor ? _columns[i + 1].color : _headerColor;
         std::string title = _columns[i + 1].title;
         int16_t charWidth = _columnCharWidths[i];
         if ((int16_t)title.length() < charWidth)
         {
            title = std::string((size_t)(charWidth - title.length()), ' ') + title;
         }

         _display->setCursor(_columnX[i], _y);
         _display->print(title.c_str(), color);
      }

      int16_t lineY = _y + _display->charH();
      int16_t lineWidth = (_columnX[valueColumnCount - 1] + _columnPixelWidths[valueColumnCount - 1]) - _x;
      _display->drawLine(_x, lineY, _x + lineWidth - 1, lineY, _headerColor);

      _headerDrawn = true;
   }

   ///
   /// <summary>
   /// Sets the value of an existing cell, formatting it via the cell's own format.
   /// Shared by every typed setValue() overload.
   /// </summary>
   ///
   template<typename T>
   void _setCellValue(size_t rowIndex, size_t columnIndex, const T& value, Color valueColor)
   {
      if (rowIndex >= _rows.size())
      {
         return;
      }

      RowState& row = _rows[rowIndex];
      if (columnIndex >= row.cells.size())
      {
         return;
      }

      Cell& cell = row.cells[columnIndex];
      cell.value = cell.format.toString(value).c_str();
      cell.valueColor = valueColor;
   }

   ///
   /// <summary>
   /// Sets the value of an existing cell, formatting it via the cell's own format, while
   /// preserving the cell's current value color. Shared by every color-preserving
   /// setValue() overload.
   /// </summary>
   ///
   template<typename T>
   void _setCellValue(size_t rowIndex, size_t columnIndex, const T& value)
   {
      if (rowIndex >= _rows.size())
      {
         return;
      }

      RowState& row = _rows[rowIndex];
      if (columnIndex >= row.cells.size())
      {
         return;
      }

      Cell& cell = row.cells[columnIndex];
      cell.value = cell.format.toString(value).c_str();
   }

public:
   ///
   /// <summary>
   /// Initializes a new instance of the Table class as a legacy single-value
   /// table: each row supplies its own format via addRow(), values are set via
   /// setValue(rowIndex, value, color), and no header row/divider line is drawn.
   /// </summary>
   /// <param name="display">The display interface to draw onto.</param>
   /// <param name="x">The X coordinate for the top-left corner of the table.</param>
   /// <param name="y">The Y coordinate for the top-left corner of the table.</param>
   /// <param name="textSize">The text size applied automatically before drawing labels and values.</param>
   /// <param name="labelAlignment">Alignment of the row label text within its reserved column width (default: RIGHT).</param>
   ///
   Table(ArduinoWithDisplay* display, int16_t x, int16_t y, uint8_t textSize = 2,
      Alignment labelAlignment = Alignment::RIGHT)
      : _display(display), _x(x), _y(y), _labelWidth(0), _labelAlignment(labelAlignment), _textSize(textSize),
        _columns(), _headerColor(Color::TABLE_HEADER)
   {
   }

   ///
   /// <summary>
   /// Initializes a new instance of the Table class as a legacy single-value table,
   /// adding the given rows immediately (equivalent to calling addRows(rows) right after
   /// construction). Values are set via setValue(rowIndex, value, color), and no header
   /// row/divider line is drawn.
   /// </summary>
   /// <param name="display">The display interface to draw onto.</param>
   /// <param name="x">The X coordinate for the top-left corner of the table.</param>
   /// <param name="y">The Y coordinate for the top-left corner of the table.</param>
   /// <param name="rows">Row label/format metadata, one entry per row to add.</param>
   /// <param name="textSize">The text size applied automatically before drawing labels and values.</param>
   /// <param name="labelAlignment">Alignment of the row label text within its reserved column width (default: RIGHT).</param>
   ///
   Table(ArduinoWithDisplay* display, int16_t x, int16_t y, std::span<const ValueRow> rows,
      uint8_t textSize = 2, Alignment labelAlignment = Alignment::RIGHT)
      : _display(display), _x(x), _y(y), _labelWidth(0), _labelAlignment(labelAlignment), _textSize(textSize),
        _columns(), _headerColor(Color::TABLE_HEADER)
   {
      addRows(rows);
   }

   ///
   /// <summary>
   /// Initializes a new instance of the Table class as a multi-column table: each
   /// row's cells are automatically created (one per column) via addRow(label), values
   /// are set per column via setValue(rowIndex, columnIndex, value, color), and a header
   /// row plus a divider line are drawn above the data rows.
   /// </summary>
   /// <param name="display">The display interface to draw onto.</param>
   /// <param name="x">The X coordinate for the top-left corner of the table.</param>
   /// <param name="y">The Y coordinate for the top-left corner of the table.</param>
   /// <param name="columns">Column metadata (title + format), owned by the caller and expected to outlive this object. columns[0] is the row-label column; its alignment governs the row labels/header title.</param>
   /// <param name="textSize">The text size applied automatically before drawing labels, headers, and values.</param>
   /// <param name="headerColor">The color used to draw the column header row (unless a column overrides it).</param>
   ///
   Table(ArduinoWithDisplay* display, int16_t x, int16_t y, std::span<const Column> columns,
      uint8_t textSize = 2, Color headerColor = Color::TABLE_HEADER)
      : _display(display), _x(x), _y(y), _labelWidth(0),
        _labelAlignment(columns.empty() ? Alignment::LEFT : _fromFormatAlignment(columns[0].format.alignment())),
        _textSize(textSize), _columns(columns), _headerColor(headerColor)
   {
   }

   ///
   /// <summary>
   /// Initializes a new instance of the Table class as a multi-column table, adding
   /// the given rows immediately (equivalent to calling addRows(rows) right after
   /// construction). Values are set per column via setValue(rowIndex, columnIndex, value,
   /// color), and a header row plus a divider line are drawn above the data rows.
   /// </summary>
   /// <param name="display">The display interface to draw onto.</param>
   /// <param name="x">The X coordinate for the top-left corner of the table.</param>
   /// <param name="y">The Y coordinate for the top-left corner of the table.</param>
   /// <param name="columns">Column metadata (title + format), owned by the caller and expected to outlive this object. columns[0] is the row-label column; its alignment governs the row labels/header title.</param>
   /// <param name="rows">Row label metadata, one entry per row to add.</param>
   /// <param name="textSize">The text size applied automatically before drawing labels, headers, and values.</param>
   /// <param name="headerColor">The color used to draw the column header row (unless a column overrides it).</param>
   ///
   Table(ArduinoWithDisplay* display, int16_t x, int16_t y, std::span<const Column> columns,
      std::span<const Row> rows, uint8_t textSize = 2, Color headerColor = Color::TABLE_HEADER)
      : _display(display), _x(x), _y(y), _labelWidth(0),
        _labelAlignment(columns.empty() ? Alignment::LEFT : _fromFormatAlignment(columns[0].format.alignment())),
        _textSize(textSize), _columns(columns), _headerColor(headerColor)
   {
      addRows(rows);
   }

   ///
   /// <summary>
   /// Removes all rows from the table and forces the next draw() call to rebuild the
   /// column sprites and redraw everything from scratch, e.g. so addRow() can be called
   /// again to build a completely different set of rows (see FieldTableEditor::setFields()).
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
    /// Adds a new row to a legacy single-value table, parsing the given format string into
    /// a Format owned internally by the table (so callers don't need to declare a named
    /// Format variable for every row), using a non-default label color. Not valid for
    /// tables constructed with an explicit Column array; use addRow(label, labelColor)
    /// instead for those.
    /// </summary>
     /// <param name="label">The text label for the row.</param>
     /// <param name="labelColor">The color to draw the label text.</param>
     /// <param name="formatStr">Pattern containing optional prefix/postfix and # placeholders.</param>
     /// <param name="valueColor">The color to draw the value text (default: Color::VALUE).</param>
     ///
     void addRow(const char* label, Color labelColor, const char* formatStr,
        Color valueColor = Color::VALUE)
     {
        addRow(label, Format(formatStr), labelColor, valueColor);
     }

    ///
    /// <summary>
    /// Adds a new row to a legacy single-value table, parsing the given format string into
    /// a Format owned internally by the table, using an explicit label color. Overload of
    /// addRow(label, labelColor, const char* formatStr, ...) accepting a std::string so
    /// callers don't need to call .c_str() themselves.
    /// </summary>
    /// <param name="label">The text label for the row.</param>
    /// <param name="labelColor">The color to draw the label text.</param>
    /// <param name="formatStr">Pattern containing optional prefix/postfix and # placeholders.</param>
    /// <param name="valueColor">The color to draw the value text (default: Color::VALUE).</param>
    ///
    void addRow(const char* label, Color labelColor, const std::string& formatStr,
       Color valueColor = Color::VALUE)
    {
       addRow(label, labelColor, formatStr.c_str(), valueColor);
    }

     ///
    /// <summary>
    /// Adds a new row to a legacy single-value table, parsing the given format string into
    /// a Format owned internally by the table (so callers don't need to declare a named
    /// Format variable for every row), using the default label color. Not valid for tables
    /// constructed with an explicit Column array; use addRow(label, labelColor) instead for
    /// those.
    /// </summary>
     /// <param name="label">The text label for the row.</param>
     /// <param name="formatStr">Pattern containing optional prefix/postfix and # placeholders.</param>
     /// <param name="valueColor">The color to draw the value text (default: Color::VALUE).</param>
     ///
     void addRow(const char* label, const char* formatStr, Color valueColor = Color::VALUE)
     {
        addRow(label, Color::LABEL, formatStr, valueColor);
     }

     ///
    /// <summary>
    /// Adds a new row to a legacy single-value table, parsing the given format string into
    /// a Format owned internally by the table, using the default label color. Overload of
    /// addRow(label, const char* formatStr, ...) accepting a std::string so callers don't
    /// need to call .c_str() themselves.
    /// </summary>
     /// <param name="label">The text label for the row.</param>
     /// <param name="formatStr">Pattern containing optional prefix/postfix and # placeholders.</param>
     /// <param name="valueColor">The color to draw the value text (default: Color::VALUE).</param>
     ///
     void addRow(const char* label, const std::string& formatStr, Color valueColor = Color::VALUE)
     {
        addRow(label, Color::LABEL, formatStr.c_str(), valueColor);
     }

   ///
   /// <summary>
   /// Adds a new row to a multi-column table, creating one cell per configured column,
   /// each using that column's format and (if set) its fixed color, otherwise
   /// Color::VALUE. Only valid for tables constructed with an explicit Column array.
   /// </summary>
   /// <param name="label">The text label for the row.</param>
   /// <param name="labelColor">The color to draw the label text (default: Color::LABEL).</param>
   ///
   void addRow(const char* label, Color labelColor = Color::LABEL)
   {
      if (!_hasColumns())
      {
         return;
      }

      RowState row(label, labelColor);
      for (size_t i = 1; i < _columns.size(); i++)
      {
         Color valueColor = _columns[i].hasColor ? _columns[i].color : Color::VALUE;
         row.cells.emplace_back(_columns[i].format, valueColor);
      }
      _rows.push_back(std::move(row));

      int16_t labelLen = strlen(label);
      if (labelLen > _labelWidth)
      {
         _labelWidth = labelLen;
      }
   }

   ///
   /// <summary>
   /// Adds every row described by rows, in order, to a multi-column table. Equivalent to
   /// calling addRow(row.label, row.labelColor) once per entry, but lets callers define
   /// their table's rows as a single static array (see Column/addRow(s) above) instead of
   /// looping over a separately tracked row count.
   /// </summary>
   /// <param name="rows">Row label metadata, one entry per row to add.</param>
   ///
   void addRows(std::span<const Row> rows)
   {
      for (const auto& row : rows)
      {
         addRow(row.label, row.labelColor);
      }
   }

   ///
   /// <summary>
   /// Adds every row described by rows, in order, to a legacy single-value table.
   /// Equivalent to calling addRow(row.label, row.labelColor, row.formatStr, row.valueColor)
   /// once per entry, but lets callers define their table's rows as a
   /// single static array instead of looping over a separately tracked row count.
   /// </summary>
   /// <param name="rows">Row label/format metadata, one entry per row to add.</param>
   ///
   void addRows(std::span<const ValueRow> rows)
   {
         for (const auto& row : rows)
         {
            addRow(row.label, row.labelColor, row.formatStr, row.valueColor);
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
   /// Sets the value of an existing row's cell in the given column, for multi-column tables.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="columnIndex">The zero-based index of the column to update.</param>
   /// <param name="value">The new double value to display.</param>
   /// <param name="valueColor">The color to draw the updated value (default: Color::VALUE).</param>
   ///
   void setValue(size_t rowIndex, size_t columnIndex, double value, Color valueColor)
   {
      _setCellValue(rowIndex, columnIndex, value, valueColor);
   }

   ///
   /// <summary>
   /// Sets the value of an existing row's cell in the given column, for multi-column
   /// tables, preserving the cell's current value color.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="columnIndex">The zero-based index of the column to update.</param>
   /// <param name="value">The new double value to display.</param>
   ///
   void setValue(size_t rowIndex, size_t columnIndex, double value)
   {
      _setCellValue(rowIndex, columnIndex, value);
   }

   ///
   /// <summary>
   /// Sets the value of an existing row's cell in column 0, for legacy single-value tables.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="value">The new double value to display.</param>
   /// <param name="valueColor">The color to draw the updated value.</param>
   ///
   void setValue(size_t rowIndex, double value, Color valueColor)
   {
      setValue(rowIndex, (size_t)0, value, valueColor);
   }

   ///
   /// <summary>
   /// Sets the value of an existing row's cell in column 0, for legacy single-value
   /// tables, preserving the cell's current value color.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="value">The new double value to display.</param>
   ///
   void setValue(size_t rowIndex, double value)
   {
      setValue(rowIndex, (size_t)0, value);
   }

   ///
   /// <summary>
   /// Sets the value of an existing row's cell in the given column, for multi-column tables.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="columnIndex">The zero-based index of the column to update.</param>
   /// <param name="value">The new float value to display.</param>
   /// <param name="valueColor">The color to draw the updated value (default: Color::VALUE).</param>
   ///
   void setValue(size_t rowIndex, size_t columnIndex, float value, Color valueColor)
   {
      _setCellValue(rowIndex, columnIndex, (double)value, valueColor);
   }

   ///
   /// <summary>
   /// Sets the value of an existing row's cell in the given column, for multi-column
   /// tables, preserving the cell's current value color.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="columnIndex">The zero-based index of the column to update.</param>
   /// <param name="value">The new float value to display.</param>
   ///
   void setValue(size_t rowIndex, size_t columnIndex, float value)
   {
      _setCellValue(rowIndex, columnIndex, (double)value);
   }

   ///
   /// <summary>
   /// Sets the value of an existing row's cell in column 0, for legacy single-value tables.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="value">The new float value to display.</param>
   /// <param name="valueColor">The color to draw the updated value.</param>
   ///
   void setValue(size_t rowIndex, float value, Color valueColor)
   {
      setValue(rowIndex, (size_t)0, value, valueColor);
   }

   ///
   /// <summary>
   /// Sets the value of an existing row's cell in column 0, for legacy single-value
   /// tables, preserving the cell's current value color.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="value">The new float value to display.</param>
   ///
   void setValue(size_t rowIndex, float value)
   {
      setValue(rowIndex, (size_t)0, value);
   }

   ///
   /// <summary>
   /// Sets the value of an existing row's cell in the given column, for multi-column tables.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="columnIndex">The zero-based index of the column to update.</param>
   /// <param name="value">The new int value to display.</param>
   /// <param name="valueColor">The color to draw the updated value (default: Color::VALUE).</param>
   ///
   void setValue(size_t rowIndex, size_t columnIndex, int value, Color valueColor)
   {
      _setCellValue(rowIndex, columnIndex, (double)value, valueColor);
   }

   ///
   /// <summary>
   /// Sets the value of an existing row's cell in the given column, for multi-column
   /// tables, preserving the cell's current value color.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="columnIndex">The zero-based index of the column to update.</param>
   /// <param name="value">The new int value to display.</param>
   ///
   void setValue(size_t rowIndex, size_t columnIndex, int value)
   {
      _setCellValue(rowIndex, columnIndex, (double)value);
   }

   ///
   /// <summary>
   /// Sets the value of an existing row's cell in column 0, for legacy single-value tables.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="value">The new int value to display.</param>
   /// <param name="valueColor">The color to draw the updated value.</param>
   ///
   void setValue(size_t rowIndex, int value, Color valueColor)
   {
      setValue(rowIndex, (size_t)0, value, valueColor);
   }

   ///
   /// <summary>
   /// Sets the value of an existing row's cell in column 0, for legacy single-value
   /// tables, preserving the cell's current value color.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="value">The new int value to display.</param>
   ///
   void setValue(size_t rowIndex, int value)
   {
      setValue(rowIndex, (size_t)0, value);
   }

   ///
   /// <summary>
   /// Sets the value of an existing row's cell in the given column, for multi-column tables.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="columnIndex">The zero-based index of the column to update.</param>
   /// <param name="value">The new unsigned long value to display.</param>
   /// <param name="valueColor">The color to draw the updated value (default: Color::VALUE).</param>
   ///
   void setValue(size_t rowIndex, size_t columnIndex, unsigned long value, Color valueColor)
   {
      _setCellValue(rowIndex, columnIndex, (double)value, valueColor);
   }

   ///
   /// <summary>
   /// Sets the value of an existing row's cell in the given column, for multi-column
   /// tables, preserving the cell's current value color.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="columnIndex">The zero-based index of the column to update.</param>
   /// <param name="value">The new unsigned long value to display.</param>
   ///
   void setValue(size_t rowIndex, size_t columnIndex, unsigned long value)
   {
      _setCellValue(rowIndex, columnIndex, (double)value);
   }

   ///
   /// <summary>
   /// Sets the value of an existing row's cell in column 0, for legacy single-value tables.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="value">The new unsigned long value to display.</param>
   /// <param name="valueColor">The color to draw the updated value.</param>
   ///
   void setValue(size_t rowIndex, unsigned long value, Color valueColor)
   {
      setValue(rowIndex, (size_t)0, value, valueColor);
   }

   ///
   /// <summary>
   /// Sets the value of an existing row's cell in column 0, for legacy single-value
   /// tables, preserving the cell's current value color.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="value">The new unsigned long value to display.</param>
   ///
   void setValue(size_t rowIndex, unsigned long value)
   {
      setValue(rowIndex, (size_t)0, value);
   }

   ///
   /// <summary>
   /// Sets the value of an existing row's cell in the given column, for multi-column tables.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="columnIndex">The zero-based index of the column to update.</param>
   /// <param name="value">The new size_t value to display.</param>
   /// <param name="valueColor">The color to draw the updated value (default: Color::VALUE).</param>
   ///
   void setValue(size_t rowIndex, size_t columnIndex, size_t value, Color valueColor)
   {
      _setCellValue(rowIndex, columnIndex, (double)value, valueColor);
   }

   ///
   /// <summary>
   /// Sets the value of an existing row's cell in the given column, for multi-column
   /// tables, preserving the cell's current value color.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="columnIndex">The zero-based index of the column to update.</param>
   /// <param name="value">The new size_t value to display.</param>
   ///
   void setValue(size_t rowIndex, size_t columnIndex, size_t value)
   {
      _setCellValue(rowIndex, columnIndex, (double)value);
   }

   ///
   /// <summary>
   /// Sets the value of an existing row's cell in the given column, for multi-column tables.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="columnIndex">The zero-based index of the column to update.</param>
   /// <param name="value">The new String value to display.</param>
   /// <param name="valueColor">The color to draw the updated value (default: Color::VALUE).</param>
   ///
   void setValue(size_t rowIndex, size_t columnIndex, const String& value, Color valueColor)
   {
      _setCellValue(rowIndex, columnIndex, value, valueColor);
   }

   ///
   /// <summary>
   /// Sets the value of an existing row's cell in the given column, for multi-column
   /// tables, preserving the cell's current value color.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="columnIndex">The zero-based index of the column to update.</param>
   /// <param name="value">The new String value to display.</param>
   ///
   void setValue(size_t rowIndex, size_t columnIndex, const String& value)
   {
      _setCellValue(rowIndex, columnIndex, value);
   }

   ///
   /// <summary>
   /// Sets the value of an existing row's cell in column 0, for legacy single-value tables.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="value">The new String value to display.</param>
   /// <param name="valueColor">The color to draw the updated value.</param>
   ///
   void setValue(size_t rowIndex, const String& value, Color valueColor)
   {
      setValue(rowIndex, (size_t)0, value, valueColor);
   }

   ///
   /// <summary>
   /// Sets the value of an existing row's cell in column 0, for legacy single-value
   /// tables, preserving the cell's current value color.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="value">The new String value to display.</param>
   ///
   void setValue(size_t rowIndex, const String& value)
   {
      setValue(rowIndex, (size_t)0, value);
   }

   ///
   /// <summary>
   /// Sets the value of an existing row's cell in the given column, for multi-column tables.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="columnIndex">The zero-based index of the column to update.</param>
   /// <param name="value">The new std::string value to display.</param>
   /// <param name="valueColor">The color to draw the updated value (default: Color::VALUE).</param>
   ///
   void setValue(size_t rowIndex, size_t columnIndex, const std::string& value, Color valueColor)
   {
      _setCellValue(rowIndex, columnIndex, value, valueColor);
   }

   ///
   /// <summary>
   /// Sets the value of an existing row's cell in the given column, for multi-column
   /// tables, preserving the cell's current value color.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="columnIndex">The zero-based index of the column to update.</param>
   /// <param name="value">The new std::string value to display.</param>
   ///
   void setValue(size_t rowIndex, size_t columnIndex, const std::string& value)
   {
      _setCellValue(rowIndex, columnIndex, value);
   }

   ///
   /// <summary>
   /// Sets the value of an existing row's cell in column 0, for legacy single-value tables.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="value">The new std::string value to display.</param>
   /// <param name="valueColor">The color to draw the updated value.</param>
   ///
   void setValue(size_t rowIndex, const std::string& value, Color valueColor)
   {
      setValue(rowIndex, (size_t)0, value, valueColor);
   }

   ///
   /// <summary>
   /// Sets the value of an existing row's cell in column 0, for legacy single-value
   /// tables, preserving the cell's current value color.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="value">The new std::string value to display.</param>
   ///
   void setValue(size_t rowIndex, const std::string& value)
   {
      setValue(rowIndex, (size_t)0, value);
   }

   ///
   /// <summary>
   /// Sets the value of an existing row's cell in the given column, for multi-column tables.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="columnIndex">The zero-based index of the column to update.</param>
   /// <param name="value">The new character array value to display.</param>
   /// <param name="valueColor">The color to draw the updated value (default: Color::VALUE).</param>
   ///
   void setValue(size_t rowIndex, size_t columnIndex, const char* value, Color valueColor)
   {
      _setCellValue(rowIndex, columnIndex, value, valueColor);
   }

   ///
   /// <summary>
   /// Sets the value of an existing row's cell in the given column, for multi-column
   /// tables, preserving the cell's current value color.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="columnIndex">The zero-based index of the column to update.</param>
   /// <param name="value">The new character array value to display.</param>
   ///
   void setValue(size_t rowIndex, size_t columnIndex, const char* value)
   {
      _setCellValue(rowIndex, columnIndex, value);
   }

   ///
   /// <summary>
   /// Sets the value of an existing row's cell in column 0, for legacy single-value tables.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="value">The new character array value to display.</param>
   /// <param name="valueColor">The color to draw the updated value.</param>
   ///
   void setValue(size_t rowIndex, const char* value, Color valueColor)
   {
      setValue(rowIndex, (size_t)0, value, valueColor);
   }

   ///
   /// <summary>
   /// Sets the value of an existing row's cell in column 0, for legacy single-value
   /// tables, preserving the cell's current value color.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="value">The new character array value to display.</param>
   ///
   void setValue(size_t rowIndex, const char* value)
   {
      setValue(rowIndex, (size_t)0, value);
   }

   ///
   /// <summary>
   /// Sets a row's cell to a placeholder string (via the cell format's toNoValueString())
   /// to indicate no value is currently available, as opposed to a legitimate NaN reading.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="columnIndex">The zero-based index of the column to update.</param>
   /// <param name="valueColor">The color to draw the placeholder value (default: Color::VALUE).</param>
   /// <param name="noValueChar">Character used to fill each digit position.</param>
   ///
   void setValueNone(size_t rowIndex, size_t columnIndex, Color valueColor = Color::VALUE, char noValueChar = '-')
   {
      if (rowIndex >= _rows.size())
      {
         return;
      }

      RowState& row = _rows[rowIndex];
      if (columnIndex >= row.cells.size())
      {
         return;
      }

      Cell& cell = row.cells[columnIndex];
      cell.value = cell.format.toNoValueString(noValueChar).c_str();
      cell.valueColor = valueColor;
   }

   ///
   /// <summary>
   /// Sets a row's cell in column 0 to a placeholder string (via the cell format's
   /// toNoValueString()) to indicate no value is currently available, as opposed to a
   /// legitimate NaN reading.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="valueColor">The color to draw the placeholder value (default: Color::VALUE).</param>
   /// <param name="noValueChar">Character used to fill each digit position.</param>
   ///
   void setValueNone(size_t rowIndex, Color valueColor = Color::VALUE, char noValueChar = '-')
   {
      setValueNone(rowIndex, (size_t)0, valueColor, noValueChar);
   }

   ///
   /// <summary>
   /// Sets the background color drawn behind a row's cell, e.g. to highlight a currently
   /// selected/editable row. Defaults to Color::DARKGRAY for every cell.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="columnIndex">The zero-based index of the column to update.</param>
   /// <param name="valueBackgroundColor">The background color to draw behind the value.</param>
   ///
   void setValueBackgroundColor(size_t rowIndex, size_t columnIndex, Color valueBackgroundColor)
   {
      if (rowIndex >= _rows.size())
      {
         return;
      }

      RowState& row = _rows[rowIndex];
      if (columnIndex >= row.cells.size())
      {
         return;
      }

      row.cells[columnIndex].valueBackgroundColor = valueBackgroundColor;
   }

   ///
   /// <summary>
   /// Sets the background color drawn behind a row's cell in column 0, e.g. to highlight a
   /// currently selected/editable row. Defaults to Color::DARKGRAY for every cell.
   /// </summary>
   /// <param name="rowIndex">The zero-based index of the row to update.</param>
   /// <param name="valueBackgroundColor">The background color to draw behind the value.</param>
   ///
   void setValueBackgroundColor(size_t rowIndex, Color valueBackgroundColor)
   {
      setValueBackgroundColor(rowIndex, (size_t)0, valueBackgroundColor);
   }

   ///
   /// <summary>
   /// Sets the color drawn for a row's label text.
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
   /// invalidate()), draws the header row/divider line (if configured) plus labels and
   /// values for every row; on subsequent calls, only redraws the value of a cell when
   /// its text or color has changed, since labels/headers never change and formatted
   /// values share a fixed width, avoiding unnecessary display writes.
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
         _display->setTextSize(_textSize);
      }

      if (!_spritesCreated)
      {
         _createSprites();
      }

      if (!_headerDrawn)
      {
         _drawHeader();
      }

      for (size_t i = 0; i < _rows.size(); i++)
      {
         RowState& row = _rows[i];
         int16_t y = _y + _rowY(i);

         bool labelNeedsDraw = !_labelsDrawn || (row.labelColor != row.drawnLabelColor);

         if (labelNeedsDraw)
         {
            if (_showSections && row.section != nullptr && !_labelsDrawn)
            {
               int16_t rowHeight = _display->charH();
               _display->setCursor(_x, y - rowHeight);
               _display->println(row.section, Color::SECTION_HEADER, Color::BLACK);
            }

            _display->setCursor(_x, y);

            int16_t labelLen = row.label.length();
            int16_t padding = _labelWidth - labelLen;

            if (_labelAlignment == Alignment::RIGHT)
            {
               for (int16_t p = 0; p < padding; p++)
               {
                  _display->print(" ", row.labelColor);
               }
            }

            _display->print(row.label.c_str(), row.labelColor);

            if (_labelAlignment == Alignment::LEFT)
            {
               for (int16_t p = 0; p < padding; p++)
               {
                  _display->print(" ", row.labelColor);
               }
            }

            row.drawnLabelColor = row.labelColor;
         }

         for (size_t c = 0; c < row.cells.size(); c++)
         {
            Cell& cell = row.cells[c];

            if (!_labelsDrawn)
            {
               _drawValueSprite(cell, c, y);

               cell.drawnValue = cell.value;
               cell.drawnValueColor = cell.valueColor;
               cell.drawnValueBackgroundColor = cell.valueBackgroundColor;
            }
            else
            {
               if ((cell.value != cell.drawnValue) || (cell.valueColor != cell.drawnValueColor) ||
                  (cell.valueBackgroundColor != cell.drawnValueBackgroundColor))
               {
                  _drawValueSprite(cell, c, y);

                  cell.drawnValue = cell.value;
                  cell.drawnValueColor = cell.valueColor;
                  cell.drawnValueBackgroundColor = cell.valueBackgroundColor;
               }
            }
         }
      }

      _labelsDrawn = true;
   }

   ///
   /// <summary>
   /// Moves the table so that the given anchor point of its bounding rectangle is placed
   /// at (x, y), and forces the next draw() call to redraw every label and value from
   /// scratch at the new location. The table's width/height (see getWidth()) is used to
   /// compute the top-left position for anchors other than TOP_LEFT, so this reflects the
   /// table's current rows/columns at the time of the call.
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
         int16_t height = _rowY(_rows.size());

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
      _labelsDrawn = false;
      _headerDrawn = false;
      _spritesCreated = false;
   }

   ///
   /// <summary>
   /// Moves the table so that the given anchor point of its bounding rectangle is placed
   /// at pos, and forces the next draw() call to redraw every label and value from
   /// scratch at the new location.
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
   /// Computes the table's bounding rectangle, based on its top-left position, its
   /// content width (see getWidth()), and the total height of all its rows (plus the
   /// header row, if any). Useful for laying out other content (e.g. a value field)
   /// relative to the table's actual bounds.
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
   /// Forces the next draw() call to redraw everything from scratch and rebuild the
   /// per-column sprites (e.g. after the display area was cleared, the table's position
   /// changed, or the font/size changed).
   /// </summary>
   ///
   void invalidate()
   {
      _labelsDrawn = false;
      _headerDrawn = false;
      if (_spritesCreated)
      {
         for (auto& sprite : _sprites)
         {
            sprite.unloadFont();
            sprite.deleteSprite();
         }
         _sprites.clear();
         _spritesCreated = false;
      }
   }

   ///
   /// <summary>
   /// Computes the total pixel width the table needs to display its label and value
   /// column(s), based on the longest label and the widest formatted value/title across
   /// all columns. Useful for laying out other content (e.g. a plot) relative to the
   /// table's actual content width rather than a guessed fixed width.
   /// </summary>
   /// <returns>The required width, in pixels.</returns>
   ///
   int16_t getWidth()
   {
      if (!_spritesCreated)
      {
         _display->setTextSize(_textSize);
         _createSprites();
      }

      size_t columnCount = _valueColumnCount();
      int16_t lastColumnRight = _columnX[columnCount - 1] + _columnPixelWidths[columnCount - 1];
      return lastColumnRight - _x;
   }
};
