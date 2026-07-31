#pragma once

#include "ScatterPlotSeriesBase.h"
#include "Util.h"
#include <math.h>
#include <new>

class ScatterPlot;

///
/// <summary>
/// A ScatterPlotSeries variant that stores every sample as a raw, unbinned (x, y) point -
/// unlike TimedScatterPlotSeries, which aggregates samples into a fixed number of
/// time-duration bins - so no averaging/overhead is incurred regardless of how fast add()
/// is called. Each add(value) timestamps the sample with millis() as its X coordinate.
/// Call updateWindow(nowMs) once per frame, before the owning ScatterPlot::draw(), to evict
/// points older than historyMs and to lock the X axis to exactly [nowMs - historyMs, nowMs]
/// - decoupling the visible window from whether a new sample happened to arrive that
/// frame, so the plot can stay synchronized with the wall clock. Create instances via
/// ScatterPlot::createTimeWindowSeries() rather than constructing directly.
/// </summary>
///
class TimeWindowScatterPlotSeries : public ScatterPlotSeriesBase
{
   friend class ScatterPlot;

private:
   unsigned long _historyMs;

public:
   ///
   /// <summary>
   /// Creates a time-window series retaining samples for historyMs milliseconds.
   /// </summary>
   /// <param name="historyMs">Duration, in milliseconds, of the rolling window.</param>
   /// <param name="initialCapacity">Initial capacity for the raw point storage, grown automatically if exceeded.</param>
   ///
   explicit TimeWindowScatterPlotSeries(unsigned long historyMs, size_t initialCapacity = 64)
      : ScatterPlotSeriesBase(initialCapacity), _historyMs(historyMs)
   {
   }

   ///
   /// <summary>
   /// Adds a sample timestamped with the current millis() value.
   /// </summary>
   /// <param name="value">Y value (sample) to add.</param>
   ///
   void add(float value) override
   {
      _appendRaw(static_cast<float>(millis()), value);
   }

   ///
   /// <summary>
   /// Evicts every point older than historyMs relative to nowMs, then locks this series'
   /// reported X range to [nowMs - historyMs, nowMs] via setFixedXRange(). Call once per
   /// frame, before the owning ScatterPlot::draw(), so the visible window advances in
   /// lockstep with the wall clock even on frames where no new sample was added.
   /// </summary>
   /// <param name="nowMs">Current time, in milliseconds (typically millis()).</param>
   ///
   void updateWindow(unsigned long nowMs)
   {
      unsigned long cutoff = (nowMs > _historyMs) ? (nowMs - _historyMs) : 0;

      size_t evictCount = 0;
      while (evictCount < _count && _x[evictCount] < static_cast<float>(cutoff))
      {
         evictCount++;
      }

      if (evictCount > 0)
      {
         size_t remaining = _count - evictCount;
         memmove(_x, _x + evictCount, remaining * sizeof(float));
         memmove(_y, _y + evictCount, remaining * sizeof(float));
         _count = remaining;

         _xMin = NAN;
         _xMax = NAN;
         _yMin = NAN;
         _yMax = NAN;
         _rangeDirty = true;
         _statsDirty = true;

         // A varying number of points can be evicted per call (unlike the fixed one-slot
         // shift used by rolling-count/time-bin series), so the incremental moving-average/
         // stddev scan state is simply reset here rather than shifted; the next
         // prepareForRender() will do a full (but cheap, since data is small) rescan.
         _movingAverageReadyCount = 0;
         _movingAverageDirty = true;
         _movingAverageLo = 0;
         _movingAverageHi = 0;
         _movingAverageSum = 0.0f;
         _movingAverageFiniteCount = 0;

         _stdDevReadyCount = 0;
         _stdDevDirty = true;
         _stdDevLo = 0;
         _stdDevHi = 0;
         _stdDevSum = 0.0;
         _stdDevSumSquares = 0.0;
         _stdDevFiniteCount = 0;
      }

      setFixedXRange(static_cast<float>(cutoff), static_cast<float>(nowMs));
   }
};
