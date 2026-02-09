#pragma once

#include <string>

// boards that define round cause problems for us using std::round()
#ifdef round
#undef round
#endif

class Format
{
public:
   enum class Alignment
   {
      LEFT,
      RIGHT,
      CENTER
   };

   std::string prefix = "";
   std::string postfix = "";
   size_t length = 0;
   Alignment alignment = Alignment::LEFT;
   uint8_t precision = 2;
   char errChar = '#';
   bool includePlus = false;

   //----------------------------------------------------------------------------------------------
   Format(size_t length)
   {
      this->length = length;
   }

   //----------------------------------------------------------------------------------------------
   Format(const char* format, Alignment alignment=Alignment::LEFT)
   {
      alignment = alignment;
      std::string str = format;

      // extract the length
      length = str.length();

      // extract the prefix
      while (str.length() > 0)
      {
         if (str[0] == '#' || str[0] == '+')
         {
            break;
         }
         else
         {
            prefix.append(1, str[0]);
            str.erase(0, 1);
         }
      }

      if (str[0] == '+')
      {
         includePlus = true;
         str.erase(0, 1);
      }

      // extract the postfix
      while (str.length() > 0)
      {
         size_t pos = str.length() - 1;

         if (str[pos] == '#')
         {
            break;
         }
         postfix.insert(0, 1, str[pos]);
         str.erase(pos, 1);
      }

      // extract the precision
      size_t pos = str.find_last_of('.');
      precision = pos == std::string::npos ? 0 : (str.length() - 1) - pos;
   }

   //----------------------------------------------------------------------------------------------
   std::string toString(double value) const
   {
      std::string valueStr;

      // avoid the -0.00 result
      double v = value * std::pow(10, precision);
      if (std::fabs(v) < 1)
      {
         if (includePlus)
         {
            valueStr.append(1, ' ');
         }
         value = 0;
      }

      if (includePlus && value > 0)
      {
         valueStr.append(1, '+');
      }

      if (precision == 0)
      {
         // String has a bug for precision 0 - it adds a blank space to start the string
         valueStr += std::to_string((long)std::round(value));
      }
      else
      {
         valueStr += String(value, (uint)precision).c_str();
      }

      // build the desired full string
      std::string str = prefix + valueStr + postfix;

      return toString(str);
   }

   //----------------------------------------------------------------------------------------------
   std::string toString(String value) const
   {
      return toString(value.c_str());
   }

   //----------------------------------------------------------------------------------------------
   std::string toString(std::string value) const
   {
      return toString(value.c_str());
   }

   //----------------------------------------------------------------------------------------------
   std::string toString(const char* value) const
   {
      std::string str = value;

      // determine how to actually display it
      if (str.length() > length)
      {
         str.clear();
         str.append(length, errChar);
      }
      else if (str.length() < length)
      {
         switch (alignment)
         {
         case Format::Alignment::LEFT:
            str.append(length - str.length(), ' ');
            break;

         case Format::Alignment::RIGHT:
         {
            std::string pre(length - str.length(), ' ');
            str = pre + str;
         }
         break;

         case Format::Alignment::CENTER:
         {
            size_t fill = length - str.length();
            size_t fillLeft = fill / 2;
            size_t fillRight = fill - fillLeft;
            std::string pre(fillLeft, ' ');
            std::string post(fillRight, ' ');
            str = pre + str + post;
         }
         break;
         }
      }

      return str;
   }
};
