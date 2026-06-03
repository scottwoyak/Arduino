#pragma once

#include <Arduino.h>
#include <cmath>

/// <summary>
/// A lightweight, memory-efficient class that incrementally calculates the population 
/// standard deviation and variance using Welford's algorithm.
/// </summary>
class StdDev
{
private:
   size_t _count = 0;
   float _mean = NAN;
   float _s = 0.0f; // Running sum of squares of differences from the current mean

   /// <summary>
   /// Calculates the current population variance internally.
   /// </summary>
   /// <returns>The population variance, or NAN if no data points have been added.</returns>
   float _getPopulationVariance() const
   {
      return (_count > 0) ? (_s / _count) : NAN;
   }

public:
   /// <summary>
   /// Adds a new data point to the streaming dataset and updates the statistics.
   /// </summary>
   /// <param name="x">The new floating-point value to stream in.</param>
   void add(float x)
   {
      _count++;
      if (_count == 1)
      {
         _mean = x;
         _s = 0.0f;
         return;
      }
      float prevMean = _mean;
      _mean += (x - prevMean) / _count;
      _s += (x - prevMean) * (x - _mean);
   }

   /// <summary>
   /// Removes a previously added data point from the calculation (Welford's Downdate).
   /// </summary>
   /// <param name="x">The exact floating-point value to remove.</param>
   /// <returns>True if the value was removed successfully; false if the tracker was already empty.</returns>
   bool remove(float x)
   {
      if (_count == 0)
      {
         return false;
      }

      if (_count == 1)
      {
         reset();
         return true;
      }

      _count--;
      float prevMean = _mean;
      _mean = (prevMean * (_count + 1) - x) / _count;
      _s -= (x - prevMean) * (x - _mean);

      if (_s < 0.0f)
      {
         _s = 0.0f;
      }
      return true;
   }

   /// <summary>
   /// Gets the current number of elements being tracked.
   /// </summary>
   /// <returns>The running count as a size_t.</returns>
   size_t count() const
   {
      return _count;
   }

   /// <summary>
   /// Gets the current running mean of the tracked population.
   /// </summary>
   /// <returns>The calculated mean, or NAN if no data points have been added.</returns>
   float mean() const
   {
      return (_count > 0) ? _mean : NAN;
   }

   /// <summary>
   /// Calculates the current population standard deviation.
   /// </summary>
   /// <returns>The population standard deviation, or NAN if no data points have been added.</returns>
   float get() const
   {
      return (_count > 0) ? sqrtf(_getPopulationVariance()) : NAN;
   }

   /// <summary>
   /// Resets the internal state, clearing out all tracked history and setting metrics back to NAN.
   /// </summary>
   void reset()
   {
      _count = 0;
      _mean = NAN;
      _s = 0.0f;
   }
};
