#pragma once

#include <algorithm>
#include <cmath>

#include "Stats.h"
#include "TimedMinMax.h"

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
   unsigned long _durationMs = 1;
   unsigned long _bucketMs = 1;
   unsigned long _startTicks = 0;
   float _elapsedTicks = 0;
   TimedMinMaxBase<TimeFunc> _minMax;

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
   /// Accumulates weighted count, sum, and pooled sum-of-squared-deviations (relative to
   /// <paramref name="referenceMean"/>) for the active window.
   /// </summary>
   /// <remarks>
   /// Accumulating squared deviations relative to the overall mean (rather than the raw
   /// values) avoids catastrophic cancellation when computing variance for values with a
   /// large offset and small spread (e.g. a temperature reading around 98.6 with noise of
   /// only a few hundredths). Pass 0.0f as <paramref name="referenceMean"/> when only the
   /// weighted count/sum (i.e. the average) are needed.
   /// </remarks>
   void _accumulateWeighted(float elapsed, float referenceMean, float& weightedCount, float& weightedSum, float& weightedSumSquares)
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
         float meanDeviation = bucketMean - referenceMean;
         float bucketSumSquares = static_cast<float>(bucketCount) * (bucketVariance + (meanDeviation * meanDeviation));

         weightedCount += static_cast<float>(bucketCount);
         weightedSum += bucketMean * static_cast<float>(bucketCount);
         weightedSumSquares += bucketSumSquares;
         usedCoreBuckets = true;
      }

      if (usedCoreBuckets)
      {
         auto accumulateBucket = [&](uint bucketIndex, float fraction)
         {
            size_t bucketCount = _buckets[bucketIndex]->count();
            if (bucketCount == 0)
            {
               return;
            }

            float weightedBucketCount = fraction * static_cast<float>(bucketCount);
            if (weightedBucketCount < 1.0f)
            {
               return;
            }

            float bucketMean = _buckets[bucketIndex]->get();
            float bucketVariance = _buckets[bucketIndex]->variance();
            float meanDeviation = bucketMean - referenceMean;
            float bucketSumSquares = static_cast<float>(bucketCount) * (bucketVariance + (meanDeviation * meanDeviation));

            weightedCount += weightedBucketCount;
            weightedSum += fraction * bucketMean * static_cast<float>(bucketCount);
            weightedSumSquares += fraction * bucketSumSquares;
         };

         accumulateBucket(_currentBucket, elapsedFraction);
         accumulateBucket(firstBoundaryBucket, 1.0f - elapsedFraction);

         if (_buckets[firstBoundaryBucket]->count() > 0)
         {
            accumulateBucket(secondBoundaryBucket, 1.0f);
         }

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
         float meanDeviation = bucketMean - referenceMean;
         float bucketSumSquares = static_cast<float>(bucketCount) * (bucketVariance + (meanDeviation * meanDeviation));

         weightedCount += weightedBucketCount;
         weightedSum += fraction * bucketMean * static_cast<float>(bucketCount);
         weightedSumSquares += fraction * bucketSumSquares;
      }
   }

   /// <summary>
   /// Computes the effective min/max tracking window so it matches the window used by
   /// the weighted average/stdDev calculations, which exclude the current bucket's
   /// boundary blending buckets.
   /// </summary>
   /// <param name="durationMs">Total duration in milliseconds.</param>
   /// <param name="bucketMs">Duration of a single bucket in milliseconds.</param>
   /// <returns>Effective duration, in milliseconds, to track min/max values over.</returns>
   static unsigned long _minMaxDurationMs(unsigned long durationMs, unsigned long bucketMs)
   {
      unsigned long effective = (durationMs > bucketMs) ? (durationMs - bucketMs) : durationMs;
      return std::max(effective, 1UL);
   }

public:
   /// <summary>
   /// Initializes timed statistics for a duration divided into buckets.
   /// </summary>
   /// <param name="durationMs">Total duration in milliseconds.</param>
   /// <param name="nBuckets">Number of time buckets (plus one blending bucket).</param>
   TimedStatsBase(unsigned long durationMs, uint nBuckets = 20) :
      _minMax(_minMaxDurationMs(std::max(durationMs, 1UL),
         std::max(1UL, static_cast<unsigned long>(static_cast<float>(std::max(durationMs, 1UL)) / std::max(nBuckets, static_cast<uint>(1))))))
   {
      uint normalizedBuckets = std::max(nBuckets, static_cast<uint>(1));

      _numBuckets = normalizedBuckets + 1;
      _durationMs = std::max(durationMs, 1UL);
      _bucketMs = std::max(1UL, static_cast<unsigned long>(static_cast<float>(_durationMs) / normalizedBuckets));

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
      _minMax.set(value);
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
      _accumulateWeighted(elapsed, 0.0f, weightedCount, weightedSum, weightedSumSquares);

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
      _advance();
      return _minMax.min();
   }

   /// <summary>
   /// Gets the maximum value across active buckets.
   /// </summary>
   /// <returns>Maximum value, or NaN when no values exist.</returns>
   float max()
   {
      _advance();
      return _minMax.max();
   }

   /// <summary>
   /// Gets max-minus-min across active buckets.
   /// </summary>
   /// <returns>Range value, or NaN when no values exist.</returns>
   float range()
   {
      _advance();
      return _minMax.range();
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
      _accumulateWeighted(elapsed, 0.0f, weightedCount, weightedSum, weightedSumSquares);

      if (weightedCount == 0)
      {
         return NAN;
      }

      float avg = weightedSum / weightedCount;

      // Recompute the pooled sum-of-squares as deviations from the overall mean, rather
      // than from zero, to avoid catastrophic cancellation when values have a large
      // offset and small spread.
      _accumulateWeighted(elapsed, avg, weightedCount, weightedSum, weightedSumSquares);
      float variance = weightedSumSquares / weightedCount;
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
   void setDurationMs(unsigned long durationMs)
   {
      _durationMs = std::max(durationMs, 1UL);
      _bucketMs = std::max(1UL, static_cast<unsigned long>(static_cast<float>(_durationMs) / (_numBuckets - 1)));
      _minMax.setDurationMs(_minMaxDurationMs(_durationMs, _bucketMs));
      reset();
   }

   /// <summary>
   /// Gets the configured duration in milliseconds.
   /// </summary>
   /// <returns>Total tracked duration in milliseconds.</returns>
   unsigned long durationMs() const
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
      _minMax.reset();
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
      _accumulateWeighted(elapsed, 0.0f, weightedCount, weightedSum, weightedSumSquares);

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
