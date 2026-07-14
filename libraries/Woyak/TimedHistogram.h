#pragma once

#include <new>
#include <algorithm>
#include "Structs.h"
#include "TimedBin.h"
#include "TimedValues.h"
#include "Util.h"

///
/// <summary>
/// Describes the samples captured for a single histogram render pass.
/// </summary>
///
struct TimedHistogramSnapshot
{
   /// <summary>Smallest sample value captured.</summary>
   float minValue = 0.0f;

   /// <summary>Largest sample value captured.</summary>
   float maxValue = 0.0f;

   /// <summary>Age in seconds of the oldest sample captured, or NAN if not yet computed.</summary>
   float sampleRangeSeconds = NAN;

   /// <summary>Number of samples captured.</summary>
   size_t valueCount = 0;

   /// <summary>Effective number of bins used to render this snapshot.</summary>
   uint16_t renderedBins = 0;

   /// <summary>Largest normalized bin count, as computed by computeNormalizedBins().</summary>
   float maxBinCount = 0.0f;
};

///
/// <summary>
/// Tracks histogram data over a time window using binned counts.
/// </summary>
/// <remarks>
/// This class divides a value range into equally-sized bins and maintains
/// a time-windowed count for each bin using TimedBin.
/// Rendering is handled by TimedHistogramPlot.
/// </remarks>
///
template<unsigned long (*TimeFunc)() = millis>
class TimedHistogramBase
{
private:
   TimedBinBase<TimeFunc>** _bins;
   uint16_t _numBins;
   RangeF _range;
   unsigned long _durationMs = 1;

   // Computed normalized bin values
   float* _binValues = nullptr;
   float _minBinWidth = 0.0f;

   // Snapshot buffer for compute operations
   float* _sampleValues = nullptr;
   unsigned long* _sampleAgesMs = nullptr;
   size_t _sampleCapacity = 0;

   ///
   /// <summary>
   /// Lazily allocates the _binValues buffer used by computeNormalizedBins(), sized to
   /// _numBins. A no-op if already allocated.
   /// </summary>
   /// <returns>True if the buffer is allocated (either just now or previously).</returns>
   ///
   bool _allocateComputeBuffers()
   {
      if (_binValues != nullptr)
      {
         return true;
      }

      _binValues = new (std::nothrow) float[_numBins];
      if (_binValues == nullptr)
      {
         Util::setHaltReason("OOM allocating binValues in TimedHistogram");
         Util::reset();
         return false;
      }

      for (uint16_t i = 0; i < _numBins; i++)
      {
         _binValues[i] = 0.0f;
      }

      return true;
   }

   ///
   /// <summary>
   /// Grows the sample value/age scratch buffers to at least the requested capacity,
   /// reused across capture() calls instead of reallocating every time.
   /// </summary>
   /// <param name="capacity">Minimum required capacity.</param>
   /// <returns>True if the buffers are large enough (either just grown or already sufficient).</returns>
   ///
   bool _ensureSampleBuffer(size_t capacity)
   {
      if (capacity <= _sampleCapacity)
      {
         return true;
      }

      float* nextValues = new (std::nothrow) float[capacity];
      unsigned long* nextAges = new (std::nothrow) unsigned long[capacity];
      if (nextValues == nullptr || nextAges == nullptr)
      {
         Util::setHaltReason("OOM allocating sample buffers in TimedHistogram");
         Util::reset();
         return false;
      }

      delete[] _sampleValues;
      delete[] _sampleAgesMs;
      _sampleValues = nextValues;
      _sampleAgesMs = nextAges;
      _sampleCapacity = capacity;
      return true;
   }

   ///
   /// <summary>
   /// Computes how many of the configured bins should actually be used to represent the
   /// given value range, capped so no bin is narrower than minBinWidth.
   /// </summary>
   /// <param name="range">Value range (max - min) being rendered.</param>
   /// <returns>Effective number of bins to use, between 1 and _numBins.</returns>
   ///
   uint16_t _effectiveBinCount(float range) const
   {
      if (_numBins == 0)
      {
         return 0;
      }

      if (_minBinWidth <= 0.0f || range <= 0.0f)
      {
         return _numBins;
      }

      uint16_t maxBinsByResolution = static_cast<uint16_t>(floor(range / _minBinWidth + 0.0001f));
      if (maxBinsByResolution < 1)
      {
         maxBinsByResolution = 1;
      }

      return std::min(_numBins, maxBinsByResolution);
   }

public:
   ///
   /// <summary>
   /// Creates a histogram for dynamic (sliding-scale) data capture.
   /// </summary>
   /// <param name="numBins">Number of bins to divide the value range into.</param>
   /// <param name="ms">Time window in milliseconds spanned by each bin's retained count.</param>
   /// <param name="minBinWidth">Minimum value width per bin (0.0 for no minimum, default 0.0).</param>
   ///
   TimedHistogramBase(uint16_t numBins, unsigned long ms, float minBinWidth = 0.0f)
      : TimedHistogramBase(RangeF{ 0.0f, 1.0f }, numBins, ms, minBinWidth)
   {}

   ///
   /// <summary>
   /// Creates a histogram with the specified range, number of bins, and time window.
   /// </summary>
   /// <param name="range">Value range divided into numBins equal-sized bins.</param>
   /// <param name="numBins">Number of bins to divide the value range into.</param>
   /// <param name="ms">Time window in milliseconds spanned by each bin's retained count.</param>
   /// <param name="minBinWidth">Minimum value width per bin (0.0 for no minimum, default 0.0).</param>
   ///
   TimedHistogramBase(RangeF range, uint16_t numBins, unsigned long ms, float minBinWidth = 0.0f)
   {
      _range = range;
      _numBins = numBins;
      _durationMs = std::max(1UL, ms);
      _minBinWidth = minBinWidth;
      _bins = new (std::nothrow) TimedBinBase<TimeFunc>*[numBins];

      if (_bins == nullptr)
      {
         Util::setHaltReason("OOM allocating bins in TimedHistogram");
         Util::reset();
         return;
      }

      for (uint16_t i = 0; i < numBins; i++)
      {
         _bins[i] = new (std::nothrow) TimedBinBase<TimeFunc>(_durationMs);
         if (_bins[i] == nullptr)
         {
            Util::setHaltReason("OOM allocating TimedBinBase in TimedHistogram");
            Util::reset();
            return;
         }
      }
   }

   ///
   /// <summary>
   /// Releases all bin and compute resources.
   /// </summary>
   ///
   ~TimedHistogramBase()
   {
      for (uint16_t i = 0; i < _numBins; i++)
      {
         delete _bins[i];
      }
      delete[] _bins;
      delete[] _binValues;
      delete[] _sampleValues;
      delete[] _sampleAgesMs;
   }

   ///
   /// <summary>
   /// Gets the number of bins.
   /// </summary>
   /// <returns>The number of bins.</returns>
   ///
   uint16_t getNumBinsX() const
   {
      return _numBins;
   }

   ///
   /// <summary>
   /// Records a value in the appropriate bin.
   /// </summary>
   /// <param name="value">The value to record. NaN values are ignored.</param>
   ///
   void set(float value)
   {
      if (isnan(value))
      {
         return;
      }

      int bin = static_cast<int>(floor((value - _range.min) / (_range.max - _range.min) * _numBins));
      bin = std::max(0, std::min(bin, static_cast<int>(_numBins - 1)));

      _bins[bin]->add();
   }

   ///
   /// <summary>
   /// Gets normalized bin counts (0.0 to 1.0) based on the maximum count.
   /// </summary>
   /// <param name="values">Array to populate with normalized bin counts.</param>
   ///
   void get(float* values) const
   {
      float maxValue = 0;
      for (uint16_t i = 0; i < _numBins; i++)
      {
         values[i] = _bins[i]->getCount();
         maxValue = std::max(maxValue, values[i]);
      }

      if (maxValue > 0)
      {
         for (uint16_t i = 0; i < _numBins; i++)
         {
            values[i] /= maxValue;
         }
      }
   }

   ///
   /// <summary>
   /// Gets the range of bins containing data.
   /// </summary>
   /// <returns>A range with the first and last bin indices that have data, or the full range if empty.</returns>
   ///
   RangeU16 getCurrentBinsRange() const
   {
      bool found = false;
      uint16_t firstBin = 0;
      uint16_t lastBin = 0;

      for (uint16_t i = 0; i < _numBins; i++)
      {
         if (_bins[i]->getCount() > 0)
         {
            if (!found)
            {
               firstBin = i;
               found = true;
            }
            lastBin = i;
         }
      }

      if (!found)
      {
         firstBin = 0;
         lastBin = _numBins - 1;
      }

      return RangeU16{ firstBin, lastBin };
   }

   ///
   /// <summary>
   /// Gets the value range corresponding to bins with data.
   /// </summary>
   /// <returns>A range with the min/max values represented by the current bin range.</returns>
   ///
   RangeF getCurrentValuesRange() const
   {
      RangeU16 bins = getCurrentBinsRange();
      float span = (_range.max - _range.min) / _numBins;
      float min = _range.min + (static_cast<float>(bins.min) / _numBins) * (_range.max - _range.min);
      float max = _range.min + (static_cast<float>(bins.max) / _numBins) * (_range.max - _range.min) + span;
      return RangeF{ min, max };
   }

   ///
   /// <summary>
   /// Gets the value range for a specific bin.
   /// </summary>
   /// <param name="bin">The bin index.</param>
   /// <returns>A range with the min/max values for the specified bin.</returns>
   ///
   RangeF getBinRange(uint16_t bin) const
   {
      float span = (_range.max - _range.min) / _numBins;
      float min = _range.min + (static_cast<float>(bin) / _numBins) * (_range.max - _range.min);
      float max = min + span;
      return RangeF{ min, max };
   }

   ///
   /// <summary>
   /// Gets the total number of samples currently retained across all bins.
   /// </summary>
   /// <returns>Total number of samples across all bins.</returns>
   ///
   size_t count() const
   {
      size_t total = 0;
      for (uint16_t i = 0; i < _numBins; i++)
      {
         total += _bins[i]->getCount();
      }
      return total;
   }

   ///
   /// <summary>
   /// Resets all tracked bin data and cached compute state.
   /// </summary>
   ///
   void reset()
   {
      for (uint16_t i = 0; i < _numBins; i++)
      {
         _bins[i]->reset();
      }

      if (_binValues != nullptr)
      {
         for (uint16_t i = 0; i < _numBins; i++)
         {
            _binValues[i] = 0.0f;
         }
      }
   }

   ///
   /// <summary>
   /// Takes a snapshot of the given samples, computing the value range, sample count, and
   /// effective bin count needed to render this histogram, without touching the bins
   /// themselves. Pair with computeNormalizedBins() to populate per-bin values.
   /// </summary>
   /// <param name="samples">Source of the raw sample values/ages to snapshot.</param>
   /// <returns>A snapshot describing the captured samples.</returns>
   ///
   TimedHistogramSnapshot capture(TimedValuesBase<float, TimeFunc>& samples)
   {
      TimedHistogramSnapshot snapshot;

      if (!_ensureSampleBuffer(samples.size()))
      {
         return snapshot;
      }

      snapshot.valueCount = samples.snapshot(_sampleValues, _sampleAgesMs, _sampleCapacity);
      if (snapshot.valueCount == 0)
      {
         snapshot.renderedBins = _effectiveBinCount(0.0f);
         return snapshot;
      }

      snapshot.minValue = _sampleValues[0];
      snapshot.maxValue = snapshot.minValue;
      for (size_t i = 1; i < snapshot.valueCount; i++)
      {
         snapshot.minValue = std::min(snapshot.minValue, _sampleValues[i]);
         snapshot.maxValue = std::max(snapshot.maxValue, _sampleValues[i]);
      }

      float range = snapshot.maxValue - snapshot.minValue;
      if (range <= 0.0f)
      {
         range = 1.0f;
      }

      snapshot.renderedBins = _effectiveBinCount(range);

      unsigned long maxAgeMs = 0;
      for (size_t i = 0; i < snapshot.valueCount; i++)
      {
         maxAgeMs = std::max(maxAgeMs, _sampleAgesMs[i]);
      }
      snapshot.sampleRangeSeconds = maxAgeMs / 1000.0f;

      return snapshot;
   }

   ///
   /// <summary>
   /// Bins the samples captured by the most recent capture() call into snapshot.renderedBins
   /// normalized (0.0 to 1.0) counts, readable afterward via normalizedBinValue().
   /// </summary>
   /// <param name="snapshot">Snapshot previously returned by capture(); receives the computed maxBinCount.</param>
   /// <returns>True if the bin buffer was allocated successfully.</returns>
   ///
   bool computeNormalizedBins(TimedHistogramSnapshot& snapshot)
   {
      if (!_allocateComputeBuffers())
      {
         return false;
      }

      for (uint16_t i = 0; i < _numBins; i++)
      {
         _binValues[i] = 0.0f;
      }

      if (snapshot.valueCount == 0)
      {
         snapshot.maxBinCount = 0.0f;
         return true;
      }

      float range = snapshot.maxValue - snapshot.minValue;
      if (range <= 0.0f)
      {
         range = 1.0f;
      }

      for (size_t i = 0; i < snapshot.valueCount; i++)
      {
         int bin = static_cast<int>((_sampleValues[i] - snapshot.minValue) / range * snapshot.renderedBins);
         if (bin < 0) bin = 0;
         if (bin >= snapshot.renderedBins) bin = snapshot.renderedBins - 1;
         _binValues[bin] += 1.0f;
      }

      float maxBinValue = 0.0f;
      for (uint16_t i = 0; i < snapshot.renderedBins; i++)
      {
         maxBinValue = std::max(maxBinValue, _binValues[i]);
      }

      if (maxBinValue > 0.0f)
      {
         for (uint16_t i = 0; i < snapshot.renderedBins; i++)
         {
            _binValues[i] /= maxBinValue;
         }
      }

      snapshot.maxBinCount = maxBinValue;

      return true;
   }

   ///
   /// <summary>
   /// Gets the normalized (0.0 to 1.0) count for a single bin, as computed by the most
   /// recent computeNormalizedBins() call.
   /// </summary>
   /// <param name="bin">Bin index to read.</param>
   /// <returns>Normalized bin count, or 0.0 if the bin index is out of range.</returns>
   ///
   float normalizedBinValue(uint16_t bin) const
   {
      if (_binValues == nullptr || bin >= _numBins)
      {
         return 0.0f;
      }
      return _binValues[bin];
   }
};

using TimedHistogram = TimedHistogramBase<millis>;
