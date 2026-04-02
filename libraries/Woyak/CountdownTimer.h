#include <Stopwatch.h>

class CountdownTimer {
   Stopwatch _sw;
   unsigned long _timerMs;

public:
   CountdownTimer(unsigned long durationMs) : _sw(StopwatchPrecision::Millis)
   {
      _timerMs = durationMs;
   }

   CountdownTimer(float durationS) : CountdownTimer((unsigned long)(1000 * durationS))
   {
   }

   void reset()
   {
      _sw.reset();
   }

   unsigned long remainingMs()
   {
      if (isExpired())
      {
         return 0;
      }
      else
      {
         return ceil(_timerMs - _sw.elapsedMillis());
      }
   }

   double remainingS()
   {
      return remainingMs() / 1000.0;
   }

   bool isExpired()
   {
      return _sw.elapsedMillis() > _timerMs;
   }
};
