#pragma once

#include <cmath>
#include <new>

#include "Util.h"

///
/// <summary>
/// Aggregates (x, y) samples (as running averages) into a fixed number of equal-width
/// bins spanning a fixed, caller-supplied X range [xMin, xMax]. Unlike
/// TimedAverageHistoryBase (see TimedAverageHistory.h), bins here never rotate or age
/// out - every bin is a permanent slot for its portion of the X range, so this fits
/// callers that know their full X range up front (e.g. a fixed sample count or a fixed
/// index range) rather than an ever-advancing "now". Because the bin count is fixed at
/// construction and never grows, memory is bounded by numBins regardless of how many
/// samples are added, matching the same chart-rendering rationale as
/// TimedAverageHistoryBase: only numBins (e.g. a chart's pixel width) worth of resolution
/// can ever be shown anyway.
/// </summary>
///
class FixedRangeHistory
{
private:
   float* _binSums = nullptr;
   uint32_t* _binCounts = nullptr;
   size_t _numBins = 0;
   float _xMin = 0.0f;
   float _xMax = 1.0f;
   float _binWidth = 1.0f;

public:
   ///
   /// <summary>
   /// Constructs a fixed number of equal-width bins spanning [xMin, xMax].
   /// </summary>
   /// <param name="xMin">Minimum X value covered by the first bin.</param>
   /// <param name="xMax">Maximum X value covered by the last bin.</param>
   /// <param name="numBins">Number of equal-width bins to divide the range into.</param>
   ///
   FixedRangeHistory(float xMin, float xMax, size_t numBins)
      : _numBins((numBins == 0) ? 1 : numBins),
        _xMin(xMin),
        _xMax((xMax > xMin) ? xMax : (xMin + 1.0f))
   {
      _binWidth = (_xMax - _xMin) / static_cast<float>(_numBins);

      _binSums = new (std::nothrow) float[_numBins];
      _binCounts = new (std::nothrow) uint32_t[_numBins];

      if (_binSums == nullptr || _binCounts == nullptr)
      {
         Util::setHaltReason("OOM allocating bins in FixedRangeHistory");
         Util::reset();
         return;
      }

      for (size_t i = 0; i < _numBins; i++)
      {
         _binSums[i] = 0.0f;
         _binCounts[i] = 0;
      }
   }

   FixedRangeHistory(const FixedRangeHistory&) = delete;
   FixedRangeHistory& operator=(const FixedRangeHistory&) = delete;

   ~FixedRangeHistory()
   {
      delete[] _binSums;
      delete[] _binCounts;
   }

   ///
   /// <summary>
   /// Gets the fixed number of bins this object was constructed with.
   /// </summary>
   /// <returns>Number of bins.</returns>
   ///
   size_t numBins() const { return _numBins; }

   ///
   /// <summary>
   /// Gets the locked X range this object was constructed with.
   /// </summary>
   /// <param name="outMin">Receives the minimum X value.</param>
   /// <param name="outMax">Receives the maximum X value.</param>
   ///
   void getXRange(float* outMin, float* outMax) const
   {
      *outMin = _xMin;
      *outMax = _xMax;
   }

   ///
   /// <summary>
   /// Maps an X value to its bin index, clamped to [0, numBins() - 1].
   /// </summary>
   /// <param name="x">X value to map.</param>
   /// <returns>Bin index.</returns>
   ///
   size_t binIndexFor(float x) const
   {
      if (!isfinite(x) || (_binWidth <= 0.0f))
      {
         return 0;
      }

      long index = static_cast<long>((x - _xMin) / _binWidth);
      if (index < 0)
      {
         index = 0;
      }
      else if (index >= static_cast<long>(_numBins))
      {
         index = static_cast<long>(_numBins) - 1;
      }

      return static_cast<size_t>(index);
   }

   ///
   /// <summary>
   /// Adds a new (x, y) sample, accumulating it into the bin whose range contains x.
   /// Halts via Util::setHaltReason()/Util::reset() if x falls outside [xMin, xMax],
   /// since - unlike TimedAverageHistoryBase's rolling time window - this range is fixed
   /// and never expected to be exceeded by a correctly configured caller.
   /// </summary>
   /// <param name="x">X value of the sample.</param>
   /// <param name="y">Y value of the sample.</param>
   ///
   void add(float x, float y)
   {
      if (!isfinite(x) || (x < _xMin) || (x > _xMax))
      {
         Util::setHaltReason("FixedRangeHistory::add() x value outside locked range");
         Util::reset();
         return;
      }

      if (!isfinite(y))
      {
         return;
      }

      size_t index = binIndexFor(x);
      _binSums[index] += y;
      _binCounts[index]++;
   }

   ///
   /// <summary>
   /// Copies the current bin averages into a caller-provided buffer, oldest-first (index 0
   /// is the bin covering xMin). Bins with no samples yet report NAN.
   /// </summary>
   /// <param name="outValues">Receives numBins() bin averages, oldest-first.</param>
   ///
   void snapshot(float* outValues) const
   {
      for (size_t i = 0; i < _numBins; i++)
      {
         outValues[i] = (_binCounts[i] > 0) ? (_binSums[i] / _binCounts[i]) : NAN;
      }
   }

   ///
   /// <summary>
   /// Gets the X value at the center of the given bin index.
   /// </summary>
   /// <param name="index">Bin index.</param>
   /// <returns>Center X value of the bin.</returns>
   ///
   float binCenterX(size_t index) const
   {
      return _xMin + (static_cast<float>(index) + 0.5f) * _binWidth;
   }

   ///
   /// <summary>
   /// Clears every bin back to empty, keeping the same locked X range.
   /// </summary>
   ///
   void reset()
   {
      for (size_t i = 0; i < _numBins; i++)
      {
         _binSums[i] = 0.0f;
         _binCounts[i] = 0;
      }
   }
};
