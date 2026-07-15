#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

#include "SerialX.h"

/// <summary>
/// Lightweight fixed-width serial table helper.
/// </summary>
/// <remarks>
/// Uses SerialX for aligned output and avoids dynamic allocation.
/// </remarks>
class SerialTable
{
public:
   struct Column
   {
      const char* title;
      size_t width;
   };

   struct FixedFloat
   {
      float value;
      uint8_t decimals;
   };

private:
   const char* _title = nullptr;
   const Column* _columns = nullptr;
   size_t _columnCount = 0;

   /// <summary>
   /// Returns true when table metadata is available for printing.
   /// </summary>
   bool _isConfigured() const
   {
      return (_columns != nullptr) && (_columnCount > 0);
   }

   /// <summary>
   /// Right-aligns text within the given width by left-padding with spaces (no-op if the
   /// text is already at least as wide as the requested width).
   /// </summary>
   static String _pad(const String& text, size_t width)
   {
      String result;
      for (size_t i = text.length(); i < width; i++)
      {
         result += ' ';
      }
      result += text;
      return result;
   }

   /// <summary>
   /// Base case for recursive row value formatting.
   /// </summary>
   void _appendValues(String&, size_t&) const
   {
   }

   /// <summary>
   /// Recursively appends formatted row values (space-separated by column width) to line.
   /// </summary>
   template<typename T, typename... Rest>
   void _appendValues(String& line, size_t& index, const T& value, const Rest&... rest) const
   {
      if (index >= _columnCount)
      {
         return;
      }

      line += _formatValue(value, _columns[index].width);
      index++;
      _appendValues(line, index, rest...);
   }

   /// <summary>
   /// Formats a fixed-decimal floating-point value with width-based alignment.
   /// </summary>
   static String _formatValue(const FixedFloat& value, size_t width)
   {
      return _pad(String(value.value, (unsigned int)value.decimals), width);
   }

   /// <summary>
   /// Formats a value with width-based alignment.
   /// </summary>
   template<typename T>
   static String _formatValue(const T& value, size_t width)
   {
      return _pad(String(value), width);
   }

   /// <summary>
   /// Formats a C-string value with width-based alignment.
   /// </summary>
   static String _formatValue(const char* value, size_t width)
   {
      return _pad(String(value), width);
   }

public:
   /// <summary>
   /// Creates a serial table with title and fixed-width column metadata.
   /// </summary>
   explicit SerialTable(const char* title, const Column* columns, size_t columnCount)
      : _title(title), _columns(columns), _columnCount(columnCount)
   {
   }

   /// <summary>
   /// Wraps a float with a fixed decimal precision for table output.
   /// </summary>
   static FixedFloat fixed(float value, uint8_t decimals)
   {
      return FixedFloat{ value, decimals };
   }

   /// <summary>
   /// Prints the title, column header row, and divider row.
   /// </summary>
   /// <returns>The printed lines, each terminated with '\n', concatenated together.</returns>
   ///
   String printHeader() const
   {
      if (!_isConfigured())
      {
         return String();
      }

      String output;
      if (_title != nullptr)
      {
         SerialX::println();
         output += '\n';

         SerialX::println(_title);
         output += _title;
         output += '\n';
      }

      String headerLine;
      for (size_t i = 0; i < _columnCount; i++)
      {
         headerLine += _formatValue(_columns[i].title, _columns[i].width);
      }
      SerialX::println(headerLine);
      output += headerLine;
      output += '\n';

      output += printDivider();
      return output;
   }

   /// <summary>
   /// Prints a divider row based on column widths.
   /// </summary>
   /// <returns>The printed divider line, terminated with '\n'.</returns>
   ///
   String printDivider() const
   {
      if (!_isConfigured())
      {
         return String();
      }

      String divider;
      for (size_t i = 0; i < _columnCount; i++)
      {
         for (size_t j = 0; j < _columns[i].width; j++)
         {
            divider += '-';
         }
      }
      SerialX::println(divider);

      return divider + '\n';
   }

   /// <summary>
   /// Prints one row of values using configured columns.
   /// </summary>
   /// <returns>The printed row line, terminated with '\n'.</returns>
   ///
   template<typename... Args>
   String printRow(const Args&... values) const
   {
      if (!_isConfigured())
      {
         return String();
      }

      String line;
      size_t index = 0;
      _appendValues(line, index, values...);
      SerialX::println(line);

      return line + '\n';
   }
};
