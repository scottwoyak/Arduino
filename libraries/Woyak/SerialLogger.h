#pragma once

#include <Arduino.h>

#include "ILogger.h"

///
/// <summary>
/// ILogger implementation that writes to Serial. Add via Logger::addLogger(new SerialLogger()).
/// </summary>
///
class SerialLogger : public ILogger
{
public:
   void write(const char* str) override
   {
      Serial.print(str);
   }

   void writeln(const char* str) override
   {
      Serial.println(str);
   }
};
