#pragma once

#include <RollingMicros.h>

class RollingRate
{
private:
   RollingMicros _rMicros;

public:
   RollingRate(uint16_t numSamples=500) : _rMicros(numSamples)
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

   float get()
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
