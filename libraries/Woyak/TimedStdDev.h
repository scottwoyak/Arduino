#pragma once

#include <Arduino.h>
#include <cmath>

#include "TimedStats.h"

/// <summary>
/// Computes population standard deviation over a rolling time window.
/// </summary>
/// <remarks>
/// Internally tracks x and x^2 using TimedStats and evaluates:
/// stddev = sqrt(E[x^2] - (E[x])^2)
/// </remarks>
class TimedStdDev
{
private:
   TimedStats _values;
   TimedStats _squares;

public:
   /// <summary>
   /// Initializes timed standard deviation over the specified duration.
   /// </summary>
   /// <param name="durationMs">Total rolling window duration in milliseconds.</param>
   /// <param name="nBuckets">Number of time buckets (plus one blending bucket).</param>
   explicit TimedStdDev(ulong durationMs, uint nBuckets = 10)
      : _values(durationMs, nBuckets),
        _squares(durationMs, nBuckets)
   {
   }

   TimedStdDev(const TimedStdDev&) = delete;
   TimedStdDev& operator=(const TimedStdDev&) = delete;

   /// <summary>
   /// Adds a value to the current timed bucket.
   /// </summary>
   /// <param name="value">Value to add.</param>
   void set(float value)
   {
      _values.set(value);
      _squares.set(value * value);
   }

   /// <summary>
   /// Gets the rolling mean of values.
   /// </summary>
   /// <returns>Mean over the active timed window, or NaN when empty.</returns>
   float mean()
   {
      return _values.average();
   }

   /// <summary>
   /// Alias for mean().
   /// </summary>
   /// <returns>Average over the active timed window, or NaN when empty.</returns>
   float average()
   {
      return mean();
   }

   /// <summary>
   /// Gets the rolling population standard deviation.
   /// </summary>
   /// <returns>Population standard deviation, or NaN when empty.</returns>
   float get()
   {
      float avg = _values.average();
      float avgSquare = _squares.average();
      if (!isfinite(avg) || !isfinite(avgSquare))
      {
         return NAN;
      }

      float variance = avgSquare - (avg * avg);
      if (variance < 0.0f)
      {
         variance = 0.0f;
      }

      return sqrtf(variance);
   }

   /// <summary>
   /// Alias for get().
   /// </summary>
   /// <returns>Population standard deviation, or NaN when empty.</returns>
   float sigma()
   {
      return get();
   }

   /// <summary>
   /// Alias for get().
   /// </summary>
   /// <returns>Population standard deviation, or NaN when empty.</returns>
   float stdDev()
   {
      return get();
   }

   /// <summary>
   /// Gets the minimum value across the timed window.
   /// </summary>
   /// <returns>Minimum value, or NaN when empty.</returns>
   float min()
   {
      return _values.min();
   }

   /// <summary>
   /// Gets the maximum value across the timed window.
   /// </summary>
   /// <returns>Maximum value, or NaN when empty.</returns>
   float max()
   {
      return _values.max();
   }

   /// <summary>
   /// Gets the value range across the timed window.
   /// </summary>
   /// <returns>Range (max-min), or NaN when empty.</returns>
   float range()
   {
      return _values.range();
   }

   /// <summary>
   /// Gets the count of retained values in the timed window.
   /// </summary>
   /// <returns>Value count across active buckets.</returns>
   size_t count()
   {
      return _values.count();
   }

   /// <summary>
   /// Clears all timed buckets and resets timing state.
   /// </summary>
   void reset()
   {
      _values.reset();
      _squares.reset();
   }

   /// <summary>
   /// Sets the tracked duration and clears existing window data.
   /// </summary>
   /// <param name="durationMs">New rolling window duration in milliseconds.</param>
   void setDurationMs(ulong durationMs)
   {
      _values.setDurationMs(durationMs);
      _squares.setDurationMs(durationMs);
   }

   /// <summary>
   /// Gets the configured rolling window duration.
   /// </summary>
   /// <returns>Duration in milliseconds.</returns>
   ulong durationMs() const
   {
      return _values.durationMs();
   }

   /// <summary>
   /// Gets the number of allocated timed buckets.
   /// </summary>
   /// <returns>Bucket count, including blending bucket.</returns>
   uint numBuckets() const
   {
      return _values.numBuckets();
   }

   /// <summary>
   /// Gets the current active timed bucket index.
   /// </summary>
   /// <returns>Zero-based active bucket index.</returns>
   uint currentBucket() const
   {
      return _values.currentBucket();
   }
};
