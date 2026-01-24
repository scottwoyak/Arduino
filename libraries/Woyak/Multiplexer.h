#pragma once

#include "Arduino.h"

#define TCAADDR 0x70

//-------------------------------------------------------------------------------------------------
//
// Wrapper for Adafruit TCA9548A multiplexer
// 
//-------------------------------------------------------------------------------------------------
class Multiplexer
{
public:
   static void select(uint8_t i)
   {
      if (i > 7) return;

      Wire.beginTransmission(TCAADDR);
      Wire.write(1 << i);
      Wire.endTransmission();
   }
};
