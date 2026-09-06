#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>
#include <span>
#include "Format.h"
#include "SerialX.h"

/// <summary>
/// Lightweight fixed-width serial table helper.
/// </summary>
/// <remarks>
/// Builds formatted, fixed-width table text (title, header, divider, rows) and prints it
/// directly to Serial. The built text is also returned in case the caller wants to use it
/// elsewhere (e.g. a display view).
/// </remarks>
class SerialTable
{
public:
   struct Column
   {
      const char* title;
      size_t width;
      const char* format = nullptr; // Optional Format pattern (e.g. "##.#%") applied to float/double row values; ignored for other types.
   };

   struct FixedFloat
   {
      float value;
      uint8_t decimals;
   };

private:
   const char* _title = nullptr;
   std::span<const Column> _columns;

   /// <summary>
   /// Returns true when table metadata is available for printing.
   /// </summary>
   bool _isConfigured() const
   {
      return !_columns.empty();
   }

   /// <summary>
   /// Left-aligns text within the given width by right-padding with spaces (no-op if the
   /// text is already at least as wide as the requested width).
   /// </summary>
   static String _pad(const String& text, size_t width)
   {
      String result = text;
      for (size_t i = text.length(); i < width; i++)
      {
         result += ' ';
      }
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
      if (index >= _columns.size())
      {
         return;
      }

      line += _formatValue(value, _columns[index]);
      index++;
      _appendValues(line, index, rest...);
   }

   /// <summary>
   /// Formats a fixed-decimal floating-point value with width-based alignment.
   /// </summary>
   static String _formatValue(const FixedFloat& value, const Column& column)
   {
      return _pad(String(value.value, (unsigned int)value.decimals), column.width);
   }

   /// <summary>
   /// Formats a double value, applying the column's format pattern if one is set,
   /// otherwise falling back to width-based alignment.
   /// </summary>
   static String _formatValue(double value, const Column& column)
   {
      if (column.format != nullptr)
      {
         Format fmt(column.format, column.width, Format::Alignment::LEFT);
         return String(fmt.toString(value).c_str());
      }

      return _pad(String(value), column.width);
   }

   /// <summary>
   /// Formats a float value, applying the column's format pattern if one is set,
   /// otherwise falling back to width-based alignment.
   /// </summary>
   static String _formatValue(float value, const Column& column)
   {
      return _formatValue((double)value, column);
   }

   /// <summary>
   /// Formats a value with width-based alignment.
   /// </summary>
   template<typename T>
   static String _formatValue(const T& value, const Column& column)
   {
      return _pad(String(value), column.width);
   }

   /// <summary>
   /// Formats a C-string value with width-based alignment.
   /// </summary>
   static String _formatValue(const char* value, const Column& column)
   {
      return _pad(String(value), column.width);
   }

public:
   /// <summary>
   /// Creates a serial table with title and fixed-width column metadata.
   /// </summary>
   /// <param name="title">Optional title printed above the header row, or nullptr for none.</param>
   /// <param name="columns">Column metadata (title and width) for the table, owned by the caller and expected to outlive this object.</param>
   explicit SerialTable(const char* title, std::span<const Column> columns)
      : _title(title), _columns(columns)
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
   /// Prints the title, column header row, and divider row to Serial.
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
         output += '\n';

         output += _title;
         output += '\n';
      }

      String headerLine;
      for (size_t i = 0; i < _columns.size(); i++)
      {
         headerLine += _formatValue(_columns[i].title, _columns[i]);
      }
      output += headerLine;
      output += '\n';

      output += printDivider(false);

      SerialX::print(output);
      return output;
   }

   /// <summary>
   /// Prints a divider row based on column widths to Serial.
   /// </summary>
   /// <param name="printToSerial">Whether to print the divider to Serial (true), or just build and return the divider text (false, used internally by printHeader to avoid double-printing).</param>
   /// <returns>The printed divider line, terminated with '\n'.</returns>
   ///
   String printDivider(bool printToSerial = true) const
   {
      if (!_isConfigured())
      {
         return String();
      }

      String divider;
      for (size_t i = 0; i < _columns.size(); i++)
      {
         for (size_t j = 0; j < _columns[i].width; j++)
         {
            divider += '-';
         }
      }

      divider += '\n';

      if (printToSerial)
      {
         SerialX::print(divider);
      }

      return divider;
   }

   /// <summary>
   /// Prints one row of values using configured columns to Serial.
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
      line += '\n';

      SerialX::print(line);
      return line;
   }
};
