#pragma once

#include <new>
#include <math.h>
#include <Arduino.h>

#include "ArduinoWithDisplay.h"
#include "Color.h"
#include "DisplayBuffer.h"
#include "DisplayField.h"
#include "Format.h"
#include "IScatterPlot.h"
#include "Structs.h"
#include "TimedAverageHistory.h"
#include "Util.h"

class TimedScatterPlot;

///
/// <summary>
/// A single time-series data source owned and rendered by a TimedScatterPlot. Rather than
/// retaining every raw sample, values are aggregated (averaged) into a fixed number of
/// time bins sized to the plot's chart pixel width, using TimedAverageHistory (see
/// TimedAverageHistory.h) for the underlying bin storage/rotation. Since the chart can only ever
/// display one column of resolution per bin, this bounds memory by chart width instead of
/// by history duration or sample rate, and the caller never has to maintain its own
/// buffer. Display flags (showPoints, showLines, showMovingAverage) and color mirror
/// ScatterPlotSeries, but the moving average here is windowed in milliseconds rather than
/// by raw sample count, since the X axis is always time.
/// </summary>
///
class TimedScatterPlotSeries : public IScatterPlotSeries
{
   friend class TimedScatterPlot;

private:
   // Fixed-size time-binned average storage; see TimedAverageHistory.h. Handles all the
   // low-level circular-bin allocation, rotation, and averaging, bounding memory by numBins
   // (typically the chart's pixel width) regardless of history duration or sample rate.
   TimedAverageHistory _bins;

   // Scratch buffers holding the most recently captured snapshot (newest-first), refreshed
   // once per render() call and reused by axis-range computation and drawing. Sized to
   // _bins.numBins() (fixed at construction), so they no longer scale with history
   // duration or sample rate.
   float* _values = nullptr;
   unsigned long* _agesMs = nullptr;
   size_t _count = 0;

   // Scratch buffer holding the centered moving average computed over the current
   // snapshot, indexed the same as _values/_agesMs (newest-first).
   float* _movingAverageBuffer = nullptr;

   // Scratch buffers holding the rolling mean +/- stddev band computed over the current
   // snapshot (windowed the same way as the moving average, over movingStatsDuration), indexed
   // the same as _values/_agesMs (newest-first), so the band moves with the data instead
   // of being a single flat mean/stddev over the whole retained history.
   float* _stdDevLowBuffer = nullptr;
   float* _stdDevHighBuffer = nullptr;

   // Per-bin-slot cache of the last moving-average/stddev band values computed with a
   // fully populated centered window (i.e. real samples existed on both sides out to
   // movingStatsDuration/2). Indexed the same as _values/_agesMs (newest-first) and
   // shifted by _shiftLockedBuffers() as bins rotate, so once a bin's window is complete
   // its value is locked in and stops drifting as it nears the trailing (oldest) edge of
   // the retained history, where fewer older neighbors remain to average against.
   float* _lockedMovingAverage = nullptr;
   float* _lockedStdDevLow = nullptr;
   float* _lockedStdDevHigh = nullptr;
   unsigned long _lastRotationCount = 0;

   ///
   /// <summary>
   /// Allocates the raw snapshot buffers (_values/_agesMs), sized to _bins.numBins().
   /// Called once from the constructor since the bin count never changes afterward. The
   /// moving-average/stddev buffers are allocated separately and lazily; see
   /// _ensureStatsBuffersAllocated().
   /// </summary>
   /// <returns>True if every buffer was allocated successfully.</returns>
   ///
   bool _allocateBuffers()
   {
      size_t numBins = _bins.numBins();
      _values = new (std::nothrow) float[numBins];
      _agesMs = new (std::nothrow) unsigned long[numBins];

      if (_values == nullptr || _agesMs == nullptr)
      {
         Util::setHaltReason("OOM allocating rendering buffers in TimedScatterPlotSeries");
         Util::reset();
         return false;
      }

      return true;
   }

   ///
   /// <summary>
   /// Lazily allocates the moving-average/stddev buffers on first use (i.e. the first time
   /// showMovingAverage or showStdDevBand is set), rather than paying for them up front for
   /// series that never enable those features. Safe to call repeatedly; only allocates once.
   /// </summary>
   /// <returns>True if the buffers are allocated (either just now or previously).</returns>
   ///
   bool _ensureStatsBuffersAllocated()
   {
      if (_movingAverageBuffer != nullptr)
      {
         return true;
      }

      size_t numBins = _bins.numBins();
      _movingAverageBuffer = new (std::nothrow) float[numBins];
      _stdDevLowBuffer = new (std::nothrow) float[numBins];
      _stdDevHighBuffer = new (std::nothrow) float[numBins];
      _lockedMovingAverage = new (std::nothrow) float[numBins];
      _lockedStdDevLow = new (std::nothrow) float[numBins];
      _lockedStdDevHigh = new (std::nothrow) float[numBins];

      if (_movingAverageBuffer == nullptr || _stdDevLowBuffer == nullptr || _stdDevHighBuffer == nullptr
         || _lockedMovingAverage == nullptr || _lockedStdDevLow == nullptr || _lockedStdDevHigh == nullptr)
      {
         Util::setHaltReason("OOM allocating stats buffers in TimedScatterPlotSeries");
         Util::reset();
         return false;
      }

      for (size_t i = 0; i < numBins; i++)
      {
         _lockedMovingAverage[i] = NAN;
         _lockedStdDevLow[i] = NAN;
         _lockedStdDevHigh[i] = NAN;
      }

      return true;
   }

   ///
   /// <summary>
   /// Shifts the locked moving-average/stddev caches to follow the same physical bins as
   /// they rotate to higher (older) indices, discarding entries for bins that have rotated
   /// out entirely and blanking the newly opened bins at the front. Must run before
   /// recomputing so locked values stay aligned with the bin they were computed for.
   /// </summary>
   ///
   void _shiftLockedBuffers()
   {
      unsigned long currentRotationCount = _bins.rotationCount();
      unsigned long delta = currentRotationCount - _lastRotationCount;
      _lastRotationCount = currentRotationCount;

      if (delta == 0 || _lockedMovingAverage == nullptr)
      {
         return;
      }

      size_t numBins = _bins.numBins();
      if (delta >= numBins)
      {
         for (size_t i = 0; i < numBins; i++)
         {
            _lockedMovingAverage[i] = NAN;
            _lockedStdDevLow[i] = NAN;
            _lockedStdDevHigh[i] = NAN;
         }
         return;
      }

      for (size_t i = numBins; i-- > delta; )
      {
         _lockedMovingAverage[i] = _lockedMovingAverage[i - delta];
         _lockedStdDevLow[i] = _lockedStdDevLow[i - delta];
         _lockedStdDevHigh[i] = _lockedStdDevHigh[i - delta];
      }
      for (size_t i = 0; i < delta; i++)
      {
         _lockedMovingAverage[i] = NAN;
         _lockedStdDevLow[i] = NAN;
         _lockedStdDevHigh[i] = NAN;
      }
   }

   ///
   /// <summary>
   /// Captures a fresh newest-first snapshot from the underlying time bins and recomputes
   /// the centered moving average over it. Called once per render() pass.
   /// </summary>
   ///
   void _refreshSnapshot()
   {
      _count = _bins.snapshot(_values, _agesMs);

      // Index 0 (age 0) is always the currently-open bin, which is still accumulating new
      // raw samples into its running average (see TimedAverageHistoryBase::add()). Its
      // average therefore keeps changing - and visibly jitters on the chart - every time a
      // new sample lands in it, right up until it closes and a new bin opens. Blanking it
      // here (rather than only at render/rasterize time) excludes it consistently from
      // everything downstream that already treats NAN as "no data": axis-range scanning,
      // point/line rasterization, and the moving-average/stddev-band recompute below - at
      // the cost of one bin-duration of latency before new data appears on the chart.
      if (_count > 0)
      {
         _values[0] = NAN;
      }

      _shiftLockedBuffers();
      if (showMovingAverage || showStdDevBand)
      {
         _ensureStatsBuffersAllocated();
      }
      if (_movingAverageBuffer != nullptr)
      {
         _recomputeMovingAverage();
      }
      if (_stdDevLowBuffer != nullptr)
      {
         _recomputeStdDevBand();
      }
   }

   ///
   /// <summary>
   /// Recomputes a rolling mean +/- stddev band over the current snapshot, windowed the
   /// same way as the centered moving average (every retained sample whose age falls
   /// within movingStatsDuration/2 of that sample's own age), so the band moves with the data
   /// instead of being a single flat mean/stddev over the whole retained history. Indices
   /// with no finite samples in their window are set to NAN in both output buffers.
   ///
   /// The leading (newest) edge, where the window would need samples newer than any that
   /// can ever exist, is left blank (NAN) rather than drawn with a lopsided partial window.
   /// The trailing (oldest) edge, where the window needs older samples than are currently
   /// retained, is locked to the first value computed once real data eventually fills that
   /// window, via _lockedStdDevLow/_lockedStdDevHigh, so it doesn't keep drifting frame to
   /// frame as more history accumulates.
   /// </summary>
   ///
   void _recomputeStdDevBand()
   {
      if (_count == 0)
      {
         return;
      }

      // _agesMs is newest-first (smallest age at index 0, largest age at the end).
      const float halfWindow = movingStatsDuration / 2.0f;

      for (size_t i = 0; i < _count; i++)
      {
         const unsigned long centerAge = _agesMs[i];

         if (static_cast<float>(centerAge) < halfWindow)
         {
            // Leading edge: the window would need samples newer than "now", which can
            // never exist, so this index never draws.
            _stdDevLowBuffer[i] = NAN;
            _stdDevHighBuffer[i] = NAN;
            continue;
         }

         float sum = 0.0f;
         size_t finiteCount = 0;
         bool olderWindowComplete = false;

         // Fold in samples that are the same age or older (index >= i).
         for (size_t j = i; j < _count; j++)
         {
            if ((static_cast<float>(_agesMs[j]) - static_cast<float>(centerAge)) > halfWindow)
            {
               olderWindowComplete = true;
               break;
            }
            if (isfinite(_values[j]))
            {
               sum += _values[j];
               finiteCount++;
            }
         }

         // Fold in samples that are newer (index < i).
         for (size_t j = i; j-- > 0; )
         {
            if ((static_cast<float>(centerAge) - static_cast<float>(_agesMs[j])) > halfWindow)
            {
               break;
            }
            if (isfinite(_values[j]))
            {
               sum += _values[j];
               finiteCount++;
            }
         }

         if (finiteCount == 0)
         {
            _stdDevLowBuffer[i] = isfinite(_lockedStdDevLow[i]) ? _lockedStdDevLow[i] : NAN;
            _stdDevHighBuffer[i] = isfinite(_lockedStdDevHigh[i]) ? _lockedStdDevHigh[i] : NAN;
            continue;
         }

         const float mean = sum / static_cast<float>(finiteCount);
         float sumSquares = 0.0f;

         // Same two-pass windowing as above, now accumulating squared deviations from mean.
         for (size_t j = i; j < _count; j++)
         {
            if ((static_cast<float>(_agesMs[j]) - static_cast<float>(centerAge)) > halfWindow)
            {
               break;
            }
            if (isfinite(_values[j]))
            {
               const float deviation = _values[j] - mean;
               sumSquares += deviation * deviation;
            }
         }
         for (size_t j = i; j-- > 0; )
         {
            if ((static_cast<float>(centerAge) - static_cast<float>(_agesMs[j])) > halfWindow)
            {
               break;
            }
            if (isfinite(_values[j]))
            {
               const float deviation = _values[j] - mean;
               sumSquares += deviation * deviation;
            }
         }

         const float stdDev = sqrtf(sumSquares / static_cast<float>(finiteCount));
         float low = mean - stdDev;
         float high = mean + stdDev;

         if (olderWindowComplete)
         {
            _lockedStdDevLow[i] = low;
            _lockedStdDevHigh[i] = high;
            _stdDevLowBuffer[i] = low;
            _stdDevHighBuffer[i] = high;
         }
         else if (isfinite(_lockedStdDevLow[i]))
         {
            // Trailing edge still filling in and this slot already has a locked value from
            // a completed window; keep showing that instead of the still-shrinking partial.
            _stdDevLowBuffer[i] = _lockedStdDevLow[i];
            _stdDevHighBuffer[i] = _lockedStdDevHigh[i];
         }
         else
         {
            _stdDevLowBuffer[i] = low;
            _stdDevHighBuffer[i] = high;
         }
      }
   }

   ///
   /// <summary>
   /// Recomputes the centered moving average over the current snapshot. Each output
   /// value is the average of every retained sample whose age falls within
   /// movingStatsDuration/2 of that sample's own age.
   ///
   /// The leading (newest) edge, where the window would need samples newer than any that
   /// can ever exist, is left blank (NAN) rather than drawn with a lopsided partial window.
   /// The trailing (oldest) edge, where the window needs older samples than are currently
   /// retained, is locked to the first value computed once real data eventually fills that
   /// window, via _lockedMovingAverage, so it doesn't keep drifting frame to frame as more
   /// history accumulates.
   /// </summary>
   ///
   void _recomputeMovingAverage()
   {
      if (_count == 0)
      {
         return;
      }

      // _agesMs is newest-first (smallest age at index 0, largest age at the end).
      const float halfWindow = movingStatsDuration / 2.0f;

      for (size_t i = 0; i < _count; i++)
      {
         const unsigned long centerAge = _agesMs[i];

         if (static_cast<float>(centerAge) < halfWindow)
         {
            // Leading edge: the window would need samples newer than "now", which can
            // never exist, so this index never draws.
            _movingAverageBuffer[i] = NAN;
            continue;
         }

         float sum = 0.0f;
         size_t finiteCount = 0;
         bool olderWindowComplete = false;

         // Fold in samples that are the same age or older (index >= i).
         for (size_t j = i; j < _count; j++)
         {
            if ((static_cast<float>(_agesMs[j]) - static_cast<float>(centerAge)) > halfWindow)
            {
               olderWindowComplete = true;
               break;
            }
            if (isfinite(_values[j]))
            {
               sum += _values[j];
               finiteCount++;
            }
         }

         // Fold in samples that are newer (index < i).
         for (size_t j = i; j-- > 0; )
         {
            if ((static_cast<float>(centerAge) - static_cast<float>(_agesMs[j])) > halfWindow)
            {
               break;
            }
            if (isfinite(_values[j]))
            {
               sum += _values[j];
               finiteCount++;
            }
         }

         if (finiteCount == 0)
         {
            _movingAverageBuffer[i] = isfinite(_lockedMovingAverage[i]) ? _lockedMovingAverage[i] : NAN;
            continue;
         }

         const float average = sum / static_cast<float>(finiteCount);

         if (olderWindowComplete)
         {
            _lockedMovingAverage[i] = average;
            _movingAverageBuffer[i] = average;
         }
         else if (isfinite(_lockedMovingAverage[i]))
         {
            // Trailing edge still filling in and this slot already has a locked value from
            // a completed window; keep showing that instead of the still-shrinking partial.
            _movingAverageBuffer[i] = _lockedMovingAverage[i];
         }
         else
         {
            _movingAverageBuffer[i] = average;
         }
      }
   }

public:
   ///
   /// <summary>
   /// Width of the centered window used by both the moving average and the stddev band,
   /// in milliseconds, so the two always track the same span of data. Defaults to one
   /// tenth of the series' total retained history window (historyMs).
   /// </summary>
   ///
   float movingStatsDuration;

   ///
   /// <summary>
   /// Constructs a new TimedScatterPlotSeries that aggregates samples into numBins fixed
   /// time bins spanning historyMs, sized to the plot's chart pixel width so memory is
   /// bounded by chart resolution instead of history duration or sample rate.
   /// </summary>
   /// <param name="historyMs">Time window in milliseconds spanned by the retained bins.</param>
   /// <param name="numBins">Number of time bins to retain (typically the chart's pixel width).</param>
   ///
   explicit TimedScatterPlotSeries(unsigned long historyMs, size_t numBins = 16)
      : _bins(historyMs, numBins), movingStatsDuration(static_cast<float>(historyMs) / 10.0f)
   {
      _allocateBuffers();
   }

   TimedScatterPlotSeries(const TimedScatterPlotSeries&) = delete;
   TimedScatterPlotSeries& operator=(const TimedScatterPlotSeries&) = delete;

   ~TimedScatterPlotSeries()
   {
      delete[] _values;
      delete[] _agesMs;
      delete[] _movingAverageBuffer;
      delete[] _stdDevLowBuffer;
      delete[] _stdDevHighBuffer;
      delete[] _lockedMovingAverage;
      delete[] _lockedStdDevLow;
      delete[] _lockedStdDevHigh;
   }

   ///
   /// <summary>
   /// Adds a new value to the series, accumulating it into the currently-open time bin
   /// (rotating out expired bins first as needed).
   /// </summary>
   /// <param name="value">Value to add.</param>
   ///
   void add(float value) override
   {
      _bins.add(value);
   }

   ///
   /// <summary>
   /// Removes every retained sample from this series.
   /// </summary>
   ///
   void clear() override
   {
      _bins.reset();
      _count = 0;
      _lastRotationCount = 0;

      if (_lockedMovingAverage != nullptr)
      {
         size_t numBins = _bins.numBins();
         for (size_t i = 0; i < numBins; i++)
         {
            _lockedMovingAverage[i] = NAN;
            _lockedStdDevLow[i] = NAN;
            _lockedStdDevHigh[i] = NAN;
         }
      }
   }

   ///
   /// <summary>
   /// Gets the number of samples currently retained (as of the last render() snapshot).
   /// </summary>
   /// <returns>Number of retained samples.</returns>
   ///
   size_t getCount() const { return _count; }

   ///
   /// <summary>
   /// Gets the most recently computed moving-average value (the newest retained sample's
   /// centered window average), or NAN if no samples are retained or the newest sample's
   /// window is not yet ready.
   /// </summary>
   /// <returns>Newest moving-average value, or NAN if unavailable.</returns>
   ///
   float getLatestMovingAverage() const
   {
      return (_count > 0 && _movingAverageBuffer != nullptr) ? _movingAverageBuffer[0] : NAN;
   }

   ///
   /// <summary>
   /// Gets the most recently computed rolling stddev (half the width of the newest
   /// retained sample's mean +/- stddev band), or NAN if no samples are retained or the
   /// band is not finite for the newest sample.
   /// </summary>
   /// <returns>Newest stddev value, or NAN if unavailable.</returns>
   ///
   float getLatestStdDev() const
   {
      if (_count == 0 || _stdDevLowBuffer == nullptr || !isfinite(_stdDevLowBuffer[0]) || !isfinite(_stdDevHighBuffer[0]))
      {
         return NAN;
      }

      return (_stdDevHighBuffer[0] - _stdDevLowBuffer[0]) / 2.0f;
   }

   ///
   /// <summary>
   /// Gets the number of time bins retained by this series (fixed at construction).
   /// </summary>
   /// <returns>Number of retained time bins.</returns>
   ///
   size_t numBins() const { return _bins.numBins(); }

   ///
   /// <summary>
   /// Gets the duration spanned by each time bin retained by this series, in milliseconds
   /// (fixed at construction).
   /// </summary>
   /// <returns>Bin duration in milliseconds.</returns>
   ///
   unsigned long binDurationMs() const { return _bins.binDurationMs(); }
};

///
/// <summary>
/// Renders one or more TimedScatterPlotSeries on the display as a shared-axis, time-on-X
/// scatter plot within a fixed screen rectangle. Callers create series via createSeries(),
/// feed them by calling add(value) as data becomes available, and set each series'
/// display flags (showPoints, showLines, showMovingAverage) and color, just like
/// ScatterPlot. TimedScatterPlot owns all sample storage internally as fixed-size time
/// bins sized to the chart's pixel width, so the caller never maintains its own buffer
/// and memory does not scale with history duration or sample rate. The X axis always
/// represents time-ago
/// (newest samples on the right) over a fixed historyMs window; while the plot has been
/// running for less than historyMs, samples instead grow left-to-right from a fixed start
/// point, filling in new columns as data arrives, after which normal right-anchored
/// scrolling takes over. The Y axis range is computed automatically each render() from
/// whatever is currently displayed. The rectangle given to the constructor is fixed for
/// the plot's lifetime, allowing several plots to share one screen.
/// </summary>
///
class TimedScatterPlot : public IScatterPlot
{
private:
   ArduinoWithDisplay* _display;
   Rect16 _rect;
   unsigned long _historyMs;
   float _minValueStep;

   Format _minMaxFormat;

   // Right-aligned copy of _minMaxFormat used for the moving-average/stddev mid-axis
   // DisplayFields (see _drawMidAxisLabels()), which must always line up with the
   // right-aligned min/max axis labels regardless of the alignment passed to
   // setMinMaxFormat() (e.g. a sensor's own Format, which typically defaults to
   // left-aligned). DisplayField only stores a pointer to its Format, so this must be a
   // persistent member rather than a local temporary; kept in sync with _minMaxFormat
   // wherever it's set.
   Format _midAxisLabelFormat;

   // Optional centered title drawn above the chart; when set, the chart area is reduced
   // to make room for it.
   String _title;

   // Chart colors. _outerBackgroundColor covers the area outside the plotted-data region
   // (e.g. behind axis labels/title); _plotAreaBackgroundColor covers the plotted-data
   // region itself (drawn via the DisplayBuffer's background/layer-0 color).
   Color _outerBackgroundColor = Color::BLACK;
   Color _plotAreaBackgroundColor = Color::BLACK;
   Color _axisColor = Color::GRAY;
   Color _labelColor = Color::LABEL;

   int16_t _chartLeft = 0;
   int16_t _chartTop = 0;
   int16_t _chartWidth = 0;
   int16_t _chartHeight = 0;
   float _axisYMin = 0.0f;
   float _axisYMax = 0.0f;

   // True when the underlying data range this frame was flat (every plotted value
   // identical), before _axisYMin/_axisYMax were padded out to a nonzero span for axis
   // label/geometry purposes. _toPixel() uses this (rather than re-deriving flatness from
   // the already-padded axis span) to still center a flat series in the chart instead of
   // mapping it to the bottom edge.
   bool _dataIsFlat = false;

   bool _hasRenderedFrame = false;
   bool _forceFullRedraw = false;

   TimedScatterPlotSeries** _series = nullptr;
   size_t _seriesCount = 0;
   size_t _seriesCapacity = 0;

   // One optional DisplayField per series for its moving-average and stddev mid-axis
   // labels (see _drawMidAxisLabels()), lazily created the first time each is needed.
   // DisplayField redraws only its value (via an off-screen sprite) when it changes,
   // avoiding the flicker of erasing/reprinting raw text every frame. Parallel to
   // _series, growing/shrinking together with it.
   DisplayField** _movingAverageFields = nullptr;
   DisplayField** _stdDevFields = nullptr;

   // Plot-wide shared pixel buffer used to diff each frame's rasterized content (raw
   // points/lines, moving-average line, and stddev band each get their own layer index)
   // against the previous frame's, drawing only pixels whose layer actually changed. See
   // DisplayBuffer.h; extracted as a standalone reusable class since this diffing
   // behavior isn't specific to scatter plots.
   DisplayBuffer _displayBuffer;

   unsigned long _lastRenderMicros = 0;
   unsigned long _lastComputeMicros = 0;
   unsigned long _lastDisplayMicros = 0;

   ///
   /// <summary>
   /// Computes the power-of-ten scale factor corresponding to the axis label precision.
   /// </summary>
   /// <returns>Scale factor (10^precision) used to round values to axis precision.</returns>
   ///
   float _axisPrecisionScale() const
   {
      float scale = 1.0f;
      const uint8_t precision = _minMaxFormat.precision();
      for (uint8_t i = 0; i < precision; i++)
      {
         scale *= 10.0f;
      }
      return scale;
   }

   ///
   /// <summary>
   /// Aligns a value to the sensor step size if one is configured.
   /// </summary>
   /// <param name="value">Value to align.</param>
   /// <returns>Value aligned to sensor step size, or original value if step is not configured.</returns>
   ///
   float _alignToSensorStep(float value) const
   {
      if (_minValueStep <= 0.0f)
      {
         return value;
      }

      return roundf(value / _minValueStep) * _minValueStep;
   }

   ///
   /// <summary>
   /// Rounds a value down to the axis label precision.
   /// </summary>
   /// <param name="value">Value to round.</param>
   /// <returns>Value rounded down to axis precision.</returns>
   ///
   float _roundDownToAxisPrecision(float value) const
   {
      const float scale = _axisPrecisionScale();
      return floorf(value * scale) / scale;
   }

   ///
   /// <summary>
   /// Rounds a value up to the axis label precision.
   /// </summary>
   /// <param name="value">Value to round.</param>
   /// <returns>Value rounded up to axis precision.</returns>
   ///
   float _roundUpToAxisPrecision(float value) const
   {
      const float scale = _axisPrecisionScale();
      return ceilf(value * scale) / scale;
   }

   ///
   /// <summary>
   /// Refreshes every series' snapshot from its internal time bins and scans the
   /// results to compute the shared Y axis range needed to fit whatever is currently
   /// displayed: a series' raw samples contribute only if showPoints or showLines is set,
   /// and its moving average contributes only if showMovingAverage is set.
   /// </summary>
   /// <param name="outYMin">Receives the minimum finite Y value found, or NAN if none exists.</param>
   /// <param name="outYMax">Receives the maximum finite Y value found, or NAN if none exists.</param>
   ///
   void _refreshAndComputeAxisRange(float* outYMin, float* outYMax)
   {
      float yMin = NAN;
      float yMax = NAN;

      for (size_t s = 0; s < _seriesCount; s++)
      {
         TimedScatterPlotSeries* series = _series[s];
         series->_refreshSnapshot();

         bool includeRaw = series->showPoints || series->showLines;
         if (includeRaw)
         {
            for (size_t i = 0; i < series->_count; i++)
            {
               float value = series->_values[i];
               if (isfinite(value))
               {
                  if (!isfinite(yMin) || (value < yMin)) yMin = value;
                  if (!isfinite(yMax) || (value > yMax)) yMax = value;
               }
            }
         }

         if (series->showMovingAverage)
         {
            for (size_t i = 0; i < series->_count; i++)
            {
               float value = series->_movingAverageBuffer[i];
               if (isfinite(value))
               {
                  if (!isfinite(yMin) || (value < yMin)) yMin = value;
                  if (!isfinite(yMax) || (value > yMax)) yMax = value;
               }
            }
         }

         if (series->showStdDevBand)
         {
            for (size_t i = 0; i < series->_count; i++)
            {
               float low = series->_stdDevLowBuffer[i];
               float high = series->_stdDevHighBuffer[i];
               if (isfinite(low) && (!isfinite(yMin) || (low < yMin))) yMin = low;
               if (isfinite(high) && (!isfinite(yMax) || (high > yMax))) yMax = high;
            }
         }
      }

      *outYMin = yMin;
      *outYMax = yMax;
   }

   ///
   /// <summary>
   /// Formats a Y-axis label value using the configured min/max Format.
   /// </summary>
   /// <param name="value">Y-axis value to format.</param>
   /// <returns>Formatted label text.</returns>
   ///
   String _formatYLabel(float value) const
   {
      return String(_minMaxFormat.toString(value).c_str());
   }

   ///
   /// <summary>
   /// Computes the chart's pixel geometry. The Y-axis label column is sized directly from
   /// the fixed output length of _minMaxFormat (length() * charW()) rather than the rendered
   /// width of the current min/max label text, so the column width - and therefore
   /// _chartLeft/_chartWidth - never shifts as the axis range changes.
   /// </summary>
   ///
   void _computeChartGeometry()
   {
      uint16_t yLabelWidth = static_cast<uint16_t>(_minMaxFormat.length()) * _display->charW();
      int16_t yAxisWidth = static_cast<int16_t>(yLabelWidth) + 2;

      int16_t axisLineHeight = _display->charH() + 2;
      int16_t titlePadding = _display->charH() / 4;
      int16_t titleHeight = _title.length() > 0 ? (_display->charH() + 2 * titlePadding) : 0;

      _chartLeft = static_cast<int16_t>(_rect.x) + yAxisWidth;
      _chartTop = static_cast<int16_t>(_rect.y) + titleHeight;
      _chartWidth = static_cast<int16_t>(_rect.width) - yAxisWidth;
      _chartHeight = static_cast<int16_t>(_rect.height) - axisLineHeight - titleHeight;
   }

   ///
   /// <summary>
   /// Draws the gray X/Y axis lines and the min/max/time-range axis labels for the current
   /// chart geometry and axis range.
   /// </summary>
   ///
   void _drawAxes() const
   {
      if (_title.length() > 0)
      {
         int16_t titleX = _chartLeft + (_chartWidth - static_cast<int16_t>(_display->textWidth(_title.c_str()))) / 2;
         int16_t titlePadding = _display->charH() / 4;
         _display->setCursor(max(_chartLeft, titleX), static_cast<int16_t>(_rect.y) + titlePadding);
         _display->print(_title, _labelColor, _outerBackgroundColor);
      }

      _display->fillRect(_chartLeft, _chartTop + _chartHeight - 1, _chartWidth, 1, _axisColor);
      _display->fillRect(_chartLeft, _chartTop, 1, _chartHeight, _axisColor);

      // Physically clear the reserved Y-axis label column before drawing the new min/max
      // labels. Labels are right-aligned based on their own rendered text width, so a
      // narrower new label wouldn't otherwise overwrite every pixel a previously-drawn
      // wider label occupied, leaving stale digits on screen (e.g. after recreatePlot()
      // swaps in a new source/plot).
      int16_t yLabelColumnX = static_cast<int16_t>(_rect.x);
      int16_t yLabelColumnWidth = _chartLeft - 2 - yLabelColumnX;
      _display->fillRect(yLabelColumnX, _chartTop, yLabelColumnWidth, _display->charH(), _outerBackgroundColor);
      _display->fillRect(yLabelColumnX, _chartTop + _chartHeight - _display->charH(), yLabelColumnWidth, _display->charH(), _outerBackgroundColor);

      String maxLabel = _formatYLabel(_axisYMax);
      int16_t maxLabelX = _chartLeft - 2 - static_cast<int16_t>(_display->textWidth(maxLabel.c_str()));
      _display->setCursor(maxLabelX, _chartTop);
      _display->print(maxLabel, _labelColor, _outerBackgroundColor);

      String minLabel = _formatYLabel(_axisYMin);
      int16_t minLabelX = _chartLeft - 2 - static_cast<int16_t>(_display->textWidth(minLabel.c_str()));
      minLabelX = max(static_cast<int16_t>(0), minLabelX);
      _display->setCursor(minLabelX, _chartTop + _chartHeight - _display->charH());
      _display->print(minLabel, _labelColor, _outerBackgroundColor);

      String rangeLabel = _formatHistoryRangeLabel();
      int16_t rangeLabelX = _chartLeft + (_chartWidth - static_cast<int16_t>(_display->textWidth(rangeLabel.c_str()))) / 2;
      _display->setCursor(max(_chartLeft, rangeLabelX), _chartTop + _chartHeight + 1);
      _display->print(rangeLabel, _labelColor, _outerBackgroundColor);
   }

   ///
   /// <summary>
   /// Draws each series' latest moving-average and/or stddev value, stacked vertically
   /// centered in the Y-axis label column (between the min and max labels), each in the
   /// same color as the line/band it corresponds to and right-aligned like the min/max
   /// labels. Values reflect the newest retained sample as of the last render() snapshot.
   /// Each value is rendered through a lazily-created DisplayField (see
   /// _movingAverageFields/_stdDevFields), which only redraws its sprite-backed value when
   /// it changes, avoiding the flicker of reprinting raw text every frame.
   /// </summary>
   ///
   void _drawMidAxisLabels()
   {
      int16_t lineHeight = _display->charH();
      size_t labelCount = 0;

      for (size_t i = 0; i < _seriesCount; i++)
      {
         const TimedScatterPlotSeries* series = _series[i];
         if (series->showMovingAverage) labelCount++;
         if (series->showStdDevBand) labelCount++;
      }

      if (labelCount == 0)
      {
         return;
      }

      int16_t y = _chartTop + (_chartHeight - static_cast<int16_t>(labelCount) * lineHeight) / 2;
      int16_t labelWidth = static_cast<int16_t>(_display->textWidth(std::string(_minMaxFormat.length(), '0').c_str()));
      int16_t labelX = max(static_cast<int16_t>(0), static_cast<int16_t>(_chartLeft - 2 - labelWidth));

      for (size_t i = 0; i < _seriesCount; i++)
      {
         TimedScatterPlotSeries* series = _series[i];

         if (series->showStdDevBand)
         {
            if (_stdDevFields[i] == nullptr)
            {
               _stdDevFields[i] = new DisplayField(_display, labelX, y, "", _midAxisLabelFormat, 2, true, series->stdDevBandColor, series->stdDevBandColor);
            }

            float value = series->getLatestStdDev();
            if (isfinite(value))
            {
               _stdDevFields[i]->setValue(value);
               _stdDevFields[i]->draw();
            }
            y += lineHeight;
         }

         if (series->showMovingAverage)
         {
            if (_movingAverageFields[i] == nullptr)
            {
               _movingAverageFields[i] = new DisplayField(_display, labelX, y, "", _midAxisLabelFormat, 2, true, series->movingAverageColor, series->movingAverageColor);
            }

            float value = series->getLatestMovingAverage();
            if (isfinite(value))
            {
               _movingAverageFields[i]->setValue(value);
               _movingAverageFields[i]->draw();
            }
            y += lineHeight;
         }
      }
   }

   ///
   /// <summary>
   /// Formats the history-window label shown below the X axis, automatically switching
   /// units so the value stays readable: milliseconds below 2 seconds, seconds below 2
   /// minutes, and minutes otherwise.
   /// </summary>
   /// <returns>Formatted history-range label, e.g. "500 ms", "45 s", or "10 m".</returns>
   ///
   String _formatHistoryRangeLabel() const
   {
      return Util::formatDuration(_historyMs);
   }

   ///
   /// <summary>
   /// Converts a bin index/value pair to pixel coordinates relative to the chart's top-left
   /// corner (0,0 is the top-left interior pixel; _chartLeft/_chartTop are added by the
   /// caller separately when drawing to the display). Newest samples (bin index 0) map to
   /// the right edge; the oldest possible bin (numBins - 1) maps to the left edge. Before
   /// every bin has been populated, only the newest binIndex values are ever passed in (see
   /// TimedAverageHistory::snapshot()'s filledBins() count), so those samples naturally land
   /// on the rightmost columns and new columns fill in to the left as more bins are
   /// populated - no separate ramp-up tracking or wall-clock state is needed.
   /// </summary>
   /// <param name="binIndex">Newest-first index of the sample within its series (0 is newest).</param>
   /// <param name="numBins">Total number of bins retained by the series this sample came from.</param>
   /// <param name="value">Y value to convert.</param>
   /// <param name="outX">Receives the chart-relative pixel X coordinate.</param>
   /// <param name="outY">Receives the chart-relative pixel Y coordinate.</param>
   ///
   void _toPixel(size_t binIndex, size_t numBins, float value, int16_t& outX, int16_t& outY) const
   {
      float ySpan = _axisYMax - _axisYMin;

      // binIndex is a stable integer identity for a given retained sample (bin 0 is
      // always the newest/still-open bin, regardless of how many rotations have
      // occurred), so mapping it to a pixel column with pure integer arithmetic - as
      // opposed to first converting to a float age-based fraction of _historyMs -
      // guarantees the exact same binIndex always lands on the exact same pixel
      // column, frame after frame, with no floating-point rounding drift.
      //
      // The left-most chart column is reserved for the gray Y-axis line (drawn by
      // _drawAxes()), so data points are confined to columns 1..chartWidth - 1;
      // otherwise the oldest retained sample would land on column 0 and overwrite the
      // axis line on every redraw.
      const int32_t span = static_cast<int32_t>(_chartWidth) - 2;
      const int32_t denom = (numBins > 1) ? static_cast<int32_t>(numBins - 1) : 1;
      const int32_t clampedIndex = min(static_cast<int32_t>(binIndex), denom);
      outX = static_cast<int16_t>(1 + span - (clampedIndex * span) / denom);

      // The bottom-most chart row is reserved for the gray X-axis line (drawn by
      // _drawAxes()), so data points are confined to chartHeight - 1 rows above it;
      // otherwise a minimum-value sample would land on the same row as the axis line
      // and overwrite it on every redraw. When the underlying data range is flat (e.g.
      // every plotted value is identical), center the data instead of pinning it to the
      // axis-maximum edge; _dataIsFlat is used rather than re-checking ySpan here since
      // render() pads _axisYMin/_axisYMax out to a nonzero span for axis label/geometry
      // purposes even when the real data range was flat.
      if (_dataIsFlat)
      {
         outY = static_cast<int16_t>((_chartHeight - 2) / 2);
      }
      else
      {
         outY = static_cast<int16_t>(((_axisYMax - value) / ySpan) * (_chartHeight - 2));
      }

      outX = constrain(outX, static_cast<int16_t>(1), static_cast<int16_t>(_chartWidth - 1));
      outY = constrain(outY, static_cast<int16_t>(0), static_cast<int16_t>(_chartHeight - 2));
   }

       ///
       /// <summary>
       /// Rasterizes a set of samples (connected with lines when connectPoints is set) into the
       /// plot-wide shared display buffer under the given layer index, aligning to the sensor
       /// step first when alignToStep is set. Samples must be supplied newest-first but are
       /// rasterized oldest-first so lines connect in chronological order. Does not clear the
       /// buffer first; callers rasterize every series/layer into the same shared buffer before
       /// diffing once.
       /// </summary>
       /// <param name="values">Newest-first sample values to plot.</param>
       /// <param name="count">Number of samples in values.</param>
       /// <param name="numBins">Total number of bins retained by the series these samples came from.</param>
       /// <param name="connectPoints">If true, connects consecutive finite samples with a line.</param>
       /// <param name="drawPoints">If true, sets each finite sample's own pixel.</param>
       /// <param name="alignToStep">If true, aligns each value to the configured sensor step before plotting.</param>
       /// <param name="layer">Layer index (1..DisplayBuffer::MAX_LAYERS) to stamp for this series' data.</param>
       ///
       void _rasterizeSeriesData(const float* values, size_t count, size_t numBins, bool connectPoints, bool drawPoints, bool alignToStep, uint8_t layer)
       {
          if (count == 0 || layer == 0)
          {
             return;
          }

          bool havePrevPoint = false;
          int16_t prevX = 0;
          int16_t prevY = 0;

          // Iterate oldest-first (reverse of the newest-first snapshot order) so lines connect
          // chronologically left-to-right.
          for (size_t idx = count; idx-- > 0; )
          {
             float value = values[idx];
             if (!isfinite(value))
             {
                continue;
             }

             if (alignToStep && (_minValueStep > 0.0f))
             {
                value = _alignToSensorStep(value);
             }

             int16_t x;
             int16_t y;
             _toPixel(idx, numBins, value, x, y);

             if (connectPoints && havePrevPoint)
             {
                _displayBuffer.drawLine(prevX, prevY, x, y, layer);
             }
             if (drawPoints)
             {
                _displayBuffer.setPixel(x, y, layer);
             }

             prevX = x;
             prevY = y;
             havePrevPoint = true;
          }
       }

       ///
       /// <summary>
       /// Rasterizes a rolling mean +/- stddev band into the plot-wide shared display buffer
       /// under the given layer index. Each buffer is rasterized as a connected line the same
       /// way _rasterizeSeriesData draws a moving-average line, so the band moves with the
       /// data instead of being a static pair of horizontal lines. Does not clear the buffer first.
       /// </summary>
       /// <param name="lowValues">Newest-first mean-minus-stddev values to plot.</param>
       /// <param name="highValues">Newest-first mean-plus-stddev values to plot.</param>
       /// <param name="count">Number of samples in lowValues/highValues.</param>
       /// <param name="numBins">Total number of bins retained by the series these samples came from.</param>
       /// <param name="layer">Layer index (1..DisplayBuffer::MAX_LAYERS) to stamp for this band.</param>
       ///
       void _rasterizeStdDevBand(const float* lowValues, const float* highValues, size_t count, size_t numBins, uint8_t layer)
       {
          if (count == 0 || layer == 0)
          {
             return;
          }

          bool havePrevLow = false;
          bool havePrevHigh = false;
          int16_t prevLowX = 0;
          int16_t prevLowY = 0;
          int16_t prevHighX = 0;
          int16_t prevHighY = 0;

          // Iterate oldest-first (reverse of the newest-first snapshot order) so lines connect
          // chronologically left-to-right, matching _rasterizeSeriesData.
          for (size_t idx = count; idx-- > 0; )
          {
             float lowValue = lowValues[idx];
             if (isfinite(lowValue))
             {
                int16_t x;
                int16_t y;
                _toPixel(idx, numBins, lowValue, x, y);
                if (havePrevLow)
                {
                   _displayBuffer.drawLine(prevLowX, prevLowY, x, y, layer);
                }
                prevLowX = x;
                prevLowY = y;
                havePrevLow = true;
             }

             float highValue = highValues[idx];
             if (isfinite(highValue))
             {
                int16_t x;
                int16_t y;
                _toPixel(idx, numBins, highValue, x, y);
                if (havePrevHigh)
                {
                   _displayBuffer.drawLine(prevHighX, prevHighY, x, y, layer);
                }
                prevHighX = x;
                prevHighY = y;
                havePrevHigh = true;
             }
          }
       }

   public:
   ///
   /// <summary>
   /// Converts a Y-axis value to an absolute display pixel row using the axis range
   /// computed during the most recent render() call, for callers that want to overlay
   /// their own annotations (e.g. a mean/stddev band) on top of the chart.
   /// </summary>
   /// <param name="value">Y-axis value to convert.</param>
   /// <returns>Absolute display row, clamped to the chart's interior.</returns>
   ///
   int16_t valueToDisplayY(float value) const
   {
      float ySpan = _axisYMax - _axisYMin;

      int16_t relativeY;
      if (ySpan <= 0.0f)
      {
         relativeY = static_cast<int16_t>((_chartHeight - 2) / 2);
      }
      else
      {
         relativeY = static_cast<int16_t>(((_axisYMax - value) / ySpan) * (_chartHeight - 2));
      }
      relativeY = constrain(relativeY, static_cast<int16_t>(0), static_cast<int16_t>(_chartHeight - 2));
      return _chartTop + relativeY;
   }

   ///
   /// <summary>
   /// Gets the chart interior's left edge in absolute display pixels, as computed during
   /// the most recent render() call.
   /// </summary>
   /// <returns>Absolute display column of the chart's left edge.</returns>
   ///
   int16_t chartLeft() const
   {
      return _chartLeft;
   }

   ///
   /// <summary>
   /// Gets the chart interior's width in pixels, as computed during the most recent
   /// render() call.
   /// </summary>
   /// <returns>Chart interior width in pixels.</returns>
   ///
   int16_t chartWidth() const
   {
      return _chartWidth;
   }

   ///
   /// <summary>
    /// Creates a timed scatter plot renderer that draws within the given fixed screen
   /// rectangle, using a default min/max axis label format (one decimal place,
   /// right-aligned). The X axis range label is always centered and automatically
   /// switches units based on the history window: milliseconds below 2 seconds, seconds
   /// below 2 minutes, and minutes otherwise.
   /// </summary>
   /// <param name="display">Pointer to the display object.</param>
   /// <param name="rect">Fixed screen rectangle this plot draws within; unchanged for the plot's lifetime.</param>
   /// <param name="historyMs">Time window in milliseconds for the visible history.</param>
   /// <param name="minValueStep">Optional sensor resolution step size (0.0 for no alignment, default 0.0).</param>
   /// <param name="title">Optional centered title drawn above the chart; defaults to none, in which case the plot uses the full rectangle.</param>
   ///
   TimedScatterPlot(ArduinoWithDisplay* display, Rect16 rect, unsigned long historyMs, float minValueStep = 0.0f, const String& title = String())
      : TimedScatterPlot(display, rect, historyMs, Format("##.#", Format::Alignment::RIGHT), minValueStep, title)
   {}

   ///
   /// <summary>
   /// Creates a timed scatter plot renderer with a custom min/max axis label format (the X
   /// axis range label's units and precision are chosen automatically based on the history
   /// window and is always centered).
   /// </summary>
   ///
   TimedScatterPlot(ArduinoWithDisplay* display, Rect16 rect, unsigned long historyMs, const Format& minMaxFormat, float minValueStep = 0.0f, const String& title = String())
      : _display(display), _rect(rect), _historyMs((historyMs == 0) ? 1 : historyMs), _minValueStep(minValueStep),
        _minMaxFormat(&minMaxFormat, Format::Alignment::RIGHT), _midAxisLabelFormat(&minMaxFormat, Format::Alignment::RIGHT), _title(title)
   {
   }

   TimedScatterPlot(const TimedScatterPlot&) = delete;
   TimedScatterPlot& operator=(const TimedScatterPlot&) = delete;

   ~TimedScatterPlot()
   {
      for (size_t i = 0; i < _seriesCount; i++)
      {
         delete _series[i];
         delete _movingAverageFields[i];
         delete _stdDevFields[i];
      }
      delete[] _series;
      delete[] _movingAverageFields;
      delete[] _stdDevFields;
   }

   ///
   /// <summary>
   /// Creates a new time series owned by this plot, retaining samples aggregated into a
   /// fixed number of time bins spanning this plot's history window. By default the bin
   /// count matches this plot's rectangle pixel width, since that is the maximum possible
   /// chart resolution; memory is therefore bounded by chart width rather than by history
   /// duration or sample rate.
   /// </summary>
   /// <param name="numBins">Number of time bins to retain, or 0 (the default) to use this plot's rectangle pixel width.</param>
   /// <returns>Pointer to the newly created series.</returns>
   ///
   TimedScatterPlotSeries* createSeries(size_t numBins)
   {      if (_seriesCount >= _seriesCapacity)
      {
         size_t newCapacity = _seriesCapacity + 5;
         TimedScatterPlotSeries** newSeries = new (std::nothrow) TimedScatterPlotSeries*[newCapacity];
         DisplayField** newMovingAverageFields = new (std::nothrow) DisplayField*[newCapacity];
         DisplayField** newStdDevFields = new (std::nothrow) DisplayField*[newCapacity];

         if (newSeries == nullptr || newMovingAverageFields == nullptr || newStdDevFields == nullptr)
         {
            delete[] newSeries;
            delete[] newMovingAverageFields;
            delete[] newStdDevFields;
            Util::setHaltReason("OOM allocating series array in TimedScatterPlot");
            Util::reset();
            return nullptr;
         }

         if (_seriesCount > 0)
         {
            memcpy(newSeries, _series, _seriesCount * sizeof(TimedScatterPlotSeries*));
            memcpy(newMovingAverageFields, _movingAverageFields, _seriesCount * sizeof(DisplayField*));
            memcpy(newStdDevFields, _stdDevFields, _seriesCount * sizeof(DisplayField*));
         }

         delete[] _series;
         delete[] _movingAverageFields;
         delete[] _stdDevFields;
         _series = newSeries;
         _movingAverageFields = newMovingAverageFields;
         _stdDevFields = newStdDevFields;
         _seriesCapacity = newCapacity;
      }

         size_t effectiveNumBins = (numBins > 0) ? numBins : static_cast<size_t>(max<int16_t>(1, static_cast<int16_t>(_rect.width)));
         TimedScatterPlotSeries* newSeries = new TimedScatterPlotSeries(_historyMs, effectiveNumBins);
         _series[_seriesCount] = newSeries;
         _movingAverageFields[_seriesCount] = nullptr;
         _stdDevFields[_seriesCount] = nullptr;
         _seriesCount++;
         return newSeries;
      }

      ///
      /// <summary>
      /// Creates a new time series owned by this plot, using this plot's rectangle pixel
      /// width as the bin count (see IScatterPlot::createSeries()).
      /// </summary>
      /// <returns>Pointer to the newly created series.</returns>
      ///
      IScatterPlotSeries* createSeries() override
      {
         return createSeries(0);
      }

      ///
      /// <summary>
      /// Gets a series by index.
      /// </summary>
      /// <param name="index">Index of the series to retrieve.</param>
      /// <returns>Pointer to the series at the specified index, or nullptr if index is invalid.</returns>
      ///
      IScatterPlotSeries* getSeries(size_t index) override
      {
         return (index < _seriesCount) ? _series[index] : nullptr;
      }

      ///
      /// <summary>
      /// Gets the number of series currently managed by this plot.
      /// </summary>
      /// <returns>Number of series.</returns>
      ///
      size_t getSeriesCount() const override
      {
         return _seriesCount;
      }

      ///
      /// <summary>
      /// Deletes a series at the specified index.
      /// </summary>
      /// <param name="index">Index of the series to delete.</param>
      ///
      void deleteSeries(size_t index) override
      {
      if (index < _seriesCount)
      {
         delete _series[index];
         delete _movingAverageFields[index];
         delete _stdDevFields[index];

         for (size_t i = index; i < _seriesCount - 1; i++)
         {
            _series[i] = _series[i + 1];
            _movingAverageFields[i] = _movingAverageFields[i + 1];
            _stdDevFields[i] = _stdDevFields[i + 1];
         }

         _seriesCount--;
      }
   }

   ///
   /// <summary>
   /// Deletes all series managed by this plot.
   /// </summary>
   ///
   void deleteAllSeries() override
   {
      for (size_t i = 0; i < _seriesCount; i++)
      {
         delete _series[i];
         delete _movingAverageFields[i];
         delete _stdDevFields[i];
         _movingAverageFields[i] = nullptr;
         _stdDevFields[i] = nullptr;
      }
      _seriesCount = 0;
   }

   ///
   /// <summary>
   /// Updates the visible time span in milliseconds. Existing series keep their own
   /// retention window (set at createSeries() time) unless recreated.
   /// </summary>
   /// <param name="historyMs">Time window in milliseconds to display.</param>
   ///
   void setHistoryMs(unsigned long historyMs)
   {
      _historyMs = (historyMs == 0) ? 1 : historyMs;
      _forceFullRedraw = true;
   }

   ///
   /// <summary>
   /// Sets a custom format for axis min/max labels. Forces a full redraw on the next
   /// render() since the reserved Y-axis label column width is derived from this format's
   /// fixed length.
   /// </summary>
   /// <param name="format">Format object defining precision and alignment.</param>
   ///
     void setMinMaxFormat(const Format& format)
    {
       // Y-axis labels are always drawn flush against the chart's left edge, so force
       // right alignment on our own copy regardless of how the caller's format is aligned
       // (e.g. a sensor's format may be left-aligned for use elsewhere, like a table).
       _minMaxFormat = format.clone(Format::Alignment::RIGHT);
       _midAxisLabelFormat = format.clone(Format::Alignment::RIGHT);
       _forceFullRedraw = true;
    }

    ///
    /// <summary>
    /// Sets a custom format for axis min/max labels (see setMinMaxFormat()), matching the
    /// IScatterPlot::setYAxisFormat() interface shared with ScatterPlot.
    /// </summary>
    /// <param name="format">Format object defining precision and alignment.</param>
    ///
    void setYAxisFormat(const Format& format) override
    {
       setMinMaxFormat(format);
    }

   ///
   /// <summary>
   /// Sets or clears the centered title drawn above the chart. Pass an empty string to
   /// remove the title, restoring the full rectangle to the chart. Forces a full redraw
   /// on the next render() since the chart geometry changes.
   /// </summary>
   /// <param name="title">Title text to display, or an empty string for none.</param>
   ///
   void setTitle(const String& title)
   {
      _title = title;
      _forceFullRedraw = true;
   }

   ///
   /// <summary>
   /// Gets the current title text, or an empty string if none is set.
   /// </summary>
   /// <returns>Current title text.</returns>
   ///
   const String& getTitle() const
   {
      return _title;
   }

   ///
   /// <summary>
   /// Sets the colors used to draw the chart's non-data elements. Forces a full redraw on
   /// the next render() since the plot-area background is repainted immediately.
   /// </summary>
   /// <param name="outerBackgroundColor">Color used outside the plotted-data area (e.g. behind axis labels/title).</param>
   /// <param name="plotAreaBackgroundColor">Color used behind the plotted-data area itself.</param>
   /// <param name="axisColor">Color used to draw the axis lines.</param>
   /// <param name="labelColor">Color used to draw axis labels and the title.</param>
   ///
   void setColors(Color outerBackgroundColor, Color plotAreaBackgroundColor, Color axisColor, Color labelColor) override
   {
      _outerBackgroundColor = outerBackgroundColor;
      _plotAreaBackgroundColor = plotAreaBackgroundColor;
      _axisColor = axisColor;
      _labelColor = labelColor;
      _forceFullRedraw = true;
   }

   ///
   /// <summary>
   /// Forces the next render() call to perform a full clear and redraw of the chart area,
   /// even if the computed axis range has not changed.
   /// </summary>
   ///
   void invalidate() override
   {
      _forceFullRedraw = true;
   }

   ///
   /// <summary>
   /// Immediately erases any previously rendered chart content (axes, points, lines, etc.),
   /// clears every owned series' retained samples, and forces the next render() to perform
   /// a full redraw. Mirrors ScatterPlot::clear() so both plot types can be reset the same
   /// way (e.g. when switching data sources) without waiting for enough new samples to
   /// accumulate to make the computed axis range valid again.
   /// </summary>
   ///
   void clear() override
   {
      bool alreadyErased = (_display != nullptr && _hasRenderedFrame);
      if (alreadyErased)
      {
         _display->fillRect(_rect.x, _rect.y, _rect.width, _rect.height, _outerBackgroundColor);
      }

      _displayBuffer.reset(alreadyErased);

      _hasRenderedFrame = false;
      _forceFullRedraw = true;

      for (size_t i = 0; i < _seriesCount; i++)
      {
         _series[i]->clear();
      }
   }

   ///
   /// <summary>
   /// Gets the time in microseconds of the last render operation.
   /// </summary>
   /// <returns>Time elapsed during last render in microseconds.</returns>
   ///
   unsigned long lastRenderMicros() const { return _lastRenderMicros; }

   ///
   /// <summary>
   /// Gets the time in microseconds spent computing (not displaying) the last render.
   /// </summary>
   /// <returns>Time elapsed computing the last frame in microseconds.</returns>
   ///
   unsigned long lastComputeMicros() const { return _lastComputeMicros; }

   ///
   /// <summary>
   /// Gets the time in microseconds spent writing to the display during the last render.
   /// </summary>
   /// <returns>Time elapsed writing to the display in microseconds.</returns>
   ///
   unsigned long lastDisplayMicros() const { return _lastDisplayMicros; }

   ///
   /// <summary>
       /// Renders every owned series within this plot's fixed rectangle: refreshes each
       /// series' snapshot from its internal time bins, computes the shared Y axis
       /// range to fit whatever is currently displayed, then draws axis labels and each
       /// series' points, lines, and/or moving-average line. Every series/layer rasterizes
       /// its current frame into one plot-wide shared DisplayBuffer, each stamping a
       /// distinct layer index, and the whole buffer is diffed against the previous frame's in a
       /// single pass (see DisplayBuffer::diffAndDraw()), drawing only the pixels that actually changed
       /// color; unchanging pixels (the vast majority during steady-state scrolling) are never
       /// redrawn or flashed. No physical black flash-clear is needed even when the axis
       /// range changes: DisplayBuffer::diffAndDraw() erases any pixel that was lit last
       /// frame but isn't lit this frame, and this is safe because _computeChartGeometry()
       /// reserves a fixed-width Y-axis label column, so _chartLeft/_chartWidth never shift.
       /// Axis labels are only redrawn when the axis range or geometry changes,
       /// invalidate() was called, or this is the first frame.
   /// </summary>
   ///
       void render() override
       {
          const unsigned long frameStartMicros = micros();

          if (_display == nullptr || _seriesCount == 0)
          {
             return;
          }

          float yMin;
          float yMax;
          _refreshAndComputeAxisRange(&yMin, &yMax);

          if (!isfinite(yMin) || !isfinite(yMax))
          {
             _lastComputeMicros = micros() - frameStartMicros;
             _lastDisplayMicros = 0;
             _lastRenderMicros = _lastComputeMicros;
             return;
          }

          yMin = _roundDownToAxisPrecision(yMin);
          yMax = _roundUpToAxisPrecision(yMax);
          _dataIsFlat = (yMax <= yMin);
          if (_dataIsFlat)
          {
             yMax = yMin + _axisPrecisionScale();
          }

          const int16_t oldChartLeft = _chartLeft;
          const int16_t oldChartTop = _chartTop;
          const int16_t oldChartWidth = _chartWidth;
          const int16_t oldChartHeight = _chartHeight;
          const float oldAxisYMin = _axisYMin;
          const float oldAxisYMax = _axisYMax;

          _display->setTextSize(2);
          _axisYMin = yMin;
          _axisYMax = yMax;
          _computeChartGeometry();

          if (_chartWidth < 20 || _chartHeight < 20)
          {
             _lastComputeMicros = micros() - frameStartMicros;
             _lastDisplayMicros = 0;
             _lastRenderMicros = _lastComputeMicros;
             return;
          }

          // Bin rotations do not force a whole-chart full redraw here: the buffer-based diff
          // (see DisplayBuffer::diffAndDraw()) naturally redraws every pixel whose layer changed as a
          // result of a rotation, and leaves every unchanged pixel alone, so no special-casing
          // for rotation (or for the ramp-up fill-in period) is needed at all.
          bool geometryChanged = !_hasRenderedFrame
             || (_chartLeft != oldChartLeft) || (_chartTop != oldChartTop)
             || (_chartWidth != oldChartWidth) || (_chartHeight != oldChartHeight);
          bool fullRedraw = _forceFullRedraw || geometryChanged
             || (yMin != oldAxisYMin) || (yMax != oldAxisYMax);

          _forceFullRedraw = false;
          _hasRenderedFrame = true;

          const unsigned long displayStartMicros = micros();

          _display->display.startWrite();

          // The plot-wide display buffer is sized to the current chart dimensions; bind it
          // before either a full redraw or diffing occurs, since a dimension change
          // invalidates any previously accumulated buffer content anyway (handled by
          // fullRedraw resetting the buffer to match the fresh chart area below).
          _displayBuffer.bind(_display, _chartLeft, _chartTop, _chartWidth, _chartHeight);
          _displayBuffer.setBackgroundColor(_plotAreaBackgroundColor);

          // Whenever geometryChanged just physically repainted the chart interior via the
          // fillRect() below (e.g. a brand-new plot instance created by recreatePlot() when
          // switching Source), the display buffer's previous-frame state must be told that
          // the area was already erased. Otherwise bind() (whether it just (re)allocated
          // fresh buffers primed to "unknown/changed", or is reusing buffers from a
          // differently-sized previous plot) leaves _prevMask out of sync with what's
          // actually on screen, causing diffAndDraw() to individually redraw every single
          // pixel in the chart interior to "correct" a mismatch that doesn't really exist -
          // the slow, visible left-to-right clear seen on a Source change.
          if (geometryChanged)
          {
             _displayBuffer.reset(/* alreadyPhysicallyErased */ true);
          }

          if (fullRedraw)
          {
             // Only repaint the full plot rect (title, axis-label gutter, and chart
             // interior) when the chart geometry actually changed (e.g. first frame,
             // setRect()/title change); an axis-range-only change keeps the same geometry
             // and _drawAxes() below already redraws the label text in place, so repainting
             // the whole rect on every axis-range change would flicker the axis-label
             // margins for no reason. The chart interior is still fully refreshed below via
             // _displayBuffer's diffAndDraw().
             if (geometryChanged)
             {
                _display->fillRect(_rect.x, _rect.y, _rect.width, _rect.height, _outerBackgroundColor);
             }

             // No physical black flash-clear of the chart interior is needed here: it is
             // rendered entirely through _displayBuffer below, whose diffAndDraw() already
             // erases (draws black over) any pixel that was lit last frame but isn't lit
             // this frame - including every stale point invalidated by the new axis range.
             // This is only safe because _computeChartGeometry() reserves a fixed-width
             // Y-axis label column (sized from _minMaxFormat's fixed length), so
             // _chartLeft/_chartWidth never shift and there is no stale exterior area left
             // behind by a shrinking/widening chart rectangle.
             _drawAxes();

             for (size_t i = 0; i < _seriesCount; i++)
             {
                // The chart area (and thus the mid-axis label column position) was just
                // cleared/recomputed, so any existing moving-average/stddev DisplayFields
                // are stale (their captured x/y no longer matches); drop them so
                // _drawMidAxisLabels() recreates them at the current position.
                delete _movingAverageFields[i];
                delete _stdDevFields[i];
                _movingAverageFields[i] = nullptr;
                _stdDevFields[i] = nullptr;
             }
          }

          _displayBuffer.clear();

          // Assign each series/layer (raw, moving-average, stddev band) the next free nibble
          // layer index and record its color in the display buffer's palette, then rasterize
          // it into the one shared buffer. Layer 0 is reserved for "unlit"; only
          // DisplayBuffer::MAX_LAYERS distinct layers can be drawn in a single frame given the
          // 4-bit buffer width. Raw points are rasterized first for every series, then moving
          // averages, then stddev bands, so overlays are always drawn on top of raw points
          // (and stddev bands on top of moving averages) even when multiple series overlap.
          uint8_t nextLayer = 1;

          for (size_t i = 0; i < _seriesCount; i++)
          {
             TimedScatterPlotSeries* series = _series[i];

             if ((series->showPoints || series->showLines) && nextLayer <= DisplayBuffer::MAX_LAYERS)
             {
                uint8_t layer = nextLayer++;
                _displayBuffer.setPaletteColor(layer, series->color);
                _rasterizeSeriesData(series->_values, series->_count, series->numBins(), series->showLines, series->showPoints, true, layer);
             }
          }

          for (size_t i = 0; i < _seriesCount; i++)
          {
             TimedScatterPlotSeries* series = _series[i];

             if (series->showMovingAverage && nextLayer <= DisplayBuffer::MAX_LAYERS)
             {
                uint8_t layer = nextLayer++;
                _displayBuffer.setPaletteColor(layer, series->movingAverageColor);
                _rasterizeSeriesData(series->_movingAverageBuffer, series->_count, series->numBins(), true, false, false, layer);
             }
          }

          for (size_t i = 0; i < _seriesCount; i++)
          {
             TimedScatterPlotSeries* series = _series[i];

             if (series->showStdDevBand && nextLayer <= DisplayBuffer::MAX_LAYERS)
             {
                uint8_t layer = nextLayer++;
                _displayBuffer.setPaletteColor(layer, series->stdDevBandColor);
                _rasterizeStdDevBand(series->_stdDevLowBuffer, series->_stdDevHighBuffer, series->_count, series->numBins(), layer);
             }
          }

          _displayBuffer.diffAndDraw();

          _drawMidAxisLabels();

          _display->display.endWrite();

          _lastDisplayMicros = micros() - displayStartMicros;
          _lastRenderMicros = micros() - frameStartMicros;
          _lastComputeMicros = _lastRenderMicros - _lastDisplayMicros;
       }
   };
