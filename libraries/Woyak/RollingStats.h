#pragma once

#include <Arduino.h>
#include <stddef.h>

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
   float* _values;
   size_t _index;
   size_t _size;
   size_t _count;
   float _sum;
   float _low;
   float _high;

   void _recomputeLowHigh()
   {
      _low = NAN;
      _high = NAN;

      if (_count == 0)
      {
         return;
      }

      for (size_t i = 0; i < _size; i++)
      {
         float value = _values[i];
         if (!isfinite(value))
         {
            continue;
         }

         if (isnan(_low) || value < _low)
         {
            _low = value;
         }

         if (isnan(_high) || value > _high)
         {
            _high = value;
         }
      }
   }

public:
   /// <summary>
   /// Initializes a rolling stats tracker with the specified window size.
   /// </summary>
   /// <param name="size">Number of samples retained in the rolling window.</param>
   explicit RollingStats(size_t size)
   {
      _values = nullptr;
      _index = 0;
      _size = size;
      _count = 0;
      _sum = 0;
      _low = NAN;
      _high = NAN;

      if (_size > 0)
      {
         _values = new float[_size];
         for (size_t i = 0; i < _size; i++)
         {
            _values[i] = NAN;
         }
      }
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
   /// Releases allocated sample storage.
   /// </summary>
   ~RollingStats()
   {
      delete[] _values;
   }

   /// <summary>
   /// Gets the number of samples being used in the rolling window.
   /// </summary>
   /// <returns>The number of samples in the rolling window.</returns>
   size_t size() const
   {
      return _size;
   }

   /// <summary>
   /// Adds a value to the rolling window.
   /// </summary>
   /// <param name="value">Value to append.</param>
   /// <returns>True when value is added; false when size is zero.</returns>
   boolean set(float value)
   {
      if (_size == 0)
      {
         return false;
      }

      float oldValue = _values[_index];
      if (isfinite(oldValue))
      {
         _sum -= oldValue;
         _count--;
      }

      _values[_index] = value;
      if (isfinite(value))
      {
         _sum += value;
         _count++;
      }

      _index++;
      if (_index >= _size)
      {
         _index = 0;
      }

      _recomputeLowHigh();
      return true;
   }

   /// <summary>
   /// Clears the rolling window and statistics.
   /// </summary>
   void reset()
   {
      _index = 0;
      _count = 0;
      _sum = 0;
      _low = NAN;
      _high = NAN;

      for (size_t i = 0; i < _size; i++)
      {
         _values[i] = NAN;
      }
   }

   /// <summary>
   /// Resizes the rolling window and clears existing samples.
   /// </summary>
   /// <param name="size">New number of samples retained in the rolling window.</param>
   void resize(size_t size)
   {
      if (size == _size)
      {
         reset();
         return;
      }

      delete[] _values;
      _values = nullptr;
      _size = size;
      _index = 0;
      _count = 0;
      _sum = 0;
      _low = NAN;
      _high = NAN;

      if (_size > 0)
      {
         _values = new float[_size];
         for (size_t i = 0; i < _size; i++)
         {
            _values[i] = NAN;
         }
      }
   }

   /// <summary>
   /// Gets the most recently inserted value.
   /// </summary>
   /// <returns>The last value, or NaN when size is zero.</returns>
   float last() const
   {
      if (_size == 0)
      {
         return NAN;
      }

      size_t lastIndex = (_index == 0) ? (_size - 1) : (_index - 1);
      return _values[lastIndex];
   }

   /// <summary>
   /// Gets the rolling minimum value.
   /// </summary>
   /// <returns>Minimum finite value, or NaN if no finite values exist.</returns>
   float min() const
   {
      return _low;
   }

   /// <summary>
   /// Gets the rolling maximum value.
   /// </summary>
   /// <returns>Maximum finite value, or NaN if no finite values exist.</returns>
   float max() const
   {
      return _high;
   }

   /// <summary>
   /// Gets the range between rolling maximum and rolling minimum values.
   /// </summary>
   /// <returns>Max-minus-min range, or NaN if no finite values exist.</returns>
   float range() const
   {
      if (!isfinite(_low) || !isfinite(_high))
      {
         return NAN;
      }

      return _high - _low;
   }

   /// <summary>
   /// Gets the rolling average value.
   /// </summary>
   /// <returns>Average finite value, or NaN if no finite values exist.</returns>
   float average() const
   {
      return (_count > 0) ? (_sum / _count) : NAN;
   }

   /// <summary>
   /// Gets the rolling median value.
   /// </summary>
   /// <returns>Median finite value, or NaN if no finite values exist.</returns>
   float median() const
   {
      if (_count == 0)
      {
         return NAN;
      }

      float* finiteValues = new float[_count];
      size_t n = 0;
      for (size_t i = 0; i < _size; i++)
      {
         float value = _values[i];
         if (isfinite(value))
         {
            finiteValues[n++] = value;
         }
      }

      for (size_t i = 1; i < n; i++)
      {
         float key = finiteValues[i];
         size_t j = i;
         while (j > 0 && finiteValues[j - 1] > key)
         {
            finiteValues[j] = finiteValues[j - 1];
            j--;
         }
         finiteValues[j] = key;
      }

      float median;
      if ((n % 2) == 1)
      {
         median = finiteValues[n / 2];
      }
      else
      {
         median = (finiteValues[(n / 2) - 1] + finiteValues[n / 2]) / 2.0f;
      }

      delete[] finiteValues;
      return median;
   }

   /// <summary>
   /// Gets the count of finite values in the current window.
   /// </summary>
   /// <returns>Number of finite values contributing to statistics.</returns>
   size_t count() const
   {
      return _count;
   }
};
