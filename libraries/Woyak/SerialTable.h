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

public:
   SerialTable(const char* title, const Column* columns, size_t columnCount)
      : _title(title), _columns(columns), _columnCount(columnCount)
   {
   }

   static FixedFloat fixed(float value, uint8_t decimals)
   {
      return FixedFloat{ value, decimals };
   }

   void printHeader() const
   {
      if (_columns == nullptr || _columnCount == 0)
      {
         return;
      }

      if (_title != nullptr)
      {
         Serial.println();
         Serial.println(_title);
      }

      for (size_t i = 0; i < _columnCount; i++)
      {
         if (i + 1 < _columnCount)
         {
            SerialX::print(_columns[i].title, _columns[i].width);
         }
         else
         {
            SerialX::println(_columns[i].title, _columns[i].width);
         }
      }

      printDivider();
   }

   void printDivider() const
   {
      if (_columns == nullptr || _columnCount == 0)
      {
         return;
      }

      for (size_t i = 0; i < _columnCount; i++)
      {
         String dashes;
         dashes.reserve(_columns[i].width);
         for (size_t j = 0; j < _columns[i].width; j++)
         {
            dashes += '-';
         }

         if (i + 1 < _columnCount)
         {
            SerialX::print(dashes, _columns[i].width);
         }
         else
         {
            SerialX::println(dashes, _columns[i].width);
         }
      }
   }

   template<typename... Args>
   void printRow(const Args&... values) const
   {
      if (_columns == nullptr || _columnCount == 0)
      {
         return;
      }

      size_t index = 0;
      printValues(index, values...);

      if (index < _columnCount)
      {
         Serial.println();
      }
   }

private:
   void printValues(size_t&) const
   {
   }

   template<typename T, typename... Rest>
   void printValues(size_t& index, const T& value, const Rest&... rest) const
   {
      if (index >= _columnCount)
      {
         return;
      }

      bool endLine = (index + 1 >= _columnCount);
      printValue(value, _columns[index].width, endLine);
      index++;

      if (!endLine)
      {
         printValues(index, rest...);
      }
   }

   static void printValue(const FixedFloat& value, size_t width, bool endLine)
   {
      if (endLine)
      {
         SerialX::println(value.value, value.decimals, width);
      }
      else
      {
         SerialX::print(value.value, value.decimals, width);
      }
   }

   template<typename T>
   static void printValue(const T& value, size_t width, bool endLine)
   {
      if (endLine)
      {
         SerialX::println(value, width);
      }
      else
      {
         SerialX::print(value, width);
      }
   }
};
