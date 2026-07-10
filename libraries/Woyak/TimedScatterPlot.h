#pragma once

#include <new>
#include <math.h>
#include <Arduino.h>

#include "ArduinoWithDisplay.h"
#include "Color.h"
#include "Format.h"
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
class TimedScatterPlotSeries
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

   // Copy of the values/ages/moving-average actually drawn on the last render() pass, so
   // the next render() can erase exactly those strokes instead of clearing the whole chart.
   float* _prevValues = nullptr;
   unsigned long* _prevAgesMs = nullptr;
   size_t _prevCount = 0;
   float* _prevMovingAverageBuffer = nullptr;

   ///
   /// <summary>
   /// Allocates the snapshot/prev-frame rendering buffers, all sized to _bins.numBins().
   /// Called once from the constructor since the bin count never changes afterward.
   /// </summary>
   /// <returns>True if every buffer was allocated successfully.</returns>
   ///
   bool _allocateBuffers()
   {
      size_t numBins = _bins.numBins();
      _values = new (std::nothrow) float[numBins];
      _agesMs = new (std::nothrow) unsigned long[numBins];
      _movingAverageBuffer = new (std::nothrow) float[numBins];
      _prevValues = new (std::nothrow) float[numBins];
      _prevAgesMs = new (std::nothrow) unsigned long[numBins];
      _prevMovingAverageBuffer = new (std::nothrow) float[numBins];

      if (_values == nullptr || _agesMs == nullptr || _movingAverageBuffer == nullptr
         || _prevValues == nullptr || _prevAgesMs == nullptr || _prevMovingAverageBuffer == nullptr)
      {
         Util::setHaltReason("OOM allocating rendering buffers in TimedScatterPlotSeries");
         Util::reset();
         return false;
      }

      return true;
   }

   ///
   /// <summary>
   /// Copies the values/ages/moving-average just drawn into the previous-frame buffers, so
   /// the next render() can erase exactly those strokes.
   /// </summary>
   ///
   void _capturePrevSnapshot()
   {
      if (_count == 0)
      {
         _prevCount = 0;
         return;
      }

      memcpy(_prevValues, _values, _count * sizeof(float));
      memcpy(_prevAgesMs, _agesMs, _count * sizeof(unsigned long));
      memcpy(_prevMovingAverageBuffer, _movingAverageBuffer, _count * sizeof(float));
      _prevCount = _count;
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
      _recomputeMovingAverage();
   }

   ///
   /// <summary>
   /// Recomputes the centered moving average over the current snapshot. Each output
   /// value is the average of every retained sample whose age falls within
   /// movingAverageWindowMs/2 of that sample's own age. Since the snapshot is a fixed
   /// point-in-time view (no further samples will ever be added to it), every index can
   /// be fully computed immediately; averages naturally get noisier near the edges of the
   /// retained window where fewer neighboring samples exist.
   /// </summary>
   ///
   void _recomputeMovingAverage()
   {
      if (_count == 0)
      {
         return;
      }

      // _agesMs is newest-first (smallest age at index 0, largest age at the end).
      const float halfWindow = movingAverageWindowMs / 2.0f;

      for (size_t i = 0; i < _count; i++)
      {
         const unsigned long centerAge = _agesMs[i];
         float sum = 0.0f;
         size_t finiteCount = 0;

         // Fold in samples that are the same age or older (index >= i).
         for (size_t j = i; j < _count; j++)
         {
            if ((static_cast<float>(_agesMs[j]) - static_cast<float>(centerAge)) > halfWindow)
            {
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

         _movingAverageBuffer[i] = (finiteCount > 0) ? (sum / finiteCount) : NAN;
      }
   }

public:
   ///
   /// <summary>If true, draws each sample of this series.</summary>
   ///
   bool showPoints = true;

   ///
   /// <summary>If true, connects each drawn sample of this series to the previous one with a line.</summary>
   ///
   bool showLines = false;

   ///
   /// <summary>If true, draws this series' centered moving average as a connected line.</summary>
   ///
   bool showMovingAverage = false;

   ///
   /// <summary>Color used to draw this series' points and lines.</summary>
   ///
   Color color = Color::WHITE;

   ///
   /// <summary>Color used to draw this series' moving-average line.</summary>
   ///
   Color movingAverageColor = Color::YELLOW;

   ///
   /// <summary>Width of the centered moving-average window, in milliseconds.</summary>
   ///
   float movingAverageWindowMs = 0.0f;

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
      : _bins(historyMs, numBins)
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
      delete[] _prevValues;
      delete[] _prevAgesMs;
      delete[] _prevMovingAverageBuffer;
   }

   ///
   /// <summary>
   /// Adds a new value to the series, accumulating it into the currently-open time bin
   /// (rotating out expired bins first as needed).
   /// </summary>
   /// <param name="value">Value to add.</param>
   ///
   void add(float value)
   {
      _bins.add(value);
   }

   ///
   /// <summary>
   /// Removes every retained sample from this series.
   /// </summary>
   ///
   void clear()
   {
      _bins.reset();
      _count = 0;
      _prevCount = 0;
   }

   ///
   /// <summary>
   /// Gets the number of samples currently retained (as of the last render() snapshot).
   /// </summary>
   /// <returns>Number of retained samples.</returns>
   ///
   size_t getCount() const { return _count; }
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
class TimedScatterPlot
{
private:
   ArduinoWithDisplay* _display;
   Rect16 _rect;
   unsigned long _historyMs;
   float _minValueStep;

   Format _minMaxFormat;
   Format _sampleRangeFormat;

   // Optional centered title drawn above the chart; when set, the chart area is reduced
   // to make room for it.
   String _title;

   int16_t _chartLeft = 0;
   int16_t _chartTop = 0;
   int16_t _chartWidth = 0;
   int16_t _chartHeight = 0;
   float _axisYMin = 0.0f;
   float _axisYMax = 0.0f;
   bool _hasRenderedFrame = false;
   bool _forceFullRedraw = false;

   // Tracks the ramp-up period before the visible history window has actually filled with
   // data, so points start at the left edge and fill in new columns to the right as data
   // accumulates, instead of immediately being squeezed against the right edge of a
   // full-width window. Once elapsed time reaches historyMs, normal right-anchored
   // scrolling behavior takes over.
   bool _hasRampStart = false;
   unsigned long _rampStartMs = 0;
   unsigned long _rampElapsedMs = 0;
   bool _isRampingUp = true;

   TimedScatterPlotSeries** _series = nullptr;
   size_t _seriesCount = 0;
   size_t _seriesCapacity = 0;

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
   /// Computes the chart's pixel geometry from the current axis range, sizing the Y-axis
   /// label column to fit the widest of the min/max labels, within the fixed rectangle.
   /// </summary>
   ///
   void _computeChartGeometry()
   {
      String maxLabel = _formatYLabel(_axisYMax);
      String minLabel = _formatYLabel(_axisYMin);
      uint16_t yLabelWidth = max(_display->textWidth(maxLabel.c_str()), _display->textWidth(minLabel.c_str()));
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
         _display->print(_title, Color::LABEL);
      }

      _display->fillRect(_chartLeft, _chartTop + _chartHeight - 1, _chartWidth, 1, Color::GRAY);
      _display->fillRect(_chartLeft, _chartTop, 1, _chartHeight, Color::GRAY);

      String maxLabel = _formatYLabel(_axisYMax);
      int16_t maxLabelX = _chartLeft - 2 - static_cast<int16_t>(_display->textWidth(maxLabel.c_str()));
      _display->setCursor(maxLabelX, _chartTop);
      _display->print(maxLabel, Color::LABEL);

      String minLabel = _formatYLabel(_axisYMin);
      int16_t minLabelX = _chartLeft - 2 - static_cast<int16_t>(_display->textWidth(minLabel.c_str()));
      _display->setCursor(minLabelX, _chartTop + _chartHeight - _display->charH());
      _display->print(minLabel, Color::LABEL);

      String rangeLabel = _formatHistoryRangeLabel();
      int16_t rangeLabelX = _chartLeft + (_chartWidth - static_cast<int16_t>(_display->textWidth(rangeLabel.c_str()))) / 2;
      _display->setCursor(max(_chartLeft, rangeLabelX), _chartTop + _chartHeight + 1);
      _display->print(rangeLabel, Color::LABEL);
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
      float value;
      const char* suffix;

      if (_historyMs < 2000)
      {
         value = static_cast<float>(_historyMs);
         suffix = " ms";
      }
      else if (_historyMs < 120000)
      {
         value = static_cast<float>(_historyMs) / 1000.0f;
         suffix = " s";
      }
      else
      {
         value = static_cast<float>(_historyMs) / 60000.0f;
         suffix = " m";
      }

      std::string pattern = std::string("####") + suffix;
      Format format(pattern.c_str(), _sampleRangeFormat.alignment());
      return String(format.toString(value).c_str());
   }

   ///
   /// <summary>
   /// Converts an age-in-milliseconds/value pair to pixel coordinates using the current
   /// chart geometry and axis range. Newest samples (age 0) map to the right edge; samples
   /// at or beyond historyMs map to the left edge.
   /// </summary>
   /// <param name="ageMs">Age of the sample in milliseconds.</param>
   /// <param name="value">Y value to convert.</param>
   /// <param name="outX">Receives the pixel X coordinate.</param>
   /// <param name="outY">Receives the pixel Y coordinate.</param>
   ///
   void _toPixel(unsigned long ageMs, float value, int16_t& outX, int16_t& outY) const
   {
      float ySpan = _axisYMax - _axisYMin;
      if (ySpan <= 0.0f) ySpan = 1.0f;

      float fraction;
      if (_isRampingUp)
      {
         // Anchor each sample to its absolute time-since-ramp-start (instead of its age
         // relative to "now"), so already-placed points stay put and new samples simply
         // appear in new columns further to the right as time passes.
         unsigned long sampleElapsedMs = (ageMs < _rampElapsedMs) ? (_rampElapsedMs - ageMs) : 0;
         fraction = static_cast<float>(sampleElapsedMs) / static_cast<float>(_historyMs);
      }
      else
      {
         unsigned long boundedAgeMs = min(ageMs, _historyMs);
         fraction = 1.0f - (static_cast<float>(boundedAgeMs) / static_cast<float>(_historyMs));
      }
      fraction = constrain(fraction, 0.0f, 1.0f);

      outX = _chartLeft + static_cast<int16_t>(fraction * (_chartWidth - 1));
      outY = _chartTop + static_cast<int16_t>(((_axisYMax - value) / ySpan) * (_chartHeight - 1));

      outX = constrain(outX, _chartLeft, static_cast<int16_t>(_chartLeft + _chartWidth - 1));
      outY = constrain(outY, _chartTop, static_cast<int16_t>(_chartTop + _chartHeight - 1));
   }

   ///
   /// <summary>
   /// Plots a set of samples (connected with lines when connectPoints is set) using the
   /// given color, aligning to the sensor step first when alignToStep is set. Samples must
   /// be supplied newest-first but are drawn oldest-first so lines connect in chronological
   /// order. Used both to draw the current frame and, with color set to black, to erase the
   /// previous frame's exact strokes before drawing over them.
   /// </summary>
   /// <param name="values">Newest-first sample values to plot.</param>
   /// <param name="agesMs">Newest-first sample ages (parallel to values), in milliseconds.</param>
   /// <param name="count">Number of samples in values/agesMs.</param>
   /// <param name="connectPoints">If true, connects consecutive finite samples with a line.</param>
   /// <param name="drawPoints">If true, draws each finite sample as a pixel.</param>
   /// <param name="alignToStep">If true, aligns each value to the configured sensor step before plotting.</param>
   /// <param name="color">Color to draw with.</param>
   ///
   void _plotSeriesData(const float* values, const unsigned long* agesMs, size_t count, bool connectPoints, bool drawPoints, bool alignToStep, Color color) const
   {
      if (count == 0)
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
         _toPixel(agesMs[idx], value, x, y);

         if (connectPoints && havePrevPoint)
         {
            _display->drawLine(prevX, prevY, x, y, color);
         }
         if (drawPoints)
         {
            _display->drawPixel(x, y, color);
         }

         prevX = x;
         prevY = y;
         havePrevPoint = true;
      }
   }

public:
   ///
   /// <summary>
   /// Creates a timed scatter plot renderer that draws within the given fixed screen
   /// rectangle, using a default min/max axis label format (one decimal place,
   /// right-aligned) and a default sample-range alignment (center-aligned). The X axis
   /// range label automatically switches units based on the history window: milliseconds
   /// below 2 seconds, seconds below 2 minutes, and minutes otherwise.
   /// </summary>
   /// <param name="display">Pointer to the display object.</param>
   /// <param name="rect">Fixed screen rectangle this plot draws within; unchanged for the plot's lifetime.</param>
   /// <param name="historyMs">Time window in milliseconds for the visible history.</param>
   /// <param name="minValueStep">Optional sensor resolution step size (0.0 for no alignment, default 0.0).</param>
   /// <param name="title">Optional centered title drawn above the chart; defaults to none, in which case the plot uses the full rectangle.</param>
   ///
   TimedScatterPlot(ArduinoWithDisplay* display, Rect16 rect, unsigned long historyMs, float minValueStep = 0.0f, const String& title = String())
      : TimedScatterPlot(display, rect, historyMs, Format("##.#", Format::Alignment::RIGHT), Format("##.#s", Format::Alignment::CENTER), minValueStep, title)
   {}

   ///
   /// <summary>
   /// Creates a timed scatter plot renderer with a custom min/max axis label format and a
   /// custom sample-range format (only its alignment is used; the X axis range label's
   /// units and precision are chosen automatically based on the history window).
   /// </summary>
   ///
   TimedScatterPlot(ArduinoWithDisplay* display, Rect16 rect, unsigned long historyMs, const Format& minMaxFormat, const Format& sampleRangeFormat, float minValueStep = 0.0f, const String& title = String())
      : _display(display), _rect(rect), _historyMs((historyMs == 0) ? 1 : historyMs), _minValueStep(minValueStep),
        _minMaxFormat(minMaxFormat), _sampleRangeFormat(sampleRangeFormat), _title(title)
   {
   }

   TimedScatterPlot(const TimedScatterPlot&) = delete;
   TimedScatterPlot& operator=(const TimedScatterPlot&) = delete;

   ~TimedScatterPlot()
   {
      for (size_t i = 0; i < _seriesCount; i++)
      {
         delete _series[i];
      }
      delete[] _series;
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
   TimedScatterPlotSeries* createSeries(size_t numBins = 0)
   {
      if (_seriesCount >= _seriesCapacity)
      {
         size_t newCapacity = _seriesCapacity + 5;
         TimedScatterPlotSeries** newSeries = new (std::nothrow) TimedScatterPlotSeries*[newCapacity];

         if (newSeries == nullptr)
         {
            Util::setHaltReason("OOM allocating series array in TimedScatterPlot");
            Util::reset();
            return nullptr;
         }

         if (_seriesCount > 0)
         {
            memcpy(newSeries, _series, _seriesCount * sizeof(TimedScatterPlotSeries*));
         }

         delete[] _series;
         _series = newSeries;
         _seriesCapacity = newCapacity;
      }

      size_t effectiveNumBins = (numBins > 0) ? numBins : static_cast<size_t>(max<int16_t>(1, static_cast<int16_t>(_rect.width)));
      TimedScatterPlotSeries* newSeries = new TimedScatterPlotSeries(_historyMs, effectiveNumBins);
      _series[_seriesCount++] = newSeries;
      return newSeries;
   }

   ///
   /// <summary>
   /// Gets a series by index.
   /// </summary>
   /// <param name="index">Index of the series to retrieve.</param>
   /// <returns>Pointer to the series at the specified index, or nullptr if index is invalid.</returns>
   ///
   TimedScatterPlotSeries* getSeries(size_t index)
   {
      return (index < _seriesCount) ? _series[index] : nullptr;
   }

   ///
   /// <summary>
   /// Gets the number of series currently managed by this plot.
   /// </summary>
   /// <returns>Number of series.</returns>
   ///
   size_t getSeriesCount() const
   {
      return _seriesCount;
   }

   ///
   /// <summary>
   /// Deletes a series at the specified index.
   /// </summary>
   /// <param name="index">Index of the series to delete.</param>
   ///
   void deleteSeries(size_t index)
   {
      if (index < _seriesCount)
      {
         delete _series[index];

         for (size_t i = index; i < _seriesCount - 1; i++)
         {
            _series[i] = _series[i + 1];
         }

         _seriesCount--;
      }
   }

   ///
   /// <summary>
   /// Deletes all series managed by this plot.
   /// </summary>
   ///
   void deleteAllSeries()
   {
      for (size_t i = 0; i < _seriesCount; i++)
      {
         delete _series[i];
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
   /// Sets a custom format for axis min/max labels.
   /// </summary>
   /// <param name="format">Format object defining precision and alignment.</param>
   ///
   void setMinMaxFormat(const Format& format)
   {
      _minMaxFormat = format;
   }

   ///
   /// <summary>
   /// Sets a custom alignment for the sample range time display. The units and precision
   /// of the label itself are always chosen automatically based on the history window;
   /// only the alignment of this format is used.
   /// </summary>
   /// <param name="format">Format object whose alignment is used for the sample range label.</param>
   ///
   void setSampleRangeFormat(const Format& format)
   {
      _sampleRangeFormat = format;
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
   /// Forces the next render() call to perform a full clear and redraw of the chart area,
   /// even if the computed axis range has not changed.
   /// </summary>
   ///
   void invalidate()
   {
      _forceFullRedraw = true;
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
   /// series' points, lines, and/or moving-average line. When the axis range and chart
   /// geometry are unchanged from the previous frame, the chart interior is not cleared;
   /// instead, each series' previously drawn strokes are erased in black before the new
   /// frame is drawn, avoiding the flicker a full clear would cause. A full clear and
   /// redraw still happens whenever the axis range or geometry changes, invalidate() was
   /// called, or this is the first frame.
   /// </summary>
   ///
   void render()
   {
      const unsigned long frameStartMicros = micros();

      if (_display == nullptr || _seriesCount == 0)
      {
         return;
      }

      const unsigned long nowMs = millis();
      if (!_hasRampStart)
      {
         _rampStartMs = nowMs;
         _hasRampStart = true;
      }
      _rampElapsedMs = nowMs - _rampStartMs;
      const bool wasRampingUp = _isRampingUp;
      _isRampingUp = (_rampElapsedMs < _historyMs);

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
      if (yMax <= yMin)
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

      bool fullRedraw = _forceFullRedraw || !_hasRenderedFrame || wasRampingUp
         || (yMin != oldAxisYMin) || (yMax != oldAxisYMax)
         || (_chartLeft != oldChartLeft) || (_chartTop != oldChartTop)
         || (_chartWidth != oldChartWidth) || (_chartHeight != oldChartHeight);

      _forceFullRedraw = false;
      _hasRenderedFrame = true;

      const unsigned long displayStartMicros = micros();

      _display->display.startWrite();

      if (fullRedraw)
      {
         _display->fillRect(_chartLeft + 1, _chartTop, _chartWidth - 1, _chartHeight - 1, Color::BLACK);
         _drawAxes();
      }

      for (size_t i = 0; i < _seriesCount; i++)
      {
         TimedScatterPlotSeries* series = _series[i];

         if (!fullRedraw)
         {
            // Erase exactly what was drawn last frame for this series before drawing the new frame.
            if (series->showPoints || series->showLines)
            {
               _plotSeriesData(series->_prevValues, series->_prevAgesMs, series->_prevCount, series->showLines, series->showPoints, true, Color::BLACK);
            }
            if (series->showMovingAverage)
            {
               _plotSeriesData(series->_prevMovingAverageBuffer, series->_prevAgesMs, series->_prevCount, true, false, false, Color::BLACK);
            }
         }

         if (series->showPoints || series->showLines)
         {
            _plotSeriesData(series->_values, series->_agesMs, series->_count, series->showLines, series->showPoints, true, series->color);
         }

         if (series->showMovingAverage)
         {
            _plotSeriesData(series->_movingAverageBuffer, series->_agesMs, series->_count, true, false, false, series->movingAverageColor);
         }

         series->_capturePrevSnapshot();
      }

      _display->display.endWrite();

      _lastDisplayMicros = micros() - displayStartMicros;
      _lastRenderMicros = micros() - frameStartMicros;
      _lastComputeMicros = _lastRenderMicros - _lastDisplayMicros;
   }
};
