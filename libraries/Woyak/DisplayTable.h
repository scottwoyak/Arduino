#pragma once

#include "ArduinoWithDisplay.h"
#include "Format.h"
#include "Color.h"
#include <vector>

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

      Row(const char* lbl, const Format* fmt, Color lc, Color vc)
         : label(lbl), format(fmt), value(""), labelColor(lc), valueColor(vc)
      {
      }
   };

   std::vector<Row> _rows;
   ArduinoWithDisplay* _display;
   int16_t _x;
   int16_t _y;
   int16_t _labelWidth;

public:
   DisplayTable(ArduinoWithDisplay* display, int16_t x, int16_t y)
      : _display(display), _x(x), _y(y), _labelWidth(0)
   {
   }

   void addRow(const char* label, const Format& format,
               Color labelColor = Color::LABEL, Color valueColor = Color::VALUE)
   {
      _rows.emplace_back(label, &format, labelColor, valueColor);
      int16_t labelLen = strlen(label) + 2;
      if (labelLen > _labelWidth)
      {
         _labelWidth = labelLen;
      }
   }

   template <typename TValue>
   void setValue(size_t rowIndex, const TValue& value, Color valueColor = Color::VALUE)
   {
      if (rowIndex < _rows.size())
      {
         _rows[rowIndex].value = _rows[rowIndex].format->toString(value).c_str();
         _rows[rowIndex].valueColor = valueColor;
      }
   }

   void draw()
   {
      if (_display == nullptr)
      {
         return;
      }

      int16_t y = _y;

      for (const auto& row : _rows)
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

         y += _display->charH();
      }
   }
};
