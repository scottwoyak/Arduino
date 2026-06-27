#pragma once

#include <Arduino.h>
#include <cmath>

#include "Stopwatch.h"

/// <summary>
/// Measures one operation cycle duration with Stopwatch and exposes rate in events/second.
/// </summary>
class Rate
{
private:
   static constexpr float MICROS_PER_SECOND = 1000000.0f;

   Stopwatch _stopwatch = Stopwatch(false, StopwatchPrecision::Micros);
   float _elapsedMicros = NAN;

public:
   /// <summary>
   /// Starts the stopwatch to measure cycle duration.
   /// </summary>
   void start()
   {
      _stopwatch.reset();
      _stopwatch.start();
   }

   /// <summary>
   /// Stops the stopwatch and records elapsed time.
   /// </summary>
   void stop()
   {
      _stopwatch.stop();
      _elapsedMicros = static_cast<float>(_stopwatch.elapsedMicros());
   }

   /// <summary>
   /// Gets the elapsed time in microseconds for the last cycle.
   /// </summary>
   /// <returns>Elapsed microseconds, or NaN if not measured</returns>
   float elapsedMicros() const
   {
      return _elapsedMicros;
   }

   /// <summary>
   /// Gets the rate in cycles per second.
   /// </summary>
   /// <returns>Cycles per second, or NaN if elapsed time is invalid</returns>
   float get() const
   {
      if (!std::isfinite(_elapsedMicros) || _elapsedMicros <= 0)
      {
         return NAN;
      }

      return MICROS_PER_SECOND / _elapsedMicros;
   }
};
