#pragma once

#include <algorithm>
#include <cmath>

#include "Stats.h"

/// <summary>
/// Tracks time-windowed statistics by storing values in rotating time buckets.
/// </summary>
/// <remarks>
/// This class keeps one extra bucket to blend the boundary between the oldest
/// and newest time slices so the rolling window transitions smoothly.
/// </remarks>
template<unsigned long (*TimeFunc)() = millis>
class TimedStatsBase
{
private:
   Stats** _buckets = nullptr;
   uint _numBuckets = 0;
   uint _currentBucket = 0;
   ulong _durationMs = 1;
   ulong _bucketMs = 1;
   unsigned long _startTicks = 0;
   float _elapsedTicks = 0;

    /// <summary>
    /// Advances active bucket(s) based on elapsed time and clears expired buckets.
    /// </summary>
    /// <returns>Elapsed time within the current bucket, in milliseconds.</returns>
    float _advance()
    {
       unsigned long now = TimeFunc();
       float elapsed = (now - _startTicks) + _elapsedTicks;

       while (elapsed > _bucketMs)
       {
          _currentBucket++;
          if (_currentBucket >= _numBuckets)
          {
             _currentBucket = 0;
          }

          _buckets[_currentBucket]->reset();
          elapsed -= _bucketMs;
       }

       _elapsedTicks = elapsed;
       _startTicks = now;
       return elapsed;
    }

   /// <summary>
   /// Accumulates weighted count, sum, and sum-of-squares for the active window.
   /// </summary>
   void _accumulateWeighted(float elapsed, float& weightedCount, float& weightedSum, float& weightedSumSquares)
   {
      weightedCount = 0;
      weightedSum = 0;
      weightedSumSquares = 0;

      float elapsedFraction = elapsed / _bucketMs;
      if (elapsedFraction < 0.0f)
      {
         elapsedFraction = 0.0f;
      }
      else if (elapsedFraction > 1.0f)
      {
         elapsedFraction = 1.0f;
      }

      uint firstBoundaryBucket = _currentBucket + 1;
      if (firstBoundaryBucket >= _numBuckets)
      {
         firstBoundaryBucket = 0;
      }

      uint secondBoundaryBucket = firstBoundaryBucket + 1;
      if (secondBoundaryBucket >= _numBuckets)
      {
         secondBoundaryBucket = 0;
      }

      bool usedCoreBuckets = false;
      for (uint i = 0; i < _numBuckets; i++)
      {
         if ((i == _currentBucket) || (i == firstBoundaryBucket) || (i == secondBoundaryBucket))
         {
            continue;
         }

         size_t bucketCount = _buckets[i]->count();
         if (bucketCount == 0)
         {
            continue;
         }

         float bucketMean = _buckets[i]->get();
         float bucketVariance = _buckets[i]->variance();
         float bucketSumSquares = static_cast<float>(bucketCount) * (bucketVariance + (bucketMean * bucketMean));

         weightedCount += static_cast<float>(bucketCount);
         weightedSum += bucketMean * static_cast<float>(bucketCount);
         weightedSumSquares += bucketSumSquares;
         usedCoreBuckets = true;
      }

      if (usedCoreBuckets)
      {
         return;
      }

      for (uint i = 0; i < _numBuckets; i++)
      {
         size_t bucketCount = _buckets[i]->count();
         if (bucketCount == 0)
         {
            continue;
         }

         float fraction = 1.0f;
         if (i == _currentBucket)
         {
            fraction = elapsedFraction;
         }
         else if (i == firstBoundaryBucket)
         {
            fraction = 1.0f - elapsedFraction;
         }

         float weightedBucketCount = fraction * static_cast<float>(bucketCount);
         if (weightedBucketCount <= 0.0f)
         {
            continue;
         }

         float bucketMean = _buckets[i]->get();
         float bucketVariance = _buckets[i]->variance();
         float bucketSumSquares = static_cast<float>(bucketCount) * (bucketVariance + (bucketMean * bucketMean));

         weightedCount += weightedBucketCount;
         weightedSum += fraction * bucketMean * static_cast<float>(bucketCount);
         weightedSumSquares += fraction * bucketSumSquares;
      }
   }

public:
   /// <summary>
   /// Initializes timed statistics for a duration divided into buckets.
   /// </summary>
   /// <param name="durationMs">Total duration in milliseconds.</param>
   /// <param name="nBuckets">Number of time buckets (plus one blending bucket).</param>
   TimedStatsBase(ulong durationMs, uint nBuckets = 20)
   {
      uint normalizedBuckets = std::max(nBuckets, static_cast<uint>(1));

      _numBuckets = normalizedBuckets + 1;
      _durationMs = std::max(durationMs, static_cast<ulong>(1));
      _bucketMs = std::max(static_cast<ulong>(1), static_cast<ulong>(static_cast<float>(_durationMs) / normalizedBuckets));

      _buckets = new Stats*[_numBuckets];
      for (uint i = 0; i < _numBuckets; i++)
      {
         _buckets[i] = new Stats();
      }

      _startTicks = TimeFunc();
      _elapsedTicks = 0;
   }

   /// <summary>
   /// Releases all bucket storage.
   /// </summary>
   ~TimedStatsBase()
   {
      for (uint i = 0; i < _numBuckets; i++)
      {
         delete _buckets[i];
      }

      delete[] _buckets;
   }

   /// <summary>
   /// Adds a value to the current active bucket.
   /// </summary>
   /// <param name="value">Value to add.</param>
   void set(float value)
   {
      _advance();
      _buckets[_currentBucket]->add(value);
   }

   /// <summary>
   /// Gets the weighted average across active buckets.
   /// </summary>
   /// <returns>Weighted average, or NaN when no values exist.</returns>
   float average()
   {
      float elapsed = _advance();

      float weightedCount = 0;
      float weightedSum = 0;
      float weightedSumSquares = 0;
      _accumulateWeighted(elapsed, weightedCount, weightedSum, weightedSumSquares);

      if (weightedCount == 0)
      {
         return NAN;
      }

      return weightedSum / weightedCount;
   }

   /// <summary>
   /// Gets the minimum value across active buckets.
   /// </summary>
   /// <returns>Minimum value, or NaN when no values exist.</returns>
   float min()
   {
      float elapsed = _advance();

      float low = NAN;
      uint firstBucket = _currentBucket + 1;
      if (firstBucket >= _numBuckets)
      {
         firstBucket = 0;
      }

      uint secondBoundaryBucket = firstBucket + 1;
      if (secondBoundaryBucket >= _numBuckets)
      {
         secondBoundaryBucket = 0;
      }

      bool usedCoreBucket = false;
      for (uint i = 0; i < _numBuckets; i++)
      {
         if ((i == _currentBucket) || (i == firstBucket) || (i == secondBoundaryBucket))
         {
            continue;
         }

         if (_buckets[i]->count() == 0)
         {
            continue;
         }

         float bucketMin = _buckets[i]->min();
         if (std::isnan(low) || bucketMin < low)
         {
            low = bucketMin;
         }

         usedCoreBucket = true;
      }

      if (usedCoreBucket)
      {
         return low;
      }

      for (uint i = 0; i < _numBuckets; i++)
      {
         size_t bucketCount = _buckets[i]->count();
         if (bucketCount == 0)
         {
            continue;
         }

         float fraction = 1;
         if (i == _currentBucket)
         {
            fraction = elapsed / _bucketMs;
         }
         else if (i == firstBucket)
         {
            fraction = 1 - (elapsed / _bucketMs);
         }

         if (fraction < 0.5f)
         {
            continue;
         }

         float weightedBucketCount = fraction * static_cast<float>(bucketCount);
         if (weightedBucketCount < 1.0f)
         {
            continue;
         }

         float bucketMin = _buckets[i]->min();
         if (std::isnan(low) || bucketMin < low)
         {
            low = bucketMin;
         }
      }

      return low;
   }

   /// <summary>
   /// Gets the maximum value across active buckets.
   /// </summary>
   /// <returns>Maximum value, or NaN when no values exist.</returns>
   float max()
   {
      float elapsed = _advance();

      float high = NAN;
      uint firstBucket = _currentBucket + 1;
      if (firstBucket >= _numBuckets)
      {
         firstBucket = 0;
      }

      uint secondBoundaryBucket = firstBucket + 1;
      if (secondBoundaryBucket >= _numBuckets)
      {
         secondBoundaryBucket = 0;
      }

      bool usedCoreBucket = false;
      for (uint i = 0; i < _numBuckets; i++)
      {
         if ((i == _currentBucket) || (i == firstBucket) || (i == secondBoundaryBucket))
         {
            continue;
         }

         if (_buckets[i]->count() == 0)
         {
            continue;
         }

         float bucketMax = _buckets[i]->max();
         if (std::isnan(high) || bucketMax > high)
         {
            high = bucketMax;
         }

         usedCoreBucket = true;
      }

      if (usedCoreBucket)
      {
         return high;
      }

      for (uint i = 0; i < _numBuckets; i++)
      {
         size_t bucketCount = _buckets[i]->count();
         if (bucketCount == 0)
         {
            continue;
         }

         float fraction = 1;
         if (i == _currentBucket)
         {
            fraction = elapsed / _bucketMs;
         }
         else if (i == firstBucket)
         {
            fraction = 1 - (elapsed / _bucketMs);
         }

         if (fraction < 0.5f)
         {
            continue;
         }

         float weightedBucketCount = fraction * static_cast<float>(bucketCount);
         if (weightedBucketCount < 1.0f)
         {
            continue;
         }

         float bucketMax = _buckets[i]->max();
         if (std::isnan(high) || bucketMax > high)
         {
            high = bucketMax;
         }
      }

      return high;
   }

   /// <summary>
   /// Gets max-minus-min across active buckets.
   /// </summary>
   /// <returns>Range value, or NaN when no values exist.</returns>
   float range()
   {
      float elapsed = _advance();

      float low = NAN;
      float high = NAN;
      uint firstBucket = _currentBucket + 1;
      if (firstBucket >= _numBuckets)
      {
         firstBucket = 0;
      }

      uint secondBoundaryBucket = firstBucket + 1;
      if (secondBoundaryBucket >= _numBuckets)
      {
         secondBoundaryBucket = 0;
      }

      bool usedCoreBucket = false;
      for (uint i = 0; i < _numBuckets; i++)
      {
         if ((i == _currentBucket) || (i == firstBucket) || (i == secondBoundaryBucket))
         {
            continue;
         }

         if (_buckets[i]->count() == 0)
         {
            continue;
         }

         float bucketMin = _buckets[i]->min();
         float bucketMax = _buckets[i]->max();

         if (std::isnan(low) || bucketMin < low)
         {
            low = bucketMin;
         }

         if (std::isnan(high) || bucketMax > high)
         {
            high = bucketMax;
         }

         usedCoreBucket = true;
      }

      if (!usedCoreBucket)
      {
         for (uint i = 0; i < _numBuckets; i++)
         {
            size_t bucketCount = _buckets[i]->count();
            if (bucketCount == 0)
            {
               continue;
            }

            float fraction = 1;
            if (i == _currentBucket)
            {
               fraction = elapsed / _bucketMs;
            }
            else if (i == firstBucket)
            {
               fraction = 1 - (elapsed / _bucketMs);
            }

            if (fraction < 0.5f)
            {
               continue;
            }

            float weightedBucketCount = fraction * static_cast<float>(bucketCount);
            if (weightedBucketCount < 1.0f)
            {
               continue;
            }

            float bucketMin = _buckets[i]->min();
            float bucketMax = _buckets[i]->max();

            if (std::isnan(low) || bucketMin < low)
            {
               low = bucketMin;
            }

            if (std::isnan(high) || bucketMax > high)
            {
               high = bucketMax;
            }
         }
      }

      if (!isfinite(low) || !isfinite(high))
      {
         return NAN;
      }

      return high - low;
   }

   /// <summary>
   /// Gets the population standard deviation across active buckets.
   /// </summary>
   /// <returns>Standard deviation, or NaN when no values exist.</returns>
   float stdDev()
   {
      float elapsed = _advance();

      float weightedCount = 0;
      float weightedSum = 0;
      float weightedSumSquares = 0;
      _accumulateWeighted(elapsed, weightedCount, weightedSum, weightedSumSquares);

      if (weightedCount == 0)
      {
         return NAN;
      }

      float avg = weightedSum / weightedCount;
      float avgSquares = weightedSumSquares / weightedCount;
      float variance = avgSquares - (avg * avg);
      if (variance < 0.0f)
      {
         variance = 0.0f;
      }

      return sqrtf(variance);
   }

   /// <summary>
   /// Sets the tracked time duration and clears existing bucket data.
   /// </summary>
   /// <param name="durationMs">New total duration in milliseconds.</param>
   void setDurationMs(ulong durationMs)
   {
      _durationMs = std::max(durationMs, static_cast<ulong>(1));
      _bucketMs = std::max(static_cast<ulong>(1), static_cast<ulong>(static_cast<float>(_durationMs) / (_numBuckets - 1)));
      reset();
   }

   /// <summary>
   /// Gets the configured duration in milliseconds.
   /// </summary>
   /// <returns>Total tracked duration in milliseconds.</returns>
   ulong durationMs() const
   {
      return _durationMs;
   }

   /// <summary>
   /// Resets all bucket values and timing state.
   /// </summary>
   void reset()
   {
      for (uint i = 0; i < _numBuckets; i++)
      {
         _buckets[i]->reset();
      }

      _currentBucket = 0;
      _startTicks = TimeFunc();
      _elapsedTicks = 0;
   }

   /// <summary>
   /// Gets the total count of finite values across active buckets.
   /// </summary>
   /// <returns>Total count of retained values.</returns>
   size_t count()
   {
      float elapsed = _advance();

      float weightedCount = 0;
      float weightedSum = 0;
      float weightedSumSquares = 0;
      _accumulateWeighted(elapsed, weightedCount, weightedSum, weightedSumSquares);

      if (weightedCount <= 0.0f)
      {
         return 0;
      }

      return static_cast<size_t>(weightedCount + 0.5f);
   }

   /// <summary>
   /// Gets the number of allocated buckets.
   /// </summary>
   /// <returns>Bucket count (includes blending bucket).</returns>
   uint numBuckets() const
   {
      return _numBuckets;
   }

   /// <summary>
   /// Gets the current active bucket index.
   /// </summary>
   /// <returns>Zero-based active bucket index.</returns>
   uint currentBucket() const
   {
      return _currentBucket;
   }
};

using TimedStats = TimedStatsBase<millis>;
