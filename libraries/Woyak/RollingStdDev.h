#pragma once

#include <Arduino.h>
#include "RollingValues.h"
#include "StdDev.h"

/// <summary>
/// Computes a rolling population standard deviation over a fixed-size window of values.
/// </summary>
/// <remarks>
/// Non-finite values (NaN and +/-infinity) are ignored for all calculations.
/// </remarks>
class RollingStdDev
{
private:
   RollingValues _values;
   StdDev _stdDev;

public:
   /// <summary>
   /// Initializes a rolling standard deviation tracker with the specified window size.
   /// </summary>
   /// <param name="size">Number of samples retained in the rolling window.</param>
   explicit RollingStdDev(size_t size)
      : _values(size)
   {
   }

   RollingStdDev(const RollingStdDev&) = delete;
   RollingStdDev& operator=(const RollingStdDev&) = delete;

   /// <summary>
   /// Gets the capacity of the rolling window.
   /// </summary>
   /// <returns>The number of values the window can hold.</returns>
   size_t size() const
   {
      return _values.size();
   }

   /// <summary>
   /// Adds a value to the rolling window. Non-finite values are excluded from all calculations.
   /// </summary>
   /// <param name="value">Value to append.</param>
   /// <returns>True when value is added; false when size is zero.</returns>
   boolean set(float value)
   {
      if (_values.size() == 0)
      {
         return false;
      }

      float removed;
      if (_values.set(value, &removed))
      {
         if (isfinite(removed))
         {
            _stdDev.remove(removed);
         }
      }

      if (isfinite(value))
      {
         _stdDev.add(value);
      }

      return true;
   }

   /// <summary>
   /// Clears the rolling window and all statistics.
   /// </summary>
   void reset()
   {
      _values.reset();
      _stdDev.reset();
   }

   /// <summary>
   /// Gets the most recently inserted value.
   /// </summary>
   /// <returns>The last value, or NaN when nothing has been set.</returns>
   float last() const
   {
      if (_values.count() == 0)
      {
         return NAN;
      }

      return _values.get(0);
   }

   /// <summary>
   /// Gets the rolling mean of finite values in the window.
   /// </summary>
   /// <returns>The mean, or NaN if no finite values exist.</returns>
   float mean() const
   {
      return _stdDev.mean();
   }

   /// <summary>
   /// Gets the rolling population standard deviation of finite values in the window.
   /// </summary>
   /// <returns>The population standard deviation, or NaN if no finite values exist.</returns>
   float get() const
   {
      return _stdDev.get();
   }

   /// <summary>
   /// Alias for get().
   /// </summary>
   /// <returns>The population standard deviation, or NaN if no finite values exist.</returns>
   float sigma() const
   {
      return get();
   }

   /// <summary>
   /// Alias for get().
   /// </summary>
   /// <returns>The population standard deviation, or NaN if no finite values exist.</returns>
   float stdDev() const
   {
      return get();
   }

   /// <summary>
   /// Gets the count of finite values currently in the window.
   /// </summary>
   /// <returns>Number of finite values contributing to statistics.</returns>
   size_t count() const
   {
      return _stdDev.count();
   }

   /// <summary>
   /// Calculates the required sample count for the current rolling standard deviation.
   /// </summary>
   /// <param name="tolerance">Allowed error tolerance.</param>
   /// <param name="zScore">Z-score multiplier (defaults to 95% confidence).</param>
   /// <returns>Required sample count, or 0 when inputs are invalid.</returns>
   size_t requiredSamples(float tolerance, float zScore = 1.96f) const
   {
      float sigma = get();
      if (!isfinite(sigma) || tolerance <= 0 || !isfinite(zScore) || zScore <= 0)
      {
         return 0;
      }

      float n = (zScore * sigma) / tolerance;
      n *= n;
      return (size_t)ceilf(n);
   }
};
