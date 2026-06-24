#pragma once

#include <string>
#include <cmath>
#include <Arduino.h>

// boards that define round cause problems for us using std::round()
#ifdef round
#undef round
#endif

/// <summary>
/// Represents a lightweight format template used to convert values to fixed-width display strings.
/// </summary>
class Format
{
public:
   /// <summary>
   /// Controls horizontal alignment when formatted content is shorter than the target length.
   /// </summary>
   enum class Alignment
   {
      LEFT,
      RIGHT,
      CENTER
   };

private:
   std::string _prefix = "";
   std::string _postfix = "";
   size_t _length = 0;
   Alignment _alignment = Alignment::LEFT;
   uint8_t _precision = 2;
   char _errChar = '#';
   bool _includePlus = false;

public:
   /// <summary>
   /// Gets the literal prefix placed before the formatted value.
   /// </summary>
   std::string prefix() const { return _prefix; }

   /// <summary>
   /// Gets the literal postfix placed after the formatted value.
   /// </summary>
   std::string postfix() const { return _postfix; }

   /// <summary>
   /// Gets the target output length.
   /// </summary>
   size_t length() const { return _length; }

   /// <summary>
   /// Gets the alignment used when padding output.
   /// </summary>
   Alignment alignment() const { return _alignment; }

   /// <summary>
   /// Gets the number of decimal digits used for floating-point values.
   /// </summary>
   uint8_t precision() const { return _precision; }

   /// <summary>
   /// Gets the character used when content exceeds the target length.
   /// </summary>
   char errChar() const { return _errChar; }

   /// <summary>
   /// Gets whether positive values include a leading plus sign.
   /// </summary>
   bool includePlus() const { return _includePlus; }

   /// <summary>
   /// Initializes a format with an explicit fixed output length.
   /// </summary>
   /// <param name="length">Total output width to enforce.</param>
   /// <param name="alignment">Padding alignment for values shorter than the target length.</param>
   Format(size_t length, Alignment alignment = Alignment::LEFT)
   {
      _length = length;
      _alignment = alignment;
   }

   /// <summary>
   /// Initializes a format by parsing a format pattern and optional alignment.
   /// </summary>
   /// <param name="format">Pattern containing optional prefix/postfix and # placeholders.</param>
   /// <param name="alignment">Padding alignment for values shorter than the target length.</param>
   Format(const char* format, Alignment alignment = Alignment::LEFT)
   {
      _alignment = alignment;
      std::string str = format;

      // extract the length
      _length = str.length();

      // extract the prefix
      while (str.length() > 0)
      {
         if (str[0] == '#' || str[0] == '+')
         {
            break;
         }
         else
         {
            _prefix.append(1, str[0]);
            str.erase(0, 1);
         }
      }

      if (!str.empty() && str[0] == '+')
      {
         _includePlus = true;
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
         _postfix.insert(0, 1, str[pos]);
         str.erase(pos, 1);
      }

      // extract the precision
      size_t pos = str.find_last_of('.');
      _precision = pos == std::string::npos ? 0 : (str.length() - 1) - pos;
   }

   /// <summary>
   /// Initializes a parsed format while overriding the output length.
   /// </summary>
   /// <param name="format">Pattern containing optional prefix/postfix and # placeholders.</param>
   /// <param name="length">Total output width to enforce.</param>
   /// <param name="alignment">Padding alignment for values shorter than the target length.</param>
   Format(const char* format, size_t length, Alignment alignment = Alignment::LEFT)
      : Format(format, alignment)
   {
      _length = length;
   }

   /// <summary>
   /// Formats a floating-point value using this format definition.
   /// </summary>
   /// <param name="value">Value to format.</param>
   /// <returns>Formatted display string.</returns>
   std::string toString(double value) const
   {
      std::string valueStr;

      // avoid the -0.00 result, but only when the value would round to zero
      const double epsilon = 0.5 / std::pow(10.0, _precision);
      if (std::fabs(value) < epsilon)
      {
         if (_includePlus)
         {
            valueStr.append(1, ' ');
         }
         value = 0;
      }

      if (_includePlus && value > 0)
      {
         valueStr.append(1, '+');
      }

      if (_precision == 0)
      {
         // String has a bug for precision 0 - it adds a blank space to start the string
         valueStr += std::to_string((long)std::round(value));
      }
      else
      {
         valueStr += String(value, (unsigned int)_precision).c_str();
      }

      // build the desired full string
      std::string str = _prefix + valueStr + _postfix;

      return toString(str);
   }

   /// <summary>
   /// Formats an Arduino String value using this format definition.
   /// </summary>
   /// <param name="value">String value to format.</param>
   /// <returns>Formatted display string.</returns>
   std::string toString(const String& value) const
   {
      return toString(value.c_str());
   }

   /// <summary>
   /// Formats a std::string value using this format definition.
   /// </summary>
   /// <param name="value">String value to format.</param>
   /// <returns>Formatted display string.</returns>
   std::string toString(const std::string& value) const
   {
      return toString(value.c_str());
   }

   /// <summary>
   /// Formats a C-string value using this format definition.
   /// </summary>
   /// <param name="value">String value to format.</param>
   /// <returns>Formatted display string.</returns>
   std::string toString(const char* value) const
   {
      std::string str = value;

      // determine how to actually display it
      if (str.length() > _length)
      {
         str.clear();
         str.append(_length, _errChar);
      }
      else if (str.length() < _length)
      {
         switch (_alignment)
         {
         case Format::Alignment::LEFT:
            str.append(_length - str.length(), ' ');
            break;

         case Format::Alignment::RIGHT:
         {
            std::string pre(_length - str.length(), ' ');
            str = pre + str;
         }
         break;

         case Format::Alignment::CENTER:
         {
            size_t fill = _length - str.length();
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
