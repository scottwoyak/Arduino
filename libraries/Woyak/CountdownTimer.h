#pragma once

#include "Stopwatch.h"

class CountdownTimer
{
   Stopwatch _sw;
   unsigned long _timerMs;

public:
   explicit CountdownTimer(unsigned long durationMs)
      : _sw(StopwatchPrecision::Millis)
   {
      _timerMs = durationMs;
   }

   explicit CountdownTimer(float durationS)
      : CountdownTimer(static_cast<unsigned long>(1000.0f * durationS))
   {
   }

   void reset()
   {
      _sw.reset();
   }

   unsigned long remainingMs() const
   {
      if (isExpired())
      {
         return 0;
      }

      return static_cast<unsigned long>(ceil(_timerMs - _sw.elapsedMillis()));
   }

   double remainingS() const
   {
      return remainingMs() / 1000.0;
   }

   bool isExpired() const
   {
      return _sw.elapsedMillis() > _timerMs;
   }
};
