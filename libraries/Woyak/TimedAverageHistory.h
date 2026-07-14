#pragma once

#include <cmath>
#include <new>

#include "Util.h"

///
/// <summary>
/// Aggregates values (as running averages) into a fixed number of equal-duration time
/// bins spanning a fixed total duration, rotating out the oldest bin and opening a new
/// one as time advances. Unlike TimedAverage (see TimedAverage.h), which blends every
/// retained value down into a single rolling scalar, this class retains a distinct
/// average per bin, so the retained history can be read back as a time series via
/// snapshot(). Because the bin count is fixed at construction and never grows, memory is
/// bounded by numBins regardless of how long the object lives, how often add() is called,
/// or how far the total duration spans - making it well suited for cases like chart
/// rendering, where only numBins (e.g. a chart's pixel width) worth of resolution can ever
/// be shown anyway.
/// </summary>
///
/// <typeparam name="TimeFunc">Function pointer returning current time in milliseconds.</typeparam>
///
template<unsigned long (*TimeFunc)() = millis>
class TimedAverageHistoryBase
{
private:
   float* _binSums = nullptr;
   uint32_t* _binCounts = nullptr;
   size_t _numBins = 0;
   size_t _headIndex = 0;
   size_t _filledBins = 0;
   unsigned long _binStartMs = 0;
   unsigned long _binDurationMs = 1;
   bool _hasStarted = false;
   unsigned long _rotationCount = 0;

   ///
   /// <summary>
   /// Rotates out expired bins as time advances, clearing each newly-opened bin.
   /// </summary>
   /// <param name="nowMs">Current time in milliseconds.</param>
   ///
   void _advanceIfNeeded(unsigned long nowMs)
   {
      if (!_hasStarted)
      {
         _hasStarted = true;
         _binStartMs = nowMs;
         return;
      }

      while ((nowMs - _binStartMs) >= _binDurationMs)
      {
         _binStartMs += _binDurationMs;
         _headIndex = (_headIndex + 1) % _numBins;
         _binSums[_headIndex] = 0.0f;
         _binCounts[_headIndex] = 0;
         _rotationCount++;
         if (_filledBins < _numBins)
         {
            _filledBins++;
         }
      }
   }

public:
   ///
   /// <summary>
   /// Constructs a fixed-size set of time bins spanning durationMs, divided into numBins
   /// equal-duration bins.
   /// </summary>
   /// <param name="durationMs">Total duration in milliseconds spanned by all bins.</param>
   /// <param name="numBins">Number of equal-duration bins to divide the duration into.</param>
   ///
   TimedAverageHistoryBase(unsigned long durationMs, size_t numBins)
      : _numBins((numBins == 0) ? 1 : numBins),
        _binDurationMs(max(1UL, durationMs / ((numBins == 0) ? 1 : numBins)))
   {
      _binSums = new (std::nothrow) float[_numBins];
      _binCounts = new (std::nothrow) uint32_t[_numBins];

      if (_binSums == nullptr || _binCounts == nullptr)
      {
         Util::setHaltReason("OOM allocating bins in TimedAverageHistoryBase");
         Util::reset();
         return;
      }

      for (size_t i = 0; i < _numBins; i++)
      {
         _binSums[i] = 0.0f;
         _binCounts[i] = 0;
      }
   }

   TimedAverageHistoryBase(const TimedAverageHistoryBase&) = delete;
   TimedAverageHistoryBase& operator=(const TimedAverageHistoryBase&) = delete;

   ~TimedAverageHistoryBase()
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
   /// Gets the number of times a bin has rotated out (i.e. the read head has advanced)
   /// since construction or the last reset(). Callers that cache a snapshot's index-to-
   /// pixel mapping across frames can compare this against a previously saved value to
   /// detect that every retained bin has shifted position and any cached per-index
   /// state (e.g. previously drawn pixel positions) is now stale.
   /// </summary>
   /// <returns>Total number of bin rotations so far.</returns>
   ///
   unsigned long rotationCount() const { return _rotationCount; }

   ///
   /// <summary>
   /// Gets the duration spanned by each bin, in milliseconds.
   /// </summary>
   /// <returns>Bin duration in milliseconds.</returns>
   ///
   unsigned long binDurationMs() const { return _binDurationMs; }

   ///
   /// <summary>
   /// Gets the number of bins currently holding data (as of the last add()/snapshot()
   /// call), capped at numBins() once every bin has been filled at least once.
   /// </summary>
   /// <returns>Number of filled bins.</returns>
   ///
   size_t filledBins() const { return _filledBins; }

   ///
   /// <summary>
   /// Adds a new value, accumulating it into the currently-open time bin (rotating out
   /// expired bins first as needed).
   /// </summary>
   /// <param name="value">Value to add.</param>
   ///
   void add(float value)
   {
      _advanceIfNeeded(TimeFunc());
      _binSums[_headIndex] += value;
      _binCounts[_headIndex]++;
   }

   ///
   /// <summary>
   /// Rotates out any bins that have expired since the last add()/snapshot() call,
   /// without adding a new value. Useful to call before reading filledBins()/snapshot()
   /// on a series that may not have received new values recently.
   /// </summary>
   ///
   void refresh()
   {
      _advanceIfNeeded(TimeFunc());
   }

   ///
   /// <summary>
   /// Copies the current bin averages into caller-provided buffers, newest-first (index 0
   /// is the currently-open/most-recent bin), along with each bin's age in milliseconds
   /// (index * binDurationMs). Rotates out any expired bins first. Bins with no samples
   /// yet report NAN.
   /// </summary>
   /// <param name="outValues">Receives up to filledBins() bin averages, newest-first.</param>
   /// <param name="outAgesMs">Receives each bin's age in milliseconds, parallel to outValues.</param>
   /// <returns>Number of bins written to outValues/outAgesMs.</returns>
   ///
   size_t snapshot(float* outValues, unsigned long* outAgesMs)
   {
      refresh();

      for (size_t k = 0; k < _filledBins; k++)
      {
         size_t idx = (_headIndex + _numBins - k) % _numBins;
         outValues[k] = (_binCounts[idx] > 0) ? (_binSums[idx] / _binCounts[idx]) : NAN;
         outAgesMs[k] = static_cast<unsigned long>(k) * _binDurationMs;
      }

      return _filledBins;
   }

   ///
   /// <summary>
   /// Clears every bin and restarts timing from now.
   /// </summary>
   ///
   void reset()
   {
      for (size_t i = 0; i < _numBins; i++)
      {
         _binSums[i] = 0.0f;
         _binCounts[i] = 0;
      }
      _headIndex = 0;
      _filledBins = 0;
      _hasStarted = false;
      _rotationCount = 0;
   }
};

using TimedAverageHistory = TimedAverageHistoryBase<millis>;
