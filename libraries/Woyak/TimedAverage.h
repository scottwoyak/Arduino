#pragma once

#include <Arduino.h>
#include <algorithm>
#include <cmath>

/// <summary>
/// Tracks time-windowed averages by storing values in rotating time buckets.
/// </summary>
/// <remarks>
/// This class keeps one extra bucket to blend the boundary between the oldest
/// and newest time slices so the rolling window transitions smoothly.
/// </remarks>
template<unsigned long (*TimeFunc)() = millis>
class TimedAverageBase
{
private:
   float* _bucketSums = nullptr;
   size_t* _bucketCounts = nullptr;
   uint _numBuckets = 0;
   uint _currentBucket = 0;
   unsigned long _durationMs = 1;
   unsigned long _bucketMs = 1;

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

         _bucketSums[_currentBucket] = 0.0f;
         _bucketCounts[_currentBucket] = 0;
         elapsed -= _bucketMs;
      }

      _elapsedTicks = elapsed;
      _startTicks = now;
      return elapsed;
   }

   void _clearBuckets()
   {
      for (uint i = 0; i < _numBuckets; i++)
      {
         _bucketSums[i] = 0.0f;
         _bucketCounts[i] = 0;
      }
   }

public:
   /// <summary>
   /// Initializes timed averages for a duration divided into buckets.
   /// </summary>
   /// <param name="durationMs">Total duration in milliseconds.</param>
   /// <param name="nBuckets">Number of time buckets (plus one blending bucket).</param>
   TimedAverageBase(unsigned long durationMs, uint nBuckets = 10)
   {
      uint normalizedBuckets = std::max(nBuckets, static_cast<uint>(1));

      _numBuckets = normalizedBuckets + 1;
      _durationMs = std::max(durationMs, 1UL);
      _bucketMs = std::max(1UL, static_cast<unsigned long>(static_cast<float>(_durationMs) / normalizedBuckets));

      _bucketSums = new float[_numBuckets];
      _bucketCounts = new size_t[_numBuckets];
      _clearBuckets();

      _startTicks = TimeFunc();
      _elapsedTicks = 0;
   }

   /// <summary>
   /// Releases all bucket storage.
   /// </summary>
   ~TimedAverageBase()
   {
      delete[] _bucketSums;
      delete[] _bucketCounts;
   }

   TimedAverageBase(const TimedAverageBase&) = delete;
   TimedAverageBase& operator=(const TimedAverageBase&) = delete;

   /// <summary>
   /// Adds a value to the current active bucket.
   /// </summary>
   /// <param name="value">Value to add.</param>
   void set(float value)
   {
      _advance();
      if (isfinite(value))
      {
         _bucketSums[_currentBucket] += value;
         _bucketCounts[_currentBucket]++;
      }
   }

   /// <summary>
   /// Gets the weighted average across active buckets.
   /// </summary>
   /// <returns>Weighted average, or NaN when no values exist.</returns>
   float average()
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
         if (_bucketCounts[i] == 0)
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

         total += fraction * _bucketSums[i];
         count += fraction * _bucketCounts[i];
      }

      if (count == 0)
      {
         return NAN;
      }

      return total / count;
   }

   /// <summary>
   /// Gets the weighted average across active buckets.
   /// </summary>
   /// <returns>Weighted average, or NaN when no values exist.</returns>
   float get()
   {
      return average();
   }

   /// <summary>
   /// Sets the tracked time duration and clears existing bucket data.
   /// </summary>
   /// <param name="durationMs">New total duration in milliseconds.</param>
   void setDurationMs(unsigned long durationMs)
   {
      _durationMs = std::max(durationMs, 1UL);
      _bucketMs = std::max(1UL, static_cast<unsigned long>(static_cast<float>(_durationMs) / (_numBuckets - 1)));
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
      _clearBuckets();
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
      _advance();

      size_t totalCount = 0;
      for (uint i = 0; i < _numBuckets; i++)
      {
         totalCount += _bucketCounts[i];
      }

      return totalCount;
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

using TimedAverage = TimedAverageBase<millis>;
