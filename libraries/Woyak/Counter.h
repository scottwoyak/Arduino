#pragma once

#include "Arduino.h"
#include "Util.h"

//-------------------------------------------------------------------------------------------------
//
// Counts the number of times a pin goes low and the time between
// Set the pin to LOW to trigger.
//
//-------------------------------------------------------------------------------------------------
class Counter
{
   uint8_t _pin;

   static uint8_t _index;
   static const uint8_t MAX_COUNTERS = 10;
   static Counter* _counters[MAX_COUNTERS];

   volatile unsigned long _count = 0;
   volatile unsigned long _micros = 0;
   volatile unsigned long _lastMicros = 0;

   void _onLow()
   {
      _count = _count + 1;
      _lastMicros = _micros;
      _micros = micros();
   }

   static void _onLow0() { _counters[0]->_onLow(); }
   static void _onLow1() { _counters[1]->_onLow(); }
   static void _onLow2() { _counters[2]->_onLow(); }
   static void _onLow3() { _counters[3]->_onLow(); }
   static void _onLow4() { _counters[4]->_onLow(); }
   static void _onLow5() { _counters[5]->_onLow(); }
   static void _onLow6() { _counters[6]->_onLow(); }
   static void _onLow7() { _counters[7]->_onLow(); }
   static void _onLow8() { _counters[8]->_onLow(); }
   static void _onLow9() { _counters[9]->_onLow(); }

public:
   Counter(uint8_t pin)
   {
      _pin = pin;
   }

   bool begin()
   {
      if (_index < MAX_COUNTERS)
      {
         pinMode(_pin, INPUT_PULLUP);

         switch (_index)
         {
         case 0:
            attachInterrupt(digitalPinToInterrupt(_pin), Counter::_onLow0, FALLING);
            break;

         case 1:
            attachInterrupt(digitalPinToInterrupt(_pin), Counter::_onLow1, FALLING);
            break;

         case 2:
            attachInterrupt(digitalPinToInterrupt(_pin), Counter::_onLow2, FALLING);
            break;

         case 3:
            attachInterrupt(digitalPinToInterrupt(_pin), Counter::_onLow3, FALLING);
            break;

         case 4:
            attachInterrupt(digitalPinToInterrupt(_pin), Counter::_onLow4, FALLING);
            break;

         case 5:
            attachInterrupt(digitalPinToInterrupt(_pin), Counter::_onLow5, FALLING);
            break;

         case 6:
            attachInterrupt(digitalPinToInterrupt(_pin), Counter::_onLow6, FALLING);
            break;

         case 7:
            attachInterrupt(digitalPinToInterrupt(_pin), Counter::_onLow7, FALLING);
            break;

         case 8:
            attachInterrupt(digitalPinToInterrupt(_pin), Counter::_onLow8, FALLING);
            break;

         case 9:
            attachInterrupt(digitalPinToInterrupt(_pin), Counter::_onLow9, FALLING);
            break;
         }
         _counters[_index++] = this;
         return true;
      }
      else
      {
         return false;
      }

   }

   uint8_t getPin() const
   {
      return _pin;
   }

   void reset()
   {
      _count = 0;
   }

   unsigned long count() const
   {
      return _count;
   }

   unsigned long span() const
   {
      noInterrupts();
      long span = Util::getSpan(_lastMicros, _micros);
      interrupts();
      return span;
   }
};
