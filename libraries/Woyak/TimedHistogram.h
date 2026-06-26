#pragma once

#include "TimedStats.h"

/// <summary>
/// Tracks histogram data over a time window using binned statistics.
/// </summary>
/// <remarks>
/// This class divides a value range into equally-sized bins and maintains
/// time-windowed statistics for each bin using TimedStats.
/// </remarks>
template<unsigned long (*TimeFunc)() = millis>
class TimedHistogramBase
{
private:
   TimedStatsBase<TimeFunc>** _bins;
   uint _numBins;
   RangeF _range;

public:
   /// <summary>
   /// Creates a histogram with the specified range, number of bins, and time window.
   /// </summary>
   /// <param name="range">The min/max value range to histogram.</param>
   /// <param name="numBins">The number of bins to divide the range into.</param>
   /// <param name="ms">The time window duration in milliseconds.</param>
   TimedHistogramBase(RangeF range, uint numBins, uint ms)
   {
      _range = range;
      _numBins = numBins;
      _bins = new TimedStatsBase<TimeFunc>*[numBins];

      for (uint i = 0; i < numBins; i++)
      {
         _bins[i] = new TimedStatsBase<TimeFunc>(ms);
      }
   }

   /// <summary>
   /// Releases all bin resources.
   /// </summary>
   ~TimedHistogramBase()
   {
      for (uint i = 0; i < _numBins; i++)
      {
         delete _bins[i];
      }
      delete[] _bins;
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
   /// Gets the minimum value across all bins.
   /// </summary>
   /// <returns>The minimum value, or NaN if no values exist.</returns>
   float min() const
   {
      float value = __FLT_MAX__;
      for (uint i = 0; i < _numBins; i++)
      {
         value = std::min(value, _bins[i]->min());
      }

      return value;
   }

   /// <summary>
   /// Gets the maximum value across all bins.
   /// </summary>
   /// <returns>The maximum value, or NaN if no values exist.</returns>
   float max() const
   {
      float value = -__FLT_MAX__;
      for (uint i = 0; i < _numBins; i++)
      {
         value = std::max(value, _bins[i]->max());
      }

      return value;
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

      uint bin = floor((value - _range.min) / (_range.max - _range.min) * _numBins);

      bin = std::min(bin, _numBins - 1);
      bin = std::max(bin, 0u);

      _bins[bin]->set(value);
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
         values[i] = _bins[i]->count();
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
         if (firstBin == -1 && _bins[i]->count() > 0)
         {
            firstBin = i;
         }

         if (_bins[i]->count() > 0)
         {
            lastBin = i;
         }
      }

      if (firstBin == -1)
      {
         firstBin = 0;
         lastBin = _numBins - 1;
      }

      return RangeU16(firstBin, lastBin);
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
      return RangeF(min, max);
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
      return RangeF(min, max);
   }
};

using TimedHistogram = TimedHistogramBase<millis>;
