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
public:
   /// <summary>
   /// Sentinel nBuckets value (the default) requesting that the bucket count be determined
   /// automatically from the observed spacing between set() calls, rather than requiring the
   /// caller to know/specify a sampling rate up front. Pass an explicit bucket count instead
   /// to opt out of auto-sizing entirely.
   /// </summary>
   static constexpr uint AUTO_NUM_BUCKETS = static_cast<uint>(-1);

private:
   // Auto-sizing targets roughly this many samples per bucket once the sampling interval has
   // been measured, bounded to keep both memory use and boundary-approximation error in check.
   static constexpr float AUTO_BUCKET_WIDTH_SAMPLES = 4.0f;
   static constexpr uint AUTO_MIN_BUCKETS = 3;
   static constexpr uint AUTO_MAX_BUCKETS = 60;
   // Number of observed inter-sample gaps averaged together before finalizing the auto bucket
   // count; a few samples is enough to settle on a stable estimate without delaying too long.
   static constexpr uint8_t AUTO_RATE_ESTIMATE_SAMPLES = 5;

   float* _bucketSums = nullptr;
   size_t* _bucketCounts = nullptr;
   // Sum, per bucket, of each sample's offset (in ms) from the start of that bucket at the
   // moment it was recorded. Combined with _bucketCounts, this gives the true centroid time
   // of a bucket's samples (avgOffset = sum/count), which can differ from the bucket's
   // geometric midpoint whenever samples don't uniformly cover the bucket's full span (e.g.
   // sampling at the start of each sub-interval rather than continuously) - using the
   // geometric midpoint in that case would introduce a systematic bias for a changing signal.
   float* _bucketOffsetSums = nullptr;
   float* _scratchAges = nullptr;
   float* _scratchMeans = nullptr;
   uint _capacity = 0;
   uint _numBuckets = 0;
   uint _currentBucket = 0;
   unsigned long _durationMs = 1;
   unsigned long _bucketMs = 1;

   unsigned long _startTicks = 0;
   float _elapsedTicks = 0;

   // Running totals maintained incrementally so count()/isFull() don't need to rescan every
   // bucket each call.
   float _rawSumAll = 0.0f;
   uint _bucketsWithData = 0;
   size_t _totalCount = 0;

   // State for auto-sizing: measures the average spacing between set() calls so the bucket
   // count/width can be chosen to target a small, roughly-constant number of samples per
   // bucket, without the caller ever specifying a sampling rate.
   bool _autoSizing = false;
   bool _autoSizingFinalized = true;
   bool _hasPreviousSampleTicks = false;
   unsigned long _previousSampleTicks = 0;
   float _measuredIntervalSumMs = 0.0f;
   uint8_t _intervalSampleCount = 0;

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

         _clearBucket(_currentBucket);
         elapsed -= _bucketMs;
      }

      _elapsedTicks = elapsed;
      _startTicks = now;
      return elapsed;
   }

   /// <summary>
   /// Clears bucket i and removes its prior contribution from the running totals.
   /// </summary>
   void _clearBucket(uint i)
   {
      if (_bucketCounts[i] > 0)
      {
         _rawSumAll -= _bucketSums[i];
         _bucketsWithData--;
         _totalCount -= _bucketCounts[i];
      }

      _bucketSums[i] = 0.0f;
      _bucketCounts[i] = 0;
      _bucketOffsetSums[i] = 0.0f;
   }

   void _clearBuckets()
   {
      for (uint i = 0; i < _numBuckets; i++)
      {
         _bucketSums[i] = 0.0f;
         _bucketCounts[i] = 0;
         _bucketOffsetSums[i] = 0.0f;
      }

      _rawSumAll = 0.0f;
      _bucketsWithData = 0;
      _totalCount = 0;
   }

   /// <summary>
   /// Applies a (clamped) logical bucket count within the already-allocated capacity. Does not
   /// touch timing state. When preserveTotals is true, any raw sum/count already accumulated
   /// (from the provisional pre-finalization sizing) is folded into bucket 0 instead of being
   /// discarded, so mid-stream auto-sizing finalization doesn't lose already-recorded data.
   /// </summary>
   void _applyBucketCount(uint requestedBuckets, bool preserveTotals = false)
   {
      float preservedSum = _rawSumAll;
      size_t preservedCount = _totalCount;

      uint normalizedBuckets = std::max(requestedBuckets, static_cast<uint>(1));
      _numBuckets = normalizedBuckets + 1;
      _bucketMs = std::max(1UL, static_cast<unsigned long>(static_cast<float>(_durationMs) / normalizedBuckets));
      _clearBuckets();

      if (preserveTotals && preservedCount > 0)
      {
         _bucketSums[0] = preservedSum;
         _bucketCounts[0] = preservedCount;
         // Approximate the preserved samples as centered within bucket 0 (no per-sample
         // offset history survives finalization), which is a reasonable estimate since
         // they were gathered over a short span relative to a full bucket.
         _bucketOffsetSums[0] = preservedCount * (static_cast<float>(_bucketMs) * 0.5f);
         _rawSumAll = preservedSum;
         _totalCount = preservedCount;
         _bucketsWithData = 1;
      }
   }

   /// <summary>
   /// Finalizes auto-sizing given a measured average inter-sample interval, choosing a bucket
   /// count that targets AUTO_BUCKET_WIDTH_SAMPLES samples per bucket (clamped to a sane range).
   /// </summary>
   void _finalizeAutoSizing(float avgIntervalMs)
   {
      float bucketMsTarget = std::max(1.0f, avgIntervalMs) * AUTO_BUCKET_WIDTH_SAMPLES;
      uint bucketsFromRate = static_cast<uint>(static_cast<float>(_durationMs) / bucketMsTarget);
      uint clamped = std::min(AUTO_MAX_BUCKETS, std::max(AUTO_MIN_BUCKETS, bucketsFromRate));

      // Fold the handful of samples collected before finalization into bucket 0 so
      // count()/isFull() stay accurate; average() only relies on raw totals (not
      // per-bucket age) until the window is fully populated, so the provisional bucket's
      // approximate age doesn't bias the reconstructed average once real bucket data
      // (gathered under the finalized width) eventually fills the window.
      _applyBucketCount(clamped, /*preserveTotals*/ true);
      _autoSizingFinalized = true;
   }

   /// <summary>
   /// Observes the spacing between successive set() calls while auto-sizing is pending, and
   /// finalizes the bucket count once enough samples have been seen to settle on a stable
   /// estimate of the sampling interval.
   /// </summary>
   void _observeSampleInterval()
   {
      unsigned long now = TimeFunc();
      if (_hasPreviousSampleTicks)
      {
         _measuredIntervalSumMs += static_cast<float>(now - _previousSampleTicks);
         _intervalSampleCount++;

         if (_intervalSampleCount >= AUTO_RATE_ESTIMATE_SAMPLES)
         {
            _finalizeAutoSizing(_measuredIntervalSumMs / _intervalSampleCount);

            // Restart bucket timing cleanly under the newly finalized sizing rather than
            // mixing in elapsed time measured against the provisional bucket width.
            _currentBucket = 0;
            _startTicks = TimeFunc();
            _elapsedTicks = 0;
         }
      }

      _previousSampleTicks = now;
      _hasPreviousSampleTicks = true;
   }

   /// <summary>
   /// Gets the mean of bucket i, or NAN if the bucket has no data.
   /// </summary>
   float _bucketMean(uint i) const
   {
      if (_bucketCounts[i] == 0)
      {
         return NAN;
      }

      return _bucketSums[i] / _bucketCounts[i];
   }

public:
   /// <summary>
   /// Initializes timed averages for a duration divided into buckets.
   /// </summary>
   /// <param name="durationMs">Total duration in milliseconds.</param>
   /// <param name="nBuckets">
   /// Number of time buckets (plus one blending bucket). Defaults to AUTO_NUM_BUCKETS, which
   /// determines the bucket count automatically from the observed spacing between set() calls
   /// (targeting a small, roughly-constant number of samples per bucket) instead of requiring
   /// the caller to know/specify a sampling rate up front. Pass an explicit value to opt out.
   /// </param>
   TimedAverageBase(unsigned long durationMs, uint nBuckets = AUTO_NUM_BUCKETS)
   {
      _durationMs = std::max(durationMs, 1UL);
      _autoSizing = (nBuckets == AUTO_NUM_BUCKETS);

      uint initialBuckets = _autoSizing ? AUTO_MIN_BUCKETS : std::max(nBuckets, static_cast<uint>(1));
      _capacity = (_autoSizing ? AUTO_MAX_BUCKETS : initialBuckets) + 1;

      _bucketSums = new float[_capacity];
      _bucketCounts = new size_t[_capacity];
      _bucketOffsetSums = new float[_capacity];
      _scratchAges = new float[_capacity];
      _scratchMeans = new float[_capacity];
      _applyBucketCount(initialBuckets);

      _autoSizingFinalized = !_autoSizing;

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
      delete[] _bucketOffsetSums;
      delete[] _scratchAges;
      delete[] _scratchMeans;
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

      if (_autoSizing && !_autoSizingFinalized)
      {
         _observeSampleInterval();
      }

      if (isfinite(value))
      {
         uint i = _currentBucket;
         if (_bucketCounts[i] == 0)
         {
            _bucketsWithData++;
         }

         _bucketSums[i] += value;
         _bucketCounts[i]++;
         _bucketOffsetSums[i] += _elapsedTicks;
         _totalCount++;
         _rawSumAll += value;
      }
   }

   /// <summary>
   /// Gets the time-weighted average across active buckets.
   /// </summary>
   /// <remarks>
   /// Each bucket's stored mean is treated as a sample of the signal located at that
   /// bucket's true sample centroid time (the average timestamp of the values recorded into
   /// it, tracked via per-bucket offset sums), not merely its geometric midpoint - the two
   /// differ whenever samples don't uniformly cover a bucket's full time span, and using the
   /// geometric midpoint in that case would bias a changing (e.g. ramping) signal. The
   /// windowed average is then reconstructed by piecewise-linearly interpolating between
   /// consecutive bucket centroids (linear extrapolation beyond the first/last known
   /// centroid, using the slope of the nearest two) and integrating that interpolant over
   /// the trailing window [now - durationMs, now], dividing by the window width. This keeps
   /// the reconstruction exact for a linear trend once the window is full.
   /// </remarks>
   /// <returns>Weighted average, or NaN when no values exist.</returns>
   float average()
   {
      float elapsed = _advance();

      // During startup (window not yet fully populated), fall back to a simple arithmetic
      // mean of whatever raw data exists rather than time-weighting by nominal bucket
      // width, which would badly overweight sparse data relative to the actual sample
      // spacing seen so far.
      if (_bucketsWithData < _numBuckets)
      {
         return (_totalCount == 0) ? NAN : (_rawSumAll / static_cast<float>(_totalCount));
      }

      float windowAge = static_cast<float>(_durationMs);

      // Collect (age, mean) for every bucket with data, walking newest to oldest (i.e.
      // increasing age), into scratch buffers sized to the allocated bucket capacity.
      float* ages = _scratchAges;
      float* means = _scratchMeans;
      uint n = 0;

      for (uint k = 0; k < _numBuckets && n < _capacity; k++)
      {
         uint idx = (_currentBucket + _numBuckets - k) % _numBuckets;
         float mean = _bucketMean(idx);
         if (isnan(mean))
         {
            continue;
         }

         // ageStart is how long ago (in ms) this bucket's start (oldest edge) was, relative
         // to now. The current bucket (k == 0) started "elapsed" ms ago; each older bucket
         // started one additional _bucketMs further back. A sample recorded avgOffset ms
         // after its bucket's start therefore occurred (ageStart - avgOffset) ms ago; using
         // this true per-bucket sample centroid (rather than assuming the bucket's geometric
         // midpoint) avoids a systematic bias for a changing signal when samples don't
         // uniformly cover a bucket's full span.
         float ageStart = (k == 0) ? elapsed : (elapsed + k * static_cast<float>(_bucketMs));
         float avgOffset = _bucketOffsetSums[idx] / static_cast<float>(_bucketCounts[idx]);
         float age = ageStart - avgOffset;

         ages[n] = age;
         means[n] = mean;
         n++;
      }

      if (n == 0)
      {
         return NAN;
      }

      if (n == 1)
      {
         return means[0];
      }

      // Extend the piecewise-linear function defined by the known midpoints flat/linearly
      // so it covers the full window [0, windowAge]: extrapolate the near end (age 0) using
      // the slope of the first two points, and the far end (age windowAge) using the slope
      // of the last two points.
      float slopeNear = (means[1] - means[0]) / (ages[1] - ages[0]);
      float valAtZero = means[0] + slopeNear * (0.0f - ages[0]);

      float slopeFar = (means[n - 1] - means[n - 2]) / (ages[n - 1] - ages[n - 2]);
      float valAtEnd = means[n - 1] + slopeFar * (windowAge - ages[n - 1]);

      // Integrate the piecewise-linear function over [0, windowAge] via the trapezoidal
      // rule across all segments (including the two extrapolated end segments).
      float integral = 0.0f;
      float prevAge = 0.0f;
      float prevVal = valAtZero;

      for (uint i = 0; i < n; i++)
      {
         integral += (prevVal + means[i]) * 0.5f * (ages[i] - prevAge);
         prevAge = ages[i];
         prevVal = means[i];
      }

      integral += (prevVal + valAtEnd) * 0.5f * (windowAge - prevAge);

      return integral / windowAge;
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
   /// Gets the configured duration in milliseconds.
   /// </summary>
   /// <returns>Total tracked duration in milliseconds.</returns>
   unsigned long durationMs() const
   {
      return _durationMs;
   }

   /// <summary>
   /// Gets the total count of finite values across active buckets.
   /// </summary>
   /// <returns>Total count of retained values.</returns>
   size_t count()
   {
      _advance();
      return _totalCount;
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
   /// Reports whether every bucket (including the blending bucket) has received at least
   /// one value, meaning average() now reflects the full configured duration rather than
   /// a partially-filled startup window.
   /// </summary>
   /// <returns>True once the full window has been filled with data.</returns>
   bool isFull()
   {
      _advance();
      return _bucketsWithData >= _numBuckets;
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
