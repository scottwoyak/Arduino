#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>
#include <span>
#include "Format.h"
#include "Logger.h"
#include "SerialX.h"

/// <summary>
/// Lightweight fixed-width table helper.
/// </summary>
/// <remarks>
/// Builds formatted, fixed-width table text (title, header, divider, rows) and prints it,
/// by default to Logger.log() (which also echoes to Serial and forwards to the LogServer),
/// or to Serial only if toLogger is passed as false. The built text is also returned in
/// case the caller wants to use it elsewhere (e.g. a display view).
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

       /// <summary>
       /// Builds a divider line ('-' repeated across each column's width) without printing it.
       /// </summary>
       String _dividerText() const
       {
          String divider;
          for (size_t i = 0; i < _columns.size(); i++)
          {
             for (size_t j = 0; j < _columns[i].width; j++)
             {
                divider += '-';
             }
          }

          divider += '\n';
          return divider;
       }

       /// <summary>
       /// Prints text either to Logger (which also echoes to Serial) or directly to Serial only.
       /// </summary>
       /// <param name="text">Text to print.</param>
       /// <param name="toLogger">If true, sends text to Logger.log(); if false, prints to Serial only.</param>
       static void _print(const String& text, bool toLogger)
       {
          if (toLogger)
          {
             Logger.log(text.c_str());
          }
          else
          {
             SerialX::print(text);
          }
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
   /// Prints the title, column header row, and divider row.
   /// </summary>
   /// <param name="toLogger">If true (the default), sends the text to Logger.log() (which also echoes to Serial); if false, prints to Serial only.</param>
   /// <returns>The printed lines, each terminated with '\n', concatenated together.</returns>
   ///
   String printHeader(bool toLogger = true) const
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

      output += _dividerText();

      _print(output, toLogger);
      return output;
   }

       /// <summary>
       /// Prints a divider row based on column widths.
       /// </summary>
       /// <param name="toLogger">If true (the default), sends the text to Logger.log() (which also echoes to Serial); if false, prints to Serial only.</param>
       /// <returns>The printed divider line, terminated with '\n'.</returns>
       ///
       String printDivider(bool toLogger = true) const
       {
          if (!_isConfigured())
          {
             return String();
          }

          String divider = _dividerText();
          _print(divider, toLogger);
          return divider;
       }

       /// <summary>
       /// Prints one row of values using configured columns.
       /// </summary>
       /// <param name="toLogger">If true (the default), sends the text to Logger.log() (which also echoes to Serial); if false, prints to Serial only.</param>
       /// <returns>The printed row line, terminated with '\n'.</returns>
       ///
       template<typename... Args>
       String printRow(bool toLogger, const Args&... values) const
       {
          if (!_isConfigured())
          {
             return String();
          }

          String line;
          size_t index = 0;
          _appendValues(line, index, values...);
          line += '\n';

          _print(line, toLogger);
          return line;
       }

       /// <summary>
       /// Overload of printRow(bool, ...) defaulting toLogger to true.
       /// </summary>
       /// <returns>The printed row line, terminated with '\n'.</returns>
       ///
       template<typename... Args>
       String printRow(const Args&... values) const
       {
          return printRow(true, values...);
       }
   };
