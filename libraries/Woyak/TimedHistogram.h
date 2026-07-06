#pragma once

#include <new>
#include <algorithm>
#include "Structs.h"
#include "TimedBin.h"
#include "TimedValues.h"
#include "Util.h"

struct TimedHistogramSnapshot
{
   float minValue = 0.0f;
   float maxValue = 0.0f;
   float sampleRangeSeconds = NAN;
   size_t valueCount = 0;
   uint renderedBins = 0;
};

/// <summary>
/// Tracks histogram data over a time window using binned counts.
/// </summary>
/// <remarks>
/// This class divides a value range into equally-sized bins and maintains
/// a time-windowed count for each bin using TimedBin.
/// Rendering is handled by TimedHistogramPlot.
/// </remarks>
template<unsigned long (*TimeFunc)() = millis>
class TimedHistogramBase
{
private:
   TimedBinBase<TimeFunc>** _bins;
   uint _numBins;
   RangeF _range;
   unsigned long _durationMs = 1;

   // Computed normalized bin values
   float* _binValues = nullptr;
   float _minBinWidth = 0.0f;

   // Snapshot buffer for compute operations
   float* _sampleValues = nullptr;
   unsigned long* _sampleAgesMs = nullptr;
   size_t _sampleCapacity = 0;

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

      for (uint i = 0; i < _numBins; i++)
      {
         _binValues[i] = 0.0f;
      }

      return true;
   }

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

   uint _effectiveBinCount(float range) const
   {
      if (_numBins == 0)
      {
         return 0;
      }

      if (_minBinWidth <= 0.0f || range <= 0.0f)
      {
         return _numBins;
      }

      uint maxBinsByResolution = static_cast<uint>(floor(range / _minBinWidth + 0.0001f));
      if (maxBinsByResolution < 1)
      {
         maxBinsByResolution = 1;
      }

      return std::min(_numBins, maxBinsByResolution);
   }

public:
   /// <summary>
   /// Creates a histogram for dynamic (sliding-scale) data capture.
   /// </summary>
   TimedHistogramBase(uint numBins, uint ms, float minBinWidth = 0.0f)
      : TimedHistogramBase(RangeF{ 0.0f, 1.0f }, numBins, ms, minBinWidth)
   {}

   /// <summary>
   /// Creates a histogram with the specified range, number of bins, and time window.
   /// </summary>
   TimedHistogramBase(RangeF range, uint numBins, uint ms, float minBinWidth = 0.0f)
   {
      _range = range;
      _numBins = numBins;
      _durationMs = std::max(1UL, static_cast<unsigned long>(ms));
      _minBinWidth = minBinWidth;
      _bins = new (std::nothrow) TimedBinBase<TimeFunc>*[numBins];

      if (_bins == nullptr)
      {
         Util::setHaltReason("OOM allocating bins in TimedHistogram");
         Util::reset();
         return;
      }

      for (uint i = 0; i < numBins; i++)
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

   /// <summary>
   /// Releases all bin and compute resources.
   /// </summary>
   ~TimedHistogramBase()
   {
      for (uint i = 0; i < _numBins; i++)
      {
         delete _bins[i];
      }
      delete[] _bins;
      delete[] _binValues;
      delete[] _sampleValues;
      delete[] _sampleAgesMs;
   }

   /// <summary>
   /// Gets the number of bins.
   /// </summary>
   /// <returns>The number of bins.</returns>
   uint getNumBinsX() const
   {
      return _numBins;
   }

   /// <summary>
   /// Records a value in the appropriate bin.
   /// </summary>
   /// <param name="value">The value to record. NaN values are ignored.</param>
   void set(float value)
   {
      if (isnan(value))
      {
         return;
      }

      int bin = static_cast<int>(floor((value - _range.min) / (_range.max - _range.min) * _numBins));
      bin = std::max(0, std::min(bin, static_cast<int>(_numBins - 1)));

      _bins[static_cast<uint>(bin)]->add();
   }

   /// <summary>
   /// Gets normalized bin counts (0.0 to 1.0) based on the maximum count.
   /// </summary>
   /// <param name="values">Array to populate with normalized bin counts.</param>
   void get(float* values) const
   {
      float maxValue = 0;
      for (uint i = 0; i < _numBins; i++)
      {
         values[i] = _bins[i]->getCount();
         maxValue = std::max(maxValue, values[i]);
      }

      if (maxValue > 0)
      {
         for (uint i = 0; i < _numBins; i++)
         {
            values[i] /= maxValue;
         }
      }
   }

   /// <summary>
   /// Gets the range of bins containing data.
   /// </summary>
   /// <returns>A range with the first and last bin indices that have data, or the full range if empty.</returns>
   RangeU16 getCurrentBinsRange() const
   {
      int firstBin = -1;
      int lastBin = -1;

      for (uint i = 0; i < _numBins; i++)
      {
         if (_bins[i]->getCount() > 0)
         {
            if (firstBin == -1)
            {
               firstBin = i;
            }
            lastBin = i;
         }
      }

      if (firstBin == -1)
      {
         firstBin = 0;
         lastBin = _numBins - 1;
      }

      return RangeU16{ static_cast<uint16_t>(firstBin), static_cast<uint16_t>(lastBin) };
   }

   /// <summary>
   /// Gets the value range corresponding to bins with data.
   /// </summary>
   /// <returns>A range with the min/max values represented by the current bin range.</returns>
   RangeF getCurrentValuesRange() const
   {
      RangeU16 bins = getCurrentBinsRange();
      float span = (_range.max - _range.min) / _numBins;
      float min = _range.min + ((float)bins.min / _numBins) * (_range.max - _range.min);
      float max = _range.min + ((float)bins.max / _numBins) * (_range.max - _range.min) + span;
      return RangeF{ min, max };
   }

   /// <summary>
   /// Gets the value range for a specific bin.
   /// </summary>
   /// <param name="bin">The bin index.</param>
   /// <returns>A range with the min/max values for the specified bin.</returns>
   RangeF getBinRange(uint16_t bin) const
   {
      float span = (_range.max - _range.min) / _numBins;
      float min = _range.min + ((float)bin / _numBins) * (_range.max - _range.min);
      float max = min + span;
      return RangeF{ min, max };
   }

   /// <summary>
   /// Gets the total number of samples currently retained across all bins.
   /// </summary>
   size_t count() const
   {
      size_t total = 0;
      for (uint i = 0; i < _numBins; i++)
      {
         total += _bins[i]->getCount();
      }
      return total;
   }

   /// <summary>
   /// Resets all tracked bin data and cached compute state.
   /// </summary>
   void reset()
   {
      for (uint i = 0; i < _numBins; i++)
      {
         _bins[i]->reset();
      }

      if (_binValues != nullptr)
      {
         for (uint i = 0; i < _numBins; i++)
         {
            _binValues[i] = 0.0f;
         }
      }
   }

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
      snapshot.sampleRangeSeconds = static_cast<float>(maxAgeMs) / 1000.0f;

      return snapshot;
   }

    bool computeNormalizedBins(const TimedHistogramSnapshot& snapshot)
    {
       if (!_allocateComputeBuffers())
       {
          return false;
       }

       for (uint i = 0; i < _numBins; i++)
       {
          _binValues[i] = 0.0f;
       }

      if (snapshot.valueCount == 0)
      {
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
         if (bin >= static_cast<int>(snapshot.renderedBins)) bin = static_cast<int>(snapshot.renderedBins) - 1;
         _binValues[bin] += 1.0f;
      }

      float maxBinValue = 0.0f;
      for (uint i = 0; i < snapshot.renderedBins; i++)
      {
         maxBinValue = std::max(maxBinValue, _binValues[i]);
      }

      if (maxBinValue > 0.0f)
      {
         for (uint i = 0; i < snapshot.renderedBins; i++)
         {
            _binValues[i] /= maxBinValue;
         }
      }

      return true;
   }

   float normalizedBinValue(uint bin) const
   {
      if (_binValues == nullptr || bin >= _numBins)
      {
         return 0.0f;
      }
      return _binValues[bin];
   }

};

using TimedHistogram = TimedHistogramBase<millis>;
