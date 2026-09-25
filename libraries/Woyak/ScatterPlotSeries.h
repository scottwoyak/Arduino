#pragma once

#include "FixedRangeHistory.h"
#include "ScatterPlotSeriesBase.h"
#include "Util.h"
#include <math.h>
#include <new>

class ScatterPlot;

///
/// <summary>
/// The default ScatterPlotSeries implementation, storing samples added via add(x, y) or
/// the auto-incrementing add(value): a plain growing raw-point array by default, or (once
/// switched into one of the modes below) a fixed-size storage buffer that controls what
/// happens once the series is populated - either mode is chosen once, before any add()
/// calls, and cannot be changed afterward:
///  - Fixed-range bin mode (setFixedXRange(xMin, xMax, numBins)): samples are aggregated
///    into numBins equal-width bin averages spanning [xMin, xMax], backed by
///    FixedRangeHistory, bounding memory regardless of how many samples are added.
///  - Rolling-count mode (setRollingCount(maxSamples)): samples are appended to a
///    fixed-size buffer of exactly maxSamples raw (unaveraged) points; once full, each
///    new sample becomes the last point and the oldest point is dropped, scrolling the
///    data like an oscilloscope trace instead of being averaged away. X is locked to
///    each point's fixed slot index [1, maxSamples], so the axis range never changes.
/// Create instances via ScatterPlot::createSeries() (growing/fixed-range modes) or
/// ScatterPlot::createRollingSeries() (rolling-count mode).
/// </summary>
///
class ScatterPlotSeries : public ScatterPlotSeriesBase
{
   friend class ScatterPlot;

private:
   FixedRangeHistory* _binHistory = nullptr;
   bool _binSnapshotDirty = true;

   bool _isRollingCount = false;
   size_t _rollingCapacity = 0;

   ///
   /// <summary>
   /// Refreshes the cached bin-average snapshot (_x/_y) from the underlying
   /// FixedRangeHistory and recomputes the raw X/Y range, but only if a bin has
   /// changed since the last snapshot (_binSnapshotDirty). Called lazily from
   /// _refreshStorage() before any render-facing accessor reads the series' points.
   /// </summary>
   ///
   void _refreshBinSnapshot()
   {
      if (!_binSnapshotDirty)
      {
         return;
      }

      size_t numBins = _binHistory->numBins();
      _binHistory->snapshot(_y);
      for (size_t i = 0; i < numBins; i++)
      {
         _x[i] = _binHistory->binCenterX(i);
      }
      _count = numBins;

      _xMin = NAN;
      _xMax = NAN;
      _yMin = NAN;
      _yMax = NAN;
      for (size_t i = 0; i < numBins; i++)
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

      _binSnapshotDirty = false;
      _rangeDirty = false;
      _statsDirty = true;
   }

   void _addRolling(float value)
   {
      if (_count < _rollingCapacity)
      {
         _x[_count] = (float)(_count + 1);
         _y[_count] = value;
         _count++;
         _movingAverageDirty = true;
         _stdDevDirty = true;
      }
         else
         {
            // Buffer is full: drop the oldest point (index 0) by shifting the Y values
            // left one slot, then append the new value as the last point. The X values
            // are always the fixed slot positions [0, capacity - 1], so they never need
            // to change - keeping the X axis range constant and avoiding a full redraw
            // (and the resulting flicker) on every add().
            float droppedY = _y[0];
            memmove(_y, _y + 1, (_rollingCapacity - 1) * sizeof(float));
            _y[_rollingCapacity - 1] = value;

            // Preserve already-known moving-average/stddev overlay values by shifting
            // them in lockstep with the raw data instead of recomputing the whole
            // series, so already-displayed history doesn't visibly change as it
            // scrolls off - only the newly exposed trailing slot may need filling in.
            _rollOverlaysLeft(_rollingCapacity, droppedY);
         }

         _statsDirty = true;
         _rangeDirty = true;
      }

protected:
   void _refreshStorage() override
   {
      if (_binHistory != nullptr)
      {
         _refreshBinSnapshot();
      }
   }

   void _clearDerivedState() override
   {
      if (_binHistory != nullptr)
      {
         _binHistory->reset();
         _binSnapshotDirty = true;
      }
   }

public:
   using ScatterPlotSeriesBase::setFixedXRange;
   explicit ScatterPlotSeries(size_t capacity = 10)
      : ScatterPlotSeriesBase(capacity)
   {
   }

   ~ScatterPlotSeries() override
   {
      delete _binHistory;
   }

   ///
   /// <summary>
   /// Adds an explicit (x, y) point. In fixed-range bin mode, the point is aggregated
   /// into its corresponding bin average via the underlying FixedRangeHistory instead of
   /// being stored directly. Also advances the auto-incrementing X cursor used by
   /// add(value) to x + 1.0.
   /// </summary>
   /// <param name="x">X coordinate of the point.</param>
   /// <param name="y">Y coordinate (value) of the point.</param>
   ///
   void add(float x, float y)
   {
      _nextX = x + 1.0f;

      if (_binHistory != nullptr)
      {
         _binHistory->add(x, y);
         _binSnapshotDirty = true;
         _movingAverageDirty = true;
         _stdDevDirty = true;
         if (isfinite(x) && (!isfinite(_maxSampleX) || (x > _maxSampleX)))
         {
            _maxSampleX = x;
         }
         return;
      }

      _appendRaw(x, y);
   }

   ///
   /// <summary>
   /// Adds a sample using an auto-incrementing X value. In rolling-count mode, delegates
   /// to _addRolling() to append into the fixed-size scrolling buffer; otherwise adds
   /// (nextX, value) via the two-argument add(), advancing the X cursor.
   /// </summary>
   /// <param name="value">Y value (sample) to add.</param>
   ///
   void add(float value) override
   {
      if (_isRollingCount)
      {
         _addRolling(value);
         return;
      }
      add(_nextX, value);
   }

   ///
   /// <summary>
   /// Switches this series into fixed-range bin mode, aggregating all future samples
   /// into numBins equal-width bin averages spanning [xMin, xMax] via a FixedRangeHistory,
   /// bounding memory regardless of how many samples are added. Must be called before any
   /// add() calls and before setRollingCount(); has no effect if already in bin mode or if
   /// numBins is 0.
   /// </summary>
   /// <param name="xMin">Inclusive lower bound of the fixed X range.</param>
   /// <param name="xMax">Inclusive upper bound of the fixed X range.</param>
   /// <param name="numBins">Number of equal-width bins spanning [xMin, xMax].</param>
   ///
   void setFixedXRange(float xMin, float xMax, size_t numBins)
   {
      setFixedXRange(xMin, xMax);

      if (_binHistory != nullptr || numBins == 0)
      {
         return;
      }

      _binHistory = new (std::nothrow) FixedRangeHistory(xMin, xMax, numBins);
      if (_binHistory == nullptr)
      {
         Util::reset(0.0f, "OOM allocating FixedRangeHistory in ScatterPlotSeries");
         return;
      }

      delete[] _x;
      delete[] _y;
      _x = new (std::nothrow) float[numBins];
      _y = new (std::nothrow) float[numBins];
      _capacity = numBins;

      if (_x == nullptr || _y == nullptr)
      {
         Util::reset(0.0f, "OOM allocating bin snapshot buffers in ScatterPlotSeries");
         return;
      }

      _binSnapshotDirty = true;
   }

   ///
   /// <summary>
   /// Switches this series into rolling-count mode, appending samples to a fixed-size
   /// buffer of exactly maxSamples raw (unaveraged) points. Once full, each new sample
   /// becomes the last point and the oldest point is dropped, scrolling the data like an
   /// oscilloscope trace. X is locked to each point's fixed slot index [1, maxSamples],
   /// so the axis range never changes. Must be called before any add() calls; has no
   /// effect if already in rolling-count mode or if maxSamples is 0.
   /// </summary>
   /// <param name="maxSamples">Fixed number of raw points to retain.</param>
   ///
   void setRollingCount(size_t maxSamples)
   {
      if (_isRollingCount || maxSamples == 0)
      {
         return;
      }

      delete[] _x;
      delete[] _y;
      _x = new (std::nothrow) float[maxSamples];
      _y = new (std::nothrow) float[maxSamples];
      _capacity = maxSamples;

      if (_x == nullptr || _y == nullptr)
      {
         Util::reset(0.0f, "OOM allocating rolling buffers in ScatterPlotSeries");
         return;
      }

      _isRollingCount = true;
      _rollingCapacity = maxSamples;
      _count = 0;
      setFixedXRange(1.0f, (float)maxSamples);
   }
};
