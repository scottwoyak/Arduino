#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

#include <Stats.h>

/// <summary>
/// Tracks time-windowed statistics by storing values in rotating time buckets.
/// </summary>
/// <remarks>
/// This class keeps one extra bucket to blend the boundary between the oldest
/// and newest time slices, similar to TimedAverager behavior.
/// </remarks>
class TimedStats
{
private:
   Stats** _buckets = nullptr;
   uint _numBuckets = 0;
   uint _currentBucket = 0;
   ulong _durationMs = 1;
   ulong _bucketMs = 1;

   unsigned long (*_ticks)() = nullptr;
   unsigned long _startTicks = 0;
   float _elapsedTicks = 0;

   /// <summary>
   /// Advances active bucket(s) based on elapsed time and clears expired buckets.
   /// </summary>
   /// <returns>Elapsed time within the current bucket, in milliseconds.</returns>
   float _advance()
   {
      unsigned long now = _ticks();
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

public:
   /// <summary>
   /// Optional clock provider used by tests. Falls back to millis() when null.
   /// </summary>
   static unsigned long (*tickFunc)();

   /// <summary>
   /// Initializes timed statistics for a duration divided into buckets.
   /// </summary>
   /// <param name="durationMs">Total duration in milliseconds.</param>
   /// <param name="nBuckets">Number of time buckets (plus one blending bucket).</param>
   TimedStats(ulong durationMs, uint nBuckets = 10)
   {
      uint normalizedBuckets = std::max(nBuckets, (uint)1);

      _numBuckets = normalizedBuckets + 1;
      _durationMs = std::max((ulong)1, durationMs);
      _bucketMs = std::max((ulong)1, (ulong)((float)_durationMs / normalizedBuckets));

      _buckets = new Stats*[_numBuckets];
      for (uint i = 0; i < _numBuckets; i++)
      {
         _buckets[i] = new Stats();
      }

      if (TimedStats::tickFunc != nullptr)
      {
         _ticks = TimedStats::tickFunc;
      }
      else
      {
         _ticks = millis;
      }

      _startTicks = _ticks();
      _elapsedTicks = 0;
   }

   /// <summary>
   /// Releases all bucket storage.
   /// </summary>
   ~TimedStats()
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
   float get()
   {
      float elapsed = _advance();

      float total = 0;
      float count = 0;

      uint firstBucket = _currentBucket + 1;
      if (firstBucket >= _numBuckets)
      {
         firstBucket = 0;
      }

      for (uint i = 0; i < _numBuckets; i++)
      {
         if (_buckets[i]->count() == 0)
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

         total += fraction * _buckets[i]->get() * _buckets[i]->count();
         count += fraction * _buckets[i]->count();
      }

      if (count == 0)
      {
         return NAN;
      }

      return total / count;
   }

   /// <summary>
   /// Gets the minimum value across active buckets.
   /// </summary>
   /// <returns>Minimum value, or NaN when no values exist.</returns>
   float min()
   {
      _advance();

      float low = NAN;
      for (uint i = 0; i < _numBuckets; i++)
      {
         if (_buckets[i]->count() == 0)
         {
            continue;
         }

         float bucketMin = _buckets[i]->min();
         if (isnan(low) || bucketMin < low)
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
      _advance();

      float high = NAN;
      for (uint i = 0; i < _numBuckets; i++)
      {
         if (_buckets[i]->count() == 0)
         {
            continue;
         }

         float bucketMax = _buckets[i]->max();
         if (isnan(high) || bucketMax > high)
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
      float low = min();
      float high = max();

      if (!isfinite(low) || !isfinite(high))
      {
         return NAN;
      }

      return high - low;
   }

   /// <summary>
   /// Sets the tracked time duration and clears existing bucket data.
   /// </summary>
   /// <param name="durationMs">New total duration in milliseconds.</param>
   void setDurationMs(ulong durationMs)
   {
      _durationMs = std::max((ulong)1, durationMs);
      _bucketMs = std::max((ulong)1, (ulong)((float)_durationMs / (_numBuckets - 1)));
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
      _startTicks = _ticks();
      _elapsedTicks = 0;
   }

   /// <summary>
   /// Gets the total count of finite values across active buckets.
   /// </summary>
   /// <returns>Total count of retained values.</returns>
   size_t count()
   {
      _advance();

      size_t totalCount = 0;
      for (uint i = 0; i < _numBuckets; i++)
      {
         totalCount += _buckets[i]->count();
      }

      return totalCount;
   }

   /// <summary>
   /// Gets the number of allocated buckets.
   /// </summary>
   /// <returns>Bucket count (includes blending bucket).</returns>
   uint numBuckets()
   {
      return _numBuckets;
   }

   /// <summary>
   /// Gets the current active bucket index.
   /// </summary>
   /// <returns>Zero-based active bucket index.</returns>
   uint currentBucket()
   {
      return _currentBucket;
   }
};

unsigned long (*TimedStats::tickFunc)() = nullptr;
