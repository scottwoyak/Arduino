#pragma once

#include <Arduino.h>
#include "Util.h"

/// <summary>
/// Interrupt-driven pulse counter with period measurement.
/// </summary>
/// <remarks>
/// Counts falling-edge transitions on an input pin and tracks the time between pulses
/// for frequency/period determination. Supports up to 10 simultaneous counters using
/// hardware interrupts. Pins should be pulled HIGH (active-low pulses).
/// </remarks>
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
   /// <summary>
   /// Constructs a Counter for the specified input pin.
   /// </summary>
   /// <param name="pin">GPIO pin that will receive pulse signals</param>
   Counter(uint8_t pin)
   {
      _pin = pin;
   }

   /// <summary>
   /// Initializes the counter with interrupt handler and begins counting pulses.
   /// </summary>
   /// <returns>True if initialization succeeded, false if max counter limit reached</returns>
   /// <remarks>
   /// Configures the pin as INPUT_PULLUP and attaches a FALLING edge interrupt.
   /// Must be called once during setup(). Up to 10 counters can be initialized per device.
   /// </remarks>
   bool begin()
   {
      if (_index >= MAX_COUNTERS)
      {
         return false;
      }

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

   /// <summary>
   /// Gets the GPIO pin number for this counter.
   /// </summary>
   /// <returns>Configured input pin</returns>
   uint8_t getPin() const
   {
      return _pin;
   }

   /// <summary>
   /// Resets the pulse counter to zero.
   /// </summary>
   void reset()
   {
      _count = 0;
   }

   /// <summary>
   /// Gets the total number of pulses counted.
   /// </summary>
   /// <returns>Accumulated pulse count</returns>
   unsigned long count() const
   {
      return _count;
   }

   /// <summary>
   /// Gets the time span (in microseconds) between the last two pulses.
   /// </summary>
   /// <returns>Duration in microseconds between latest and previous pulse edge</returns>
   /// <remarks>
   /// Can be used to calculate frequency or detect missing pulses. Temporarily disables
   /// interrupts for a safe read of volatile timing variables.
   /// </remarks>
   unsigned long span() const
   {
      noInterrupts();
      unsigned long span = Util::getSpan(_lastMicros, _micros);
      interrupts();
      return span;
   }
};

// Static member initialization
Counter* Counter::_counters[Counter::MAX_COUNTERS] = {};
uint8_t Counter::_index = 0;
