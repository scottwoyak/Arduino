#pragma once

#include <Arduino.h>
#include "RollingValues.h"
#include "Stats.h"

/// <summary>
/// Stores a fixed window of recent values and exposes rolling statistics.
/// </summary>
/// <remarks>
/// Non-finite values (NaN and +/-infinity) are ignored for statistic
/// calculations.
/// </remarks>
class RollingStats
{
private:
   RollingValues _values;
   Stats _stats;
   mutable float _min = NAN;
   mutable float _max = NAN;

   void _recomputeMinMax() const
   {
      _min = NAN;
      _max = NAN;

      if (_values.count() == 0)
      {
         return;
      }

      for (size_t i = 0; i < _values.count(); i++)
      {
         float value = _values.get(i);
         if (!isfinite(value))
         {
            continue;
         }

         if (isnan(_min) || value < _min)
         {
            _min = value;
         }

         if (isnan(_max) || value > _max)
         {
            _max = value;
         }
      }
   }

public:
   /// <summary>
   /// Initializes a rolling stats tracker with the specified window size.
   /// </summary>
   /// <param name="size">Number of samples retained in the rolling window.</param>
   explicit RollingStats(size_t size)
      : _values(size)
   {
   }

   /// <summary>
   /// RollingStats is non-copyable.
   /// </summary>
   RollingStats(const RollingStats&) = delete;

   /// <summary>
   /// RollingStats is non-assignable.
   /// </summary>
   RollingStats& operator=(const RollingStats&) = delete;

   /// <summary>
   /// Gets the capacity of the rolling window.
   /// </summary>
   /// <returns>The number of samples the window can hold.</returns>
   size_t size() const
   {
      return _values.size();
   }

   /// <summary>
   /// Adds a value to the rolling window.
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
         _stats.remove(removed);
      }

      _stats.add(value);
      _min = NAN;
      return true;
   }

   /// <summary>
   /// Clears the rolling window and statistics.
   /// </summary>
   void reset()
   {
      _values.reset();
      _stats.reset();
      _min = NAN;
   }

   /// <summary>
   /// Gets the most recently inserted value.
   /// </summary>
   /// <returns>The last value, or NaN when the buffer is empty or nothing has been set.</returns>
   float last() const
   {
      if (_values.count() == 0)
      {
         return NAN;
      }

      return _values.get(0);
   }

   /// <summary>
   /// Gets the rolling minimum value.
   /// </summary>
   /// <returns>Minimum finite value, or NaN if no finite values exist.</returns>
   float min() const
   {
      if (isnan(_min))
      {
         _recomputeMinMax();
      }

      return _min;
   }

   /// <summary>
   /// Gets the rolling maximum value.
   /// </summary>
   /// <returns>Maximum finite value, or NaN if no finite values exist.</returns>
   float max() const
   {
      if (isnan(_min))
      {
         _recomputeMinMax();
      }

      return _max;
   }

   /// <summary>
   /// Gets the range between rolling maximum and rolling minimum values.
   /// </summary>
   /// <returns>Max-minus-min range, or NaN if no finite values exist.</returns>
   float range() const
   {
      if (isnan(_min))
      {
         _recomputeMinMax();
      }

      if (!isfinite(_min) || !isfinite(_max))
      {
         return NAN;
      }

      return _max - _min;
   }

   /// <summary>
   /// Gets the rolling average value.
   /// </summary>
   /// <returns>Average finite value, or NaN if no finite values exist.</returns>
   float average() const
   {
      return _stats.get();
   }

   /// <summary>
   /// Gets the rolling average value.
   /// </summary>
   /// <returns>Average of finite values, or NaN if no finite values exist.</returns>
   float get() const
   {
      return average();
   }

   /// <summary>
   /// Gets the count of finite values in the current window.
   /// </summary>
   /// <returns>Number of finite values contributing to statistics.</returns>
   size_t count() const
   {
      return _stats.count();
   }
};
