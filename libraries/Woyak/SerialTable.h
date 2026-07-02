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
   /// Base case for recursive row value printing.
   /// </summary>
   void _printValues(size_t&) const
   {
   }

   /// <summary>
   /// Recursively prints row values using column widths.
   /// </summary>
   template<typename T, typename... Rest>
   void _printValues(size_t& index, const T& value, const Rest&... rest) const
   {
      if (index >= _columnCount)
      {
         return;
      }

      if (index + 1 >= _columnCount)
      {
         _printlnValue(value, _columns[index].width);
         index++;
         return;
      }

      _printValue(value, _columns[index].width);
      index++;
      _printValues(index, rest...);
   }

   /// <summary>
   /// Prints a fixed-decimal floating-point value.
   /// </summary>
   static void _printValue(const FixedFloat& value, size_t width)
   {
      SerialX::print(value.value, value.decimals, width);
   }

   /// <summary>
   /// Prints a fixed-decimal floating-point value and ends the line.
   /// </summary>
   static void _printlnValue(const FixedFloat& value, size_t width)
   {
      SerialX::println(value.value, value.decimals, width);
   }

   /// <summary>
   /// Prints a value with width-based alignment.
   /// </summary>
   template<typename T>
   static void _printValue(const T& value, size_t width)
   {
      SerialX::print(value, width);
   }

   /// <summary>
   /// Prints a value with width-based alignment and ends the line.
   /// </summary>
   template<typename T>
   static void _printlnValue(const T& value, size_t width)
   {
      SerialX::println(value, width);
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
   void printHeader() const
   {
      if (!_isConfigured())
      {
         return;
      }

      if (_title != nullptr)
      {
         Serial.println();
         Serial.println(_title);
      }

      size_t lastIndex = _columnCount - 1;
      for (size_t i = 0; i < lastIndex; i++)
      {
         _printValue(_columns[i].title, _columns[i].width);
      }
      _printlnValue(_columns[lastIndex].title, _columns[lastIndex].width);

      printDivider();
   }

   /// <summary>
   /// Prints a divider row based on column widths.
   /// </summary>
   void printDivider() const
   {
      if (!_isConfigured())
      {
         return;
      }

      for (size_t i = 0; i < _columnCount; i++)
      {
         for (size_t j = 0; j < _columns[i].width; j++)
         {
            Serial.print('-');
         }
      }
      Serial.println();
   }

   /// <summary>
   /// Prints one row of values using configured columns.
   /// </summary>
   template<typename... Args>
   void printRow(const Args&... values) const
   {
      if (!_isConfigured())
      {
         return;
      }

      size_t index = 0;
      _printValues(index, values...);

      if (index < _columnCount)
      {
         Serial.println();
      }
   }
};
