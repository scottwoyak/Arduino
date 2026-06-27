#pragma once

#include <Arduino.h>
#include "RollingValues.h"

/// <summary>
/// Stores a fixed window of recent values and exposes rolling averages.
/// </summary>
/// <remarks>
/// Non-finite values (NaN and +/-infinity) are ignored for average calculations.
/// </remarks>
class RollingAverage
{
private:
   RollingValues _values;
   float _sum = 0.0f;
   size_t _count = 0;

public:
   /// <summary>
   /// Initializes a rolling average tracker with the specified window size.
   /// </summary>
   /// <param name="size">Number of samples retained in the rolling window.</param>
   explicit RollingAverage(size_t size)
      : _values(size)
   {
   }

   RollingAverage(const RollingAverage&) = delete;
   RollingAverage& operator=(const RollingAverage&) = delete;

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
   bool set(float value)
   {
      if (_values.size() == 0)
      {
         return false;
      }

      float removed = NAN;
      if (_values.set(value, &removed))
      {
         if (isfinite(removed))
         {
            _sum -= removed;
            if (_count > 0)
            {
               _count--;
            }
         }
      }

      if (isfinite(value))
      {
         _sum += value;
         _count++;
      }

      return true;
   }

   /// <summary>
   /// Clears the rolling window and statistics.
   /// </summary>
   /// <param name="size">New window size, or 0 to keep the current size.</param>
   void reset(size_t size = 0)
   {
      if (size != 0)
      {
         _values.resize(size);
      }

      _values.reset();
      _sum = 0.0f;
      _count = 0;
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
   /// Gets the rolling average value.
   /// </summary>
   /// <returns>Average finite value, or NaN if no finite values exist.</returns>
   float average() const
   {
      if (_count == 0)
      {
         return NAN;
      }

      return _sum / _count;
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
   /// <returns>Number of finite values contributing to the average.</returns>
   size_t count() const
   {
      return _count;
   }
};
