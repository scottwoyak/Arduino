#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdint.h>

/// <summary>
/// Represents a batch-built histogram for floating-point values.
/// </summary>
/// <remarks>
/// Values are assigned to bins spanning [min, max].
/// NaN and infinite values are ignored.
/// </remarks>
class Histogram
{
private:
   uint32_t* _bins = nullptr;
   size_t _binCount = 0;
   size_t _count = 0;
   float _min = NAN;
   float _max = NAN;

   static size_t _getBinIndex(float value, float minValue, float maxValue, size_t binCount)
   {
      const float span = maxValue - minValue;
      if ((span <= 0.0f) || (binCount == 0))
      {
         return 0;
      }

      float normalized = (value - minValue) / span;
      if (normalized <= 0.0f)
      {
         return 0;
      }
      if (normalized >= 1.0f)
      {
         return binCount - 1;
      }

      size_t index = static_cast<size_t>(normalized * static_cast<float>(binCount));
      if (index >= binCount)
      {
         index = binCount - 1;
      }

      return index;
   }

   bool _allocateBins(size_t binCount)
   {
      _binCount = binCount;
      _bins = nullptr;
      _count = 0;
      _min = NAN;
      _max = NAN;

      if (_binCount == 0)
      {
         return false;
      }

      _bins = new uint32_t[_binCount];
      if (_bins == nullptr)
      {
         _binCount = 0;
         return false;
      }

      for (size_t i = 0; i < _binCount; i++)
      {
         _bins[i] = 0;
      }

      return true;
   }

   void _build(const float* values, size_t valueCount)
   {
      if ((_bins == nullptr) || (_binCount == 0) || (values == nullptr) || (valueCount == 0))
      {
         return;
      }

      bool hasFinite = false;
      for (size_t i = 0; i < valueCount; i++)
      {
         float value = values[i];
         if (!std::isfinite(value))
         {
            continue;
         }

         if (!hasFinite)
         {
            _min = value;
            _max = value;
            hasFinite = true;
         }
         else
         {
            _min = std::min(_min, value);
            _max = std::max(_max, value);
         }

         _count++;
      }

      if (!hasFinite)
      {
         _count = 0;
         _min = NAN;
         _max = NAN;
         return;
      }

      if (_max <= _min)
      {
         _bins[0] = static_cast<uint32_t>(_count);
         return;
      }

      for (size_t i = 0; i < valueCount; i++)
      {
         float value = values[i];
         if (!std::isfinite(value))
         {
            continue;
         }

         size_t index = _getBinIndex(value, _min, _max, _binCount);
         _bins[index]++;
      }
   }

public:
   Histogram(const float* values, size_t valueCount, size_t binCount)
   {
      if (_allocateBins(binCount))
      {
         _build(values, valueCount);
      }
   }

   Histogram(const float* values, size_t valueCount, size_t minBinCount, size_t maxBinCount)
   {
      size_t binCount = recommendBinCount(values, valueCount, minBinCount, maxBinCount);
      if (_allocateBins(binCount))
      {
         _build(values, valueCount);
      }
   }

   ~Histogram()
   {
      delete[] _bins;
   }

   Histogram(const Histogram&) = delete;
   Histogram& operator=(const Histogram&) = delete;

   /// <summary>
   /// Recommends a histogram bin count from input values using Freedman-Diaconis with fallback.
   /// </summary>
   static size_t recommendBinCount(const float* values, size_t valueCount, size_t minBinCount, size_t maxBinCount)
   {
      if ((values == nullptr) || (valueCount < 2) || (minBinCount == 0) || (maxBinCount < minBinCount))
      {
         return minBinCount;
      }

      float minValue = NAN;
      float maxValue = NAN;
      size_t finiteCount = 0;

      for (size_t i = 0; i < valueCount; i++)
      {
         float value = values[i];
         if (!std::isfinite(value))
         {
            continue;
         }

         if (finiteCount == 0)
         {
            minValue = value;
            maxValue = value;
         }
         else
         {
            minValue = std::min(minValue, value);
            maxValue = std::max(maxValue, value);
         }

         finiteCount++;
      }

      if (finiteCount < 2)
      {
         return minBinCount;
      }

      const float range = maxValue - minValue;
      if (!std::isfinite(range) || (range <= 0.0f))
      {
         return minBinCount;
      }

      float* sorted = new float[finiteCount];
      if (sorted == nullptr)
      {
         return minBinCount;
      }

      size_t sortedCount = 0;
      float sum = 0.0f;
      float sumSq = 0.0f;

      for (size_t i = 0; i < valueCount; i++)
      {
         float value = values[i];
         if (!std::isfinite(value))
         {
            continue;
         }

         sorted[sortedCount++] = value;
         sum += value;
         sumSq += (value * value);
      }

      for (size_t i = 1; i < sortedCount; i++)
      {
         float key = sorted[i];
         size_t j = i;
         while ((j > 0) && (sorted[j - 1] > key))
         {
            sorted[j] = sorted[j - 1];
            j--;
         }
         sorted[j] = key;
      }

      size_t q1Index = (sortedCount - 1) / 4;
      size_t q3Index = ((sortedCount - 1) * 3) / 4;
      float iqr = sorted[q3Index] - sorted[q1Index];

      float nRoot = cbrtf(static_cast<float>(sortedCount));
      float fdBinWidth = (nRoot > 0.0f) ? (2.0f * iqr / nRoot) : NAN;

      size_t binCount = minBinCount;
      if (std::isfinite(fdBinWidth) && (fdBinWidth > 0.0f))
      {
         binCount = static_cast<size_t>(ceilf(range / fdBinWidth));
      }
      else
      {
         float mean = sum / static_cast<float>(sortedCount);
         float variance = (sumSq / static_cast<float>(sortedCount)) - (mean * mean);
         if (variance < 0.0f)
         {
            variance = 0.0f;
         }

         float stdDev = sqrtf(variance);
         float scottBinWidth = (std::isfinite(stdDev) && (nRoot > 0.0f)) ? (3.5f * stdDev / nRoot) : NAN;

         if (std::isfinite(scottBinWidth) && (scottBinWidth > 0.0f))
         {
            binCount = static_cast<size_t>(ceilf(range / scottBinWidth));
         }
         else
         {
            binCount = static_cast<size_t>(ceilf(1.0f + log2f(static_cast<float>(sortedCount))));
         }
      }

      delete[] sorted;

      if (binCount < minBinCount)
      {
         binCount = minBinCount;
      }
      if (binCount > maxBinCount)
      {
         binCount = maxBinCount;
      }

      return binCount;
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
      if ((_bins == nullptr) || (index >= _binCount))
      {
         return 0;
      }

      return _bins[index];
   }

   /// <summary>
   /// Gets the configured number of bins.
   /// </summary>
   size_t bins() const
   {
      return _binCount;
   }
};
