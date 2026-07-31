#pragma once

#include "RollingMicros.h"

class RollingRate
{
private:
   RollingMicros _rMicros;

public:
   explicit RollingRate(uint16_t numSamples = 500)
      : _rMicros(numSamples)
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

   void pause()
   {
      _rMicros.pause();
   }

   void resume()
   {
      _rMicros.resume();
   }

   float get() const
   {
      float secs = _rMicros.getElapsedSeconds();
      if (secs == 0 || _rMicros.getCount() < 2)
      {
         return 0;
      }

      // The elapsed time spans getCount() - 1 intervals between the first and last
      // sample (not getCount() intervals), so the count itself must be reduced by
      // one to get an accurate rate; otherwise the rate is overestimated by a factor
      // of getCount() / (getCount() - 1).
      return (_rMicros.getCount() - 1) / secs;
   }

   uint16_t getCount() const
   {
      return _rMicros.getCount();
   }
};
