#pragma once

#include <cmath>
#include <cstddef>
#include <stdint.h>

/// <summary>
/// Represents a fixed-bin histogram for floating-point values.
/// </summary>
/// <remarks>
/// Values are assigned to bins spanning [min, max].
/// NaN and infinite values are ignored.
/// </remarks>
template<size_t BinCount>
class Histogram
{
private:
   static_assert(BinCount > 0, "Histogram requires at least one bin.");

   uint32_t _bins[BinCount] = { 0 };
   size_t _count = 0;
   float _min = NAN;
   float _max = NAN;

public:
   Histogram()
   {
   }

   /// <summary>
   /// Adds a value to the histogram.
   /// </summary>
   /// <param name="value">Value to track. NaN and infinite values are ignored.</param>
   void add(float value)
   {
      if (!std::isfinite(value))
      {
         return;
      }

      if (_count == 0)
      {
         _min = value;
         _max = value;
         _bins[0] = 1;
         _count = 1;
         return;
      }

      if (value < _min || value > _max)
      {
         _expandAndRebin(value);
      }

      const size_t index = _getBinIndex(value, _min, _max);
      _bins[index]++;
      _count++;
   }

   /// <summary>
   /// Gets the number of tracked values.
   /// </summary>
   size_t count() const
   {
      return _count;
   }

   /// <summary>
   /// Gets the minimum tracked value.
   /// </summary>
   float min() const
   {
      return (_count == 0) ? NAN : _min;
   }

   /// <summary>
   /// Gets the maximum tracked value.
   /// </summary>
   float max() const
   {
      return (_count == 0) ? NAN : _max;
   }

   /// <summary>
   /// Gets the tracked range (max-min).
   /// </summary>
   float range() const
   {
      return (_count == 0) ? NAN : (_max - _min);
   }

   /// <summary>
   /// Gets a bin count by index.
   /// </summary>
   uint32_t bin(size_t index) const
   {
      if (index >= BinCount)
      {
         return 0;
      }

      return _bins[index];
   }

   /// <summary>
   /// Gets the configured number of bins.
   /// </summary>
   constexpr size_t bins() const
   {
      return BinCount;
   }

   /// <summary>
   /// Resets all tracked values and bins.
   /// </summary>
   void reset()
   {
      for (size_t i = 0; i < BinCount; i++)
      {
         _bins[i] = 0;
      }

      _count = 0;
      _min = NAN;
      _max = NAN;
   }

private:
   size_t _getBinIndex(float value, float minValue, float maxValue) const
   {
      const float span = maxValue - minValue;
      if (span <= 0.0f)
      {
         return 0;
      }

      float normalized = (value - minValue) / span;
      if (normalized >= 1.0f)
      {
         return BinCount - 1;
      }
      if (normalized <= 0.0f)
      {
         return 0;
      }

      size_t index = (size_t)(normalized * BinCount);
      if (index >= BinCount)
      {
         index = BinCount - 1;
      }
      return index;
   }

   void _expandAndRebin(float newValue)
   {
      const float oldMin = _min;
      const float oldMax = _max;

      _min = std::min(_min, newValue);
      _max = std::max(_max, newValue);

      uint32_t rebinned[BinCount] = { 0 };

      if (_count > 0)
      {
         const float oldSpan = oldMax - oldMin;

         for (size_t i = 0; i < BinCount; i++)
         {
            if (_bins[i] == 0)
            {
               continue;
            }

            float representative = oldMin;
            if (oldSpan > 0.0f)
            {
               const float start = oldMin + (oldSpan * i) / BinCount;
               const float end = oldMin + (oldSpan * (i + 1)) / BinCount;
               representative = (start + end) * 0.5f;
            }

            const size_t newIndex = _getBinIndex(representative, _min, _max);
            rebinned[newIndex] += _bins[i];
         }
      }

      for (size_t i = 0; i < BinCount; i++)
      {
         _bins[i] = rebinned[i];
      }
   }
};
