#pragma once

#include <Util.h>

//
// Class for tracking a continuous stream of values
//
class RollingMicros 
{
private:
   unsigned long* _micros;
   uint16_t _numSamples;
   uint16_t _index = 0;
   bool _wrap = false;

   unsigned long _excludedMicros = 0;
   unsigned long _excludeStartMicros = 0;
   bool _excluding = false;

   unsigned long (*_ticks) ();

   unsigned long _effectiveMicros()
   {
      unsigned long now = _ticks();
      unsigned long excluded = _excludedMicros;

      if (_excluding)
      {
         excluded += Util::getSpan(_excludeStartMicros, now);
      }

      return now - excluded;
   }

public:
   // public function for testing only. Allows us to substitute our own clock
   // within test cases of timing code.
   static unsigned long (*tickFunc) ();

   RollingMicros(uint16_t numSamples=500)
   {
      _numSamples = numSamples;
      _micros = new unsigned long[numSamples];

      if (RollingMicros::tickFunc != nullptr)
      {
         _ticks = RollingMicros::tickFunc;
      }
      else
      {
         _ticks = micros;
      }
   }

   ~RollingMicros()
   {
      delete[] _micros;
   }

   void tick()
   {
      _micros[_index] = _effectiveMicros();

      _index++;
      if (_index == _numSamples)
      {
         _wrap = true;
         _index = 0;
      }
   }

   void reset()
   {
      _wrap = false;
      _index = 0;
      _excludedMicros = 0;
      _excludeStartMicros = 0;
      _excluding = false;
   }

   void pause()
   {
      if (!_excluding)
      {
         _excluding = true;
         _excludeStartMicros = _ticks();
      }
   }

   void resume()
   {
      if (_excluding)
      {
         _excludedMicros += Util::getSpan(_excludeStartMicros, _ticks());
         _excluding = false;
      }
   }

   unsigned long getFirstMicros()
   {
      uint16_t firstIndex;
      
      if (_wrap)
      {
         firstIndex = _index;
      }
      else
      {
         firstIndex = 0;
      }

      return _micros[firstIndex];
   }

   unsigned long getLastMicros()
   {
      uint16_t lastIndex;

      if (_wrap)
      {
         lastIndex = _index == 0 ? (_numSamples - 1) : _index - 1;
      }
      else
      {
         if (_index == 0)
         {
            return 0; // tick has not yet been called
         }
         else
         {
            lastIndex = _index - 1;
         }
      }

      return _micros[lastIndex];
   }

   unsigned long getElapsedMicros()
   {
      return Util::getSpan(getFirstMicros(), getLastMicros());
   }

   float getElapsedSeconds()
   {
      return getElapsedMicros() / (1000 * 1000.0);
   }

   uint16_t getCount()
   {
      if (_wrap)
      {
         return _numSamples;
      }
      else
      {
         return _index;
      }
   }
};

unsigned long (*RollingMicros::tickFunc) () = nullptr;
