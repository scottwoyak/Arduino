#pragma once

#include "ScatterPlotSeriesBase.h"
#include "TimedAverageHistory.h"
#include "Util.h"
#include <math.h>
#include <new>

class ScatterPlot;

///
/// <summary>
/// A ScatterPlotSeries variant that stores samples as a rolling time-window bin history,
/// backed by TimedAverageHistory (see TimedAverageHistory.h): each add(value) is folded
/// into the currently-open time bin, and samples are aggregated into numBins
/// equal-duration bins spanning the trailing historyMs milliseconds, rotating out the
/// oldest bin as new ones open, instead of being retained in a growing raw array. This
/// bounds memory by numBins (typically the chart's pixel width) regardless of how many
/// samples are added or how long the series lives. The X axis is always locked to
/// [-historyMs, 0], reported by getFixedXRange(). Create instances via
/// ScatterPlot::createTimedSeries() rather than constructing directly.
/// </summary>
///
class TimedScatterPlotSeries : public ScatterPlotSeriesBase
{
   friend class ScatterPlot;

private:
   TimedAverageHistory* _timeHistory = nullptr;
   bool _timeSnapshotDirty = true;
   unsigned long* _timeAgesMsBuffer = nullptr;
   unsigned long _lastRotationCount = 0;

   void _refreshTimeSnapshot()
   {
      if (!_timeSnapshotDirty)
      {
         return;
      }

      _timeHistory->refresh();
      unsigned long rotationCount = _timeHistory->rotationCount();
      unsigned long rotations = rotationCount - _lastRotationCount;
      _lastRotationCount = rotationCount;

      // Each bin's age is k * binDurationMs - a fixed function of its retained index, not
      // of wall-clock time - so a bin's X position only ever changes when it actually
      // rotates out (rotationCount advances), never just from time passing between
      // refreshes. That means the shift-and-reuse optimization IS valid here, the same as
      // for a rolling-count series: every rotation drops the oldest bin off the front, so
      // the moving-average/stddev overlay buffers already computed for the surviving bins
      // can be shifted left and reused rather than recomputed. _y still holds the previous
      // (pre-rotation) snapshot here since it hasn't been overwritten yet, so _y[i] is
      // exactly the i-th oldest dropped value.
      //
      // This is only true once the history is full (filledBins() == numBins()), though:
      // before that, every new bin still just extends the filled range without evicting
      // anything - existing data shifts to *higher* indices, not lower - so treating those
      // early rotations as "drop the oldest slot" would incorrectly subtract still-present
      // data from the cached window sums, permanently corrupting them. So only shift once
      // the ring is actually full; before that, a plain dirty recompute (cheap anyway,
      // since _count is still small) is used instead.
      bool historyFull = _timeHistory->filledBins() >= _timeHistory->numBins();
      if (rotations > 0 && _count > 0 && historyFull)
      {
         size_t shiftCount = (rotations < static_cast<unsigned long>(_count)) ? static_cast<size_t>(rotations) : _count;
         for (size_t i = 0; i < shiftCount; i++)
         {
            _rollOverlaysLeft(_count, _y[i]);
         }
      }
      else if (rotations > 0)
      {
         _movingAverageDirty = true;
         _stdDevDirty = true;
      }

      size_t filled = _timeHistory->snapshot(_y, _timeAgesMsBuffer);
      if (filled > 0)
      {
         _y[0] = NAN;
      }

      for (size_t i = 0; i < filled / 2; i++)
      {
         size_t j = filled - 1 - i;
         float tmpY = _y[i];
         _y[i] = _y[j];
         _y[j] = tmpY;
         unsigned long tmpAge = _timeAgesMsBuffer[i];
         _timeAgesMsBuffer[i] = _timeAgesMsBuffer[j];
         _timeAgesMsBuffer[j] = tmpAge;
      }
      for (size_t i = 0; i < filled; i++)
      {
         _x[i] = -static_cast<float>(_timeAgesMsBuffer[i]);
      }
      _count = filled;

      _xMin = NAN;
      _xMax = NAN;
      _yMin = NAN;
      _yMax = NAN;
      for (size_t i = 0; i < filled; i++)
      {
         float x = _x[i];
         if (!isfinite(_xMin) || (x < _xMin)) _xMin = x;
         if (!isfinite(_xMax) || (x > _xMax)) _xMax = x;

         float y = _y[i];
         if (isfinite(y))
         {
            if (!isfinite(_yMin) || (y < _yMin)) _yMin = y;
            if (!isfinite(_yMax) || (y > _yMax)) _yMax = y;
         }
      }

      _timeSnapshotDirty = false;
      _rangeDirty = false;
      _statsDirty = true;
   }

protected:
   void _refreshStorage() override
   {
      _refreshTimeSnapshot();
   }

   void _clearDerivedState() override
   {
      _timeHistory->reset();
      _timeSnapshotDirty = true;
      _lastRotationCount = 0;
   }

public:
   TimedScatterPlotSeries(unsigned long historyMs, size_t numBins)
      : ScatterPlotSeriesBase(numBins)
   {
      _timeHistory = new (std::nothrow) TimedAverageHistory(historyMs, numBins);
      if (_timeHistory == nullptr)
      {
         Util::setHaltReason("OOM allocating TimedAverageHistory in TimedScatterPlotSeries");
         Util::reset();
         return;
      }

      delete[] _x;
      delete[] _y;
      _x = new (std::nothrow) float[numBins];
      _y = new (std::nothrow) float[numBins];
      _timeAgesMsBuffer = new (std::nothrow) unsigned long[numBins];
      _capacity = numBins;

      if (_x == nullptr || _y == nullptr || _timeAgesMsBuffer == nullptr)
      {
         Util::setHaltReason("OOM allocating time snapshot buffers in TimedScatterPlotSeries");
         Util::reset();
         return;
      }

      _timeSnapshotDirty = true;
   }

   ~TimedScatterPlotSeries() override
   {
      delete _timeHistory;
      delete[] _timeAgesMsBuffer;
   }

   void add(float value) override
   {
      _timeHistory->add(value);
      _timeSnapshotDirty = true;
      _movingAverageDirty = true;
      _stdDevDirty = true;

      // New samples always land in the current (rightmost, x = 0) time bin, so the
      // moving-average/stddev readiness gate in ScatterPlotSeriesBase (which relies on
      // _maxSampleX to know how far real data extends) needs to track that here, mirroring
      // what ScatterPlotSeries::add() does for its own storage modes.
      _maxSampleX = 0.0f;
   }

   bool getFixedXRange(float* outMin, float* outMax) const override
   {
      unsigned long historyMs = _timeHistory->binDurationMs() * static_cast<unsigned long>(_timeHistory->numBins());
      *outMin = -static_cast<float>(historyMs);
      *outMax = 0.0f;
      return true;
   }
};
