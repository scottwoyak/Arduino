#pragma once

#include "Arduino.h"
#include "RotaryEncoder.h"

//-------------------------------------------------------------------------------------------------
class IndexedEncoder : public RotaryEncoder
{
private:
   int _numItems;

public:
   IndexedEncoder(uint8_t pinA, uint8_t pinB, uint8_t buttonPin, uint16_t numItems) : RotaryEncoder(pinA, pinB, buttonPin)
   {
      _numItems = numItems;
   }

   void setIndex(uint16_t index, uint16_t numItems)
   {
      setPosition(index);
      _numItems = numItems;
   }

   uint16_t getIndex() const
   {
      long value = getPosition();

      while (value < 0)
      {
         value += _numItems;
      }

      return value % _numItems;
   }
};
