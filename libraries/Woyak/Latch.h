#pragma once

#include "Arduino.h"
#include "Util.h"

constexpr uint DEBOUNCE_TIME = 100;

class Latch
{
private:
   unsigned long _lastHighMicros = 0;
   unsigned long _period = 0;
   unsigned long _microsAtStateChange = 0;
   int _state = LOW;

public:
   bool settled() volatile
   {
      return Util::getSpan(_microsAtStateChange, micros()) > DEBOUNCE_TIME;
   }

   bool setState(int state) volatile
   {
      unsigned long newMicros = micros();

      // debounce
      if (micros() - _microsAtStateChange < DEBOUNCE_TIME)
      {
         return false;
      }

      if (state == _state)
      {
         return false;
      }

      _state = state;
      _microsAtStateChange = newMicros;

      // keep track of the period
      if (state == HIGH)
      {
         _period = Util::getSpan(_lastHighMicros, newMicros);
         _lastHighMicros = newMicros;
      }

      return true;
   }

   int getState() volatile
   {
      return _state;
   }

   unsigned long getPeriod() volatile
   {
      return _period;
   }

   unsigned long getMicrosSinceLastTick() volatile
   {
      return Util::getSpan(this->_lastHighMicros, micros());
   }
};
