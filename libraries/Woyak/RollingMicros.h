#pragma once

#include "Util.h"

/// <summary>
/// Tracks a continuous stream of microsecond timing values in a circular buffer.
/// Supports pausing/resuming to exclude time spans from measurements.
/// </summary>
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

   unsigned long (*_ticks)();

   /// <summary>
   /// Calculates the current effective time, accounting for any paused intervals.
   /// </summary>
   /// <returns>Effective microseconds elapsed since tracking began.</returns>
   unsigned long _effectiveMicros() const
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
   /// <summary>
   /// Function pointer for injecting a custom time source (used in testing).
   /// If set, this function will be used instead of the default micros() clock.
   /// </summary>
   static unsigned long (*tickFunc)();

   /// <summary>
   /// Initializes a new RollingMicros tracker.
   /// </summary>
   /// <param name="numSamples">Maximum number of time samples to store. Defaults to 500.</param>
   explicit RollingMicros(uint16_t numSamples = 500)
   {
      _numSamples = (numSamples == 0) ? 1 : numSamples;
      _micros = new unsigned long[_numSamples];

      if (RollingMicros::tickFunc != nullptr)
      {
         _ticks = RollingMicros::tickFunc;
      }
      else
      {
         _ticks = micros;
      }
   }

   /// <summary>
   /// Destructor that frees the internal sample buffer.
   /// </summary>
   ~RollingMicros()
   {
      delete[] _micros;
   }

   /// <summary>
   /// Records a new sample at the current time (accounting for paused intervals).
   /// </summary>
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

   /// <summary>
   /// Clears all state, preparing the tracker for a new measurement cycle.
   /// </summary>
   void reset()
   {
      _wrap = false;
      _index = 0;
      _excludedMicros = 0;
      _excludeStartMicros = 0;
      _excluding = false;
   }

   /// <summary>
   /// Begins excluding time from effective measurements (e.g., during inactive periods).
   /// </summary>
   void pause()
   {
      if (!_excluding)
      {
         _excluding = true;
         _excludeStartMicros = _ticks();
      }
   }

   /// <summary>
   /// Stops excluding time and accounts for the paused interval.
   /// </summary>
   void resume()
   {
      if (_excluding)
      {
         _excludedMicros += Util::getSpan(_excludeStartMicros, _ticks());
         _excluding = false;
      }
   }

   /// <summary>
   /// Gets the timestamp of the oldest sample currently in the buffer.
   /// </summary>
   /// <returns>Microseconds of the first sample, or 0 if no samples have been recorded.</returns>
   unsigned long getFirstMicros() const
   {
      if (getCount() == 0)
      {
         return 0;
      }

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

   /// <summary>
   /// Gets the timestamp of the most recent sample in the buffer.
   /// </summary>
   /// <returns>Microseconds of the last sample, or 0 if no samples have been recorded.</returns>
   unsigned long getLastMicros() const
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
            return 0;
         }
         else
         {
            lastIndex = _index - 1;
         }
      }

      return _micros[lastIndex];
   }

   /// <summary>
   /// Gets the elapsed time between the oldest and newest samples.
   /// </summary>
   /// <returns>Effective microseconds elapsed, accounting for any paused intervals.</returns>
   unsigned long getElapsedMicros() const
   {
      return Util::getSpan(getFirstMicros(), getLastMicros());
   }

   /// <summary>
   /// Gets the elapsed time between the oldest and newest samples in seconds.
   /// </summary>
   /// <returns>Effective seconds elapsed, accounting for any paused intervals.</returns>
   float getElapsedSeconds() const
   {
      return getElapsedMicros() / 1000000.0f;
   }

   /// <summary>
   /// Gets the number of samples currently stored in the buffer.
   /// </summary>
   /// <returns>Count of recorded samples (0 to numSamples).</returns>
   uint16_t getCount() const
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

unsigned long (*RollingMicros::tickFunc)() = nullptr;
