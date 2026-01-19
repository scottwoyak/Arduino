#pragma once

#include <string>

class FixedLengthString : public Printable
{
private:
   String _str;

   void _adjustToLength(uint8_t length, char errChar = 0)
   {
      // if no length is provided, there is not alteration
      if (length == 0)
      {
         return;
      }

      if (_str.length() <= length)
      {
         while (_str.length() < length)
         {
            _str += " ";
         }
      }
      else
      {
         // if errChar==0, we truncate the string
         if (errChar == 0)
         {
            if (_str.length() > length)
            {
               _str = _str.substring(0, length - 1);
            }
         }
         // otherwise we replace it with errChar, e.g. "####"
         else
         {
            _str.clear();
            while (_str.length() < length)
            {
               _str += errChar;
            }
         }
      }
   }

public:
   FixedLengthString(double value, uint8_t length, uint8_t precision) : _str(value, (int)precision)
   {
      _adjustToLength(length, '#');
   }
   FixedLengthString(float value, uint8_t length, uint8_t precision) : _str(value, (int)precision)
   {
      _adjustToLength(length, '#');
   }
   FixedLengthString(long value, uint8_t length, uint8_t base) : _str(value)
   {
      _adjustToLength(length, '#');
   }
   FixedLengthString(int value, uint8_t length, uint8_t base) : _str(value, base)
   {
      _adjustToLength(length, '#');
   }

   FixedLengthString(const char* str, uint8_t length) : _str(str)
   {
      _adjustToLength(length);
   }
   FixedLengthString(const String& str, uint8_t length) : _str(str)
   {
      _adjustToLength(length);
   }
   FixedLengthString(const std::string& str, uint8_t length) : _str(str.c_str())
   {
      _adjustToLength(length);
   }

   virtual size_t printTo(Print& p) const
   {
      return p.print(_str);
   }
};


