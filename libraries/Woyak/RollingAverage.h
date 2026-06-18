#pragma once

#include <Arduino.h>
#include <stddef.h>

/// <summary>
/// Stores a fixed number of recent values and returns their rolling average.
/// </summary>
class RollingAverage
{
private:
   float* _values;
   size_t _index;
   size_t _size;
   size_t _count;
   float _sum;

public:
   /// <summary>
   /// Initializes a rolling average with the given window size.
   /// </summary>
   /// <param name="size">Number of samples to retain.</param>
   explicit RollingAverage(size_t size)
      : _values(nullptr), _index(0), _size(size), _count(0), _sum(0)
   {
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
   /// RollingAverage is non-copyable.
   /// </summary>
   RollingAverage(const RollingAverage&) = delete;

   /// <summary>
   /// RollingAverage is non-assignable.
   /// </summary>
   RollingAverage& operator=(const RollingAverage&) = delete;

   /// <summary>
   /// Releases allocated sample storage.
   /// </summary>
   ~RollingAverage()
   {
      delete[] _values;
   }

   /// <summary>
   /// Adds a new value to the rolling window and updates the running sum.
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

      return true;
   }

   /// <summary>
   /// Gets the most recently added value.
   /// </summary>
   /// <returns>The last inserted sample, or NaN when size is zero.</returns>
   float getLast() const
   {
      if (_size == 0)
      {
         return NAN;
      }

      size_t index = (_index == 0) ? (_size - 1) : (_index - 1);
      return _values[index];
   }

   /// <summary>
   /// Gets the current rolling average of all valid samples.
   /// </summary>
   /// <returns>The average of non-NaN values, or NaN if no values have been added.</returns>
   float get() const
   {
      return (_count > 0) ? (_sum / _count) : NAN;
   }
};
