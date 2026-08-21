#pragma once

#include <Arduino.h>
#include "Util.h"

/// <summary>
/// Interrupt-driven pulse counter with period measurement.
/// </summary>
/// <remarks>
/// Counts falling-edge transitions on an input pin and tracks the time between pulses
/// for frequency/period determination using hardware interrupts. Uses attachInterruptArg()
/// so any number of Counter instances can be created, each routed through a single shared
/// ISR. Pins should be pulled HIGH (active-low pulses).
/// </remarks>
class Counter
{
   uint8_t _pin;

   volatile unsigned long _count = 0;
   volatile unsigned long _micros = 0;
   volatile unsigned long _lastMicros = 0;

   void ARDUINO_ISR_ATTR _onLow()
   {
      _count = _count + 1;
      _lastMicros = _micros;
      _micros = micros();
   }

   static void ARDUINO_ISR_ATTR _onLowHandler(void* arg) { static_cast<Counter*>(arg)->_onLow(); }

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
   /// <returns>true, always; retained for backward compatibility with callers that check the result</returns>
   /// <remarks>
   /// Configures the pin as INPUT_PULLUP and attaches a FALLING edge interrupt.
   /// Must be called once during setup().
   /// </remarks>
   bool begin()
   {
      pinMode(_pin, INPUT_PULLUP);

      attachInterruptArg(digitalPinToInterrupt(_pin), Counter::_onLowHandler, this, FALLING);

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
