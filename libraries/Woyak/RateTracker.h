#pragma once

#include <RollingMicros.h>

//
// Class for computing ticks per second
//
class RateTracker 
{
private:
   unsigned long _startMicros = 0;
   double _elapsedSecs = 0;
   unsigned long _count;

public:
   RateTracker()
   {
   }

   void tick() 
   {
      if (_count == 0)
      {
         _startMicros = micros();
      }

      _count++;
      _elapsedSecs = (micros() - _startMicros) / (1000 * 1000.0);
   }

   void reset()
   {
      _count = 0;
   }

   float getRate() 
   {
      return (_count-1) / _elapsedSecs;
   }

   unsigned long getCount()
   {
      return _count;
   }
};

class RollingRateTracker
{
private:
   RollingMicros _rMicros;

public:
   RollingRateTracker(uint16_t numSamples=500) : _rMicros(numSamples)
   {
   }

   void tick()
   {
      _rMicros.tick();
   }

   void reset()
   {
      _rMicros.reset();
   }

   float getRate()
   {
      float secs = _rMicros.getElapsedSeconds();
      if (secs == 0)
      {
         return 0;
      }
      else
      {
         return _rMicros.getCount() / _rMicros.getElapsedSeconds();
      }
   }

   uint16_t getCount()
   {
      return _rMicros.getCount();
   }
};
