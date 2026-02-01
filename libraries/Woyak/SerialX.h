#pragma once

class SerialX
{
public:
   static void begin()
   {
      // start serial port
      Serial.begin(115200);

      // wait a few seconds for the serial monitor to open
      while (millis() < 2000 && !Serial)
      {
      };
      delay(500);
   }

   static void print(const char* str1=nullptr, const char* str2 = nullptr, const char* str3 = nullptr)
   {
      if (str1 != nullptr)
      {
         Serial.print(str1);
      }
      if (str2 != nullptr)
      {
         Serial.print(str2);
      }
      if (str3 != nullptr)
      {
         Serial.print(str3);
      }
   }
   static void println(const char* str1=nullptr, const char* str2 = nullptr, const char* str3 = nullptr)
   {
      print(str1, str2, str3);
      Serial.println();
   };


   static void print(const char* str, int value, int base = 10)
   {
      Serial.print(str);
      Serial.print(value, base);
   }
   static void println(const char* str, int value, int base = 10)
   {
      print(str, value, base);
      Serial.println();
   };
   static void print(const char* str, long value, int base = 10)
   {
      Serial.print(str);
      Serial.print(value, base);
   }
   static void println(const char* str, long value, int base = 10)
   {
      print(str, value, base);
      Serial.println();
   };
   static void print(const char* str, unsigned long value, int base = 10)
   {
      Serial.print(str);
      Serial.print(value, base);
   }
   static void println(const char* str, unsigned long value, int base = 10)
   {
      print(str, value, base);
      Serial.println();
   };
   static void print(const char* str, double value, int precision=2)
   {
      Serial.print(str);
      Serial.print(value, precision);
   }
   static void println(const char* str, double value, int precision=2)
   {
      print(str, value, precision);
      Serial.println();
   };
};