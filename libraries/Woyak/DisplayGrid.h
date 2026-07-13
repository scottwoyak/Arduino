#pragma once

#include "ArduinoWithDisplay.h"
#include "Format.h"
#include "Color.h"
#include <string.h>
#include <new>

///
/// <summary>
/// Lightweight fixed-width multi-column table helper for drawing static grids on an
/// Arduino display, mirroring SerialTable's title/header/divider/row layout. Intended
/// for tables that are drawn once (or infrequently), such as post-capture summary tables;
/// it always prints directly to the display rather than tracking per-cell dirty state,
/// so it is not intended for high-frequency redraw loops (use DisplayField/DisplayTable
/// for that instead).
/// </summary>
///
class DisplayGrid
{
public:
   ///
   /// <summary>
   /// Describes a single column: its header title and the format used to render cell values.
   /// </summary>
   ///
   struct Column
   {
      const char* title;
      const Format* format;
   };

private:
   ArduinoWithDisplay* _display;
   const char* _title;
   const Column* _columns;
   size_t _columnCount;
   Color _headerColor;

   // Raw storage for the per-column effective Format objects, since Format has no default
   // constructor; each slot is constructed via placement new in _buildEffectiveFormats().
   void* _effectiveFormatsStorage = nullptr;

   ///
   /// <summary>
   /// Gets a pointer to the effective Format for the given column index.
   /// </summary>
   ///
   Format* _effectiveFormat(size_t index) const
   {
      return reinterpret_cast<Format*>(_effectiveFormatsStorage) + index;
   }

   ///
   /// <summary>
   /// Builds one right-aligned Format per column, widened (if needed) to fit the column's
   /// header title, so header and value cells always share the same column width with a
   /// single blank space separating columns.
   /// </summary>
   ///
   void _buildEffectiveFormats()
   {
      _effectiveFormatsStorage = ::operator new(_columnCount * sizeof(Format));

      for (size_t i = 0; i < _columnCount; i++)
      {
         const Format& format = *_columns[i].format;
         size_t titleLength = strlen(_columns[i].title);
         size_t width = max(titleLength, format.length());

         const std::string& pattern = format.formatString();
         if (!pattern.empty())
         {
            new (_effectiveFormat(i)) Format(pattern.c_str(), width, Format::Alignment::RIGHT);
         }
         else
         {
            new (_effectiveFormat(i)) Format(width, Format::Alignment::RIGHT);
         }
      }
   }

   ///
   /// <summary>
   /// Computes the total pixel width of the table: the sum of each column's effective
   /// character width plus one blank-space separator between columns, in the current
   /// monospaced character width.
   /// </summary>
   ///
   int16_t _tableWidth() const
   {
      size_t totalChars = 0;
      for (size_t i = 0; i < _columnCount; i++)
      {
         totalChars += _effectiveFormat(i)->length();
      }
      totalChars += (_columnCount > 0) ? (_columnCount - 1) : 0;

      return static_cast<int16_t>(totalChars * _display->charW());
   }

   ///
   /// <summary>
   /// Base case for recursive row value printing.
   /// </summary>
   ///
   void _printValues(size_t&, Color) const
   {
   }

   ///
   /// <summary>
   /// Recursively prints row values using each column's effective Format, inserting a
   /// single space between columns and a newline after the last column.
   /// </summary>
   ///
   template<typename T, typename... Rest>
   void _printValues(size_t& index, Color valueColor, const T& value, const Rest&... rest) const
   {
      if (index >= _columnCount)
      {
         return;
      }

      bool isLast = (index + 1 >= _columnCount);
      const Format& format = *_effectiveFormat(index);

      _display->print(value, format, valueColor);
      if (isLast)
      {
         _display->println();
      }
      else
      {
         _display->print(" ", valueColor);
      }

      index++;
      if (!isLast)
      {
         _printValues(index, valueColor, rest...);
      }
   }

public:
   ///
   /// <summary>
   /// Constructs a DisplayGrid with the given title and column metadata. Each column's
   /// display width is automatically computed to fit the wider of its header title and
   /// its Format's configured width, and values are right-aligned with a single blank
   /// space separating columns.
   /// </summary>
   /// <param name="display">The display interface to draw onto.</param>
   /// <param name="title">Optional title printed above the header row, or nullptr for none.</param>
   /// <param name="columns">Column metadata (title + Format), owned by the caller and expected to outlive this object.</param>
   /// <param name="columnCount">Number of columns.</param>
   /// <param name="headerColor">The color used to draw the title and column header row.</param>
   ///
   DisplayGrid(ArduinoWithDisplay* display, const char* title, const Column* columns, size_t columnCount,
               Color headerColor = Color::VALUE3)
      : _display(display), _title(title), _columns(columns), _columnCount(columnCount), _headerColor(headerColor)
   {
      _buildEffectiveFormats();
   }

   ///
   /// <summary>
   /// Destroys the automatically computed per-column formats and frees their storage.
   /// </summary>
   ///
   ~DisplayGrid()
   {
      for (size_t i = 0; i < _columnCount; i++)
      {
         _effectiveFormat(i)->~Format();
      }
      ::operator delete(_effectiveFormatsStorage);
   }

   DisplayGrid(const DisplayGrid&) = delete;
   DisplayGrid& operator=(const DisplayGrid&) = delete;

   ///
   /// <summary>
   /// Draws the optional title, the column header row, and a thin white divider line
   /// beneath it.
   /// </summary>
   ///
   void printHeader() const
   {
      if ((_columns == nullptr) || (_columnCount == 0))
      {
         return;
      }

      if (_title != nullptr)
      {
         _display->println(_title, _headerColor);
      }

      for (size_t i = 0; i < _columnCount; i++)
      {
         bool isLast = (i + 1 >= _columnCount);
         const Format& format = *_effectiveFormat(i);

         _display->print(_columns[i].title, format, _headerColor);
         if (isLast)
         {
            _display->println();
         }
         else
         {
            _display->print(" ", _headerColor);
         }
      }

      int16_t lineY = _display->getCursorY();
      int16_t lineWidth = _tableWidth();
      _display->drawLine(0, lineY, lineWidth - 1, lineY, Color::WHITE);
      _display->setCursor(0, lineY + 2);
   }

   ///
   /// <summary>
   /// Draws one row of values, one per configured column, using each column's Format.
   /// </summary>
   /// <param name="valueColor">The color used to draw every value in this row.</param>
   /// <param name="values">Row values, one per column, in column order.</param>
   ///
   template<typename... Args>
   void printRow(Color valueColor, const Args&... values) const
   {
      if ((_columns == nullptr) || (_columnCount == 0))
      {
         return;
      }

      size_t index = 0;
      _printValues(index, valueColor, values...);
   }
};
