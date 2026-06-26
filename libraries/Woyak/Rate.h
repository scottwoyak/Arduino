#pragma once

#include <Arduino.h>
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
   void start()
   {
      _stopwatch.reset();
      _stopwatch.start();
   }

   void stop()
   {
      _stopwatch.stop();
      _elapsedMicros = static_cast<float>(_stopwatch.elapsedMicros());
   }

   float elapsedMicros() const
   {
      return _elapsedMicros;
   }

   float get() const
   {
      if (!isfinite(_elapsedMicros) || _elapsedMicros <= 0)
      {
         return NAN;
      }

      return MICROS_PER_SECOND / _elapsedMicros;
   }
};
