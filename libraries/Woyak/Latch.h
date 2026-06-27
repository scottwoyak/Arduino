#pragma once

#include <Arduino.h>
#include "Util.h"

/// <summary>
/// Debounce time constant for latched state changes.
/// </summary>
constexpr unsigned long DEBOUNCE_TIME_MICROS = 100;

/// <summary>
/// Debounced digital input latch with period tracking.
/// </summary>
/// <remarks>
/// Tracks state changes of a digital input pin with debouncing to filter noise.
/// Also measures the period between state transitions, useful for frequency measurement
/// and pulse detection. Thread-safe for use in ISR contexts via volatile qualifiers.
/// </remarks>
class Latch
{
private:
   unsigned long _lastHighMicros = 0;
   unsigned long _period = 0;
   unsigned long _microsAtStateChange = 0;
   int _state = LOW;

public:
   /// <summary>
   /// Determines whether the current state has settled (debounce time elapsed).
   /// </summary>
   /// <returns>true if at least DEBOUNCE_TIME_MICROS has passed since last state change</returns>
   bool settled() volatile
   {
      return Util::getSpan(_microsAtStateChange, micros()) > DEBOUNCE_TIME_MICROS;
   }

   /// <summary>
   /// Attempts to change the latched state with debouncing.
   /// </summary>
   /// <param name="state">The new state value (HIGH or LOW)</param>
   /// <returns>true if state changed (after debounce); false if ignored due to debounce or no change</returns>
   /// <remarks>
   /// If transitioning to HIGH, automatically records the period since the previous HIGH state.
   /// </remarks>
   bool setState(int state) volatile
   {
      unsigned long newMicros = micros();

      // debounce
      if (Util::getSpan(_microsAtStateChange, newMicros) < DEBOUNCE_TIME_MICROS)
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

   /// <summary>
   /// Gets the current latched state.
   /// </summary>
   /// <returns>The debounced state value (HIGH or LOW)</returns>
   int getState() volatile
   {
      return _state;
   }

   /// <summary>
   /// Gets the time between the last two HIGH state transitions.
   /// </summary>
   /// <returns>Period in microseconds</returns>
   unsigned long getPeriod() volatile
   {
      return _period;
   }

   /// <summary>
   /// Gets the elapsed time since the last HIGH state transition.
   /// </summary>
   /// <returns>Time in microseconds since last HIGH transition</returns>
   unsigned long getMicrosSinceLastTick() volatile
   {
      return Util::getSpan(_lastHighMicros, micros());
   }
};
