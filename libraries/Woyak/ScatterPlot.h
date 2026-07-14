#pragma once

#include "ArduinoWithDisplay.h"
#include "Color.h"
#include "Format.h"
#include "Util.h"
#include <math.h>
#include <new>

class ScatterPlot;

///
/// <summary>
/// A single data series owned and rendered by a ScatterPlot: stores its own (x, y)
/// points, display flags (points, connected lines, moving-average line), and color.
/// Also owns a centered moving-average of its own points, computed on demand from
/// movingSampleSize (in the same units as the x values) and cached until new points
/// are added. Set finalized once no more points will be added so the moving average's
/// trailing samples (which need future points to center on) are computed from whatever
/// data actually exists instead of being left pending.
/// </summary>
///
class ScatterPlotSeries
{
   friend class ScatterPlot;

private:
   float* _x = nullptr;
   float* _y = nullptr;
   size_t _count = 0;
   size_t _capacity = 0;

   // Scratch buffer holding the most recently computed centered moving average, recomputed
   // on demand (and cached until add()/set() invalidates it) from the raw points, movingSampleSize,
   // and finalized.
   float* _movingAverageBuffer = nullptr;
   size_t _movingAverageBufferCapacity = 0;
   size_t _movingAverageReadyCount = 0;
   bool _movingAverageDirty = true;

   // Tracks how much of this series has already been drawn, so ScatterPlot::render() can
   // plot only newly added points instead of redrawing the whole series every frame.
   size_t _renderedCount = 0;
   size_t _movingAverageRenderedCount = 0;

   ///
   /// <summary>
   /// Ensures the internal moving-average scratch buffer can hold at least the given
   /// number of samples, growing (and never shrinking) it as needed.
   /// </summary>
   /// <param name="count">Minimum required buffer capacity.</param>
   /// <returns>True if the buffer has at least the requested capacity.</returns>
   ///
   bool _ensureMovingAverageBuffer(size_t count)
   {
      if (count <= _movingAverageBufferCapacity)
      {
         return _movingAverageBuffer != nullptr;
      }

      delete[] _movingAverageBuffer;
      _movingAverageBuffer = new (std::nothrow) float[count];
      _movingAverageBufferCapacity = (_movingAverageBuffer != nullptr) ? count : 0;

      return _movingAverageBuffer != nullptr;
   }

   ///
   /// <summary>
   /// Recomputes the centered moving average into the internal scratch buffer if it has
   /// been invalidated since the last computation. Each output value is the average of
   /// every point whose x falls within movingSampleSize/2 of that point's own x,
   /// blending data from both before and after it. Because centering a point requires
   /// points that arrive after it, a point can only be finalized once enough later points
   /// actually exist; unless finalized is true, trailing points are left pending (NAN).
   /// </summary>
   ///
   void _recomputeMovingAverage()
   {
      if (!_movingAverageDirty)
      {
         return;
      }

      if (_count == 0 || !_ensureMovingAverageBuffer(_count))
      {
         _movingAverageReadyCount = 0;
         _movingAverageDirty = false;
         return;
      }

      float halfWindow = movingSampleSize / 2.0f;

      size_t lo = 0;
      size_t hi = 0;
      float sum = 0.0f;
      size_t finiteCount = 0;
      size_t readyCount = 0;

      for (size_t i = 0; i < _count; i++)
      {
         // Evict points that have aged out of the trailing (past) edge of the window.
         while (lo < _count && (_x[i] - _x[lo]) > halfWindow)
         {
            if (isfinite(_y[lo]))
            {
               sum -= _y[lo];
               finiteCount--;
            }
            lo++;
         }

         // Fold in points up to the leading (future) edge of the window.
         while (hi < _count && (_x[hi] - _x[i]) <= halfWindow)
         {
            if (isfinite(_y[hi]))
            {
               sum += _y[hi];
               finiteCount++;
            }
            hi++;
         }

         // The window is only known to be fully populated once a point beyond its future
         // edge has actually been seen (hi < _count); otherwise more data may still arrive
         // that belongs inside it, unless the series has been marked finalized.
         if (!finalized && (hi >= _count))
         {
            break;
         }

         _movingAverageBuffer[i] = (finiteCount > 0) ? (sum / finiteCount) : NAN;
         readyCount = i + 1;
      }

      for (size_t i = readyCount; i < _count; i++)
      {
         _movingAverageBuffer[i] = NAN;
      }

      _movingAverageReadyCount = readyCount;
      _movingAverageDirty = false;
   }

   ///
   /// <summary>
   /// Gets how many leading points have already been drawn by ScatterPlot::render(),
   /// so only newly added points need to be plotted.
   /// </summary>
   /// <returns>Number of already-rendered points.</returns>
   ///
   size_t renderedCount() const { return _renderedCount; }

   ///
   /// <summary>
   /// Records how many leading points have now been drawn by ScatterPlot::render().
   /// </summary>
   /// <param name="count">Number of points now rendered.</param>
   ///
   void setRenderedCount(size_t count) { _renderedCount = count; }

   ///
   /// <summary>
   /// Gets how many leading moving-average points have already been drawn by
   /// ScatterPlot::render().
   /// </summary>
   /// <returns>Number of already-rendered moving-average points.</returns>
   ///
   size_t movingAverageRenderedCount() const { return _movingAverageRenderedCount; }

   ///
   /// <summary>
   /// Records how many leading moving-average points have now been drawn by
   /// ScatterPlot::render().
   /// </summary>
   /// <param name="count">Number of moving-average points now rendered.</param>
   ///
   void setMovingAverageRenderedCount(size_t count) { _movingAverageRenderedCount = count; }

   ///
   /// <summary>
   /// Resets incremental redraw tracking so the next render() redraws this series from
   /// scratch (e.g. after the shared chart was fully cleared).
   /// </summary>
   ///
   void resetRenderState()
   {
      _renderedCount = 0;
      _movingAverageRenderedCount = 0;
   }

public:
   ///
   /// <summary>If true, draws each point of this series.</summary>
   ///
   bool showPoints = true;

   ///
   /// <summary>If true, connects each drawn point of this series to the previous one with a line.</summary>
   ///
   bool showLines = false;

   ///
   /// <summary>If true, draws this series' centered moving average as a connected line.</summary>
   ///
   bool showMovingAverage = false;

   ///
   /// <summary>Color used to draw this series' points, lines, and moving average.</summary>
   ///
   Color color = Color::GREEN;

   ///
   /// <summary>Width of the centered moving-average window, in the same units as the x values (sample count or time period).</summary>
   ///
   float movingSampleSize = 0.0f;

   ///
   /// <summary>
   /// Set once no more points will be added to this series, so the moving average's
   /// trailing points are computed from whatever surrounding data actually exists instead
   /// of being left pending.
   /// </summary>
   ///
   bool finalized = false;

   ///
   /// <summary>
   /// Constructs a new ScatterPlotSeries with the specified initial capacity.
   /// </summary>
   /// <param name="capacity">Initial capacity for the data arrays.</param>
   ///
   explicit ScatterPlotSeries(size_t capacity = 10)
      : _capacity(capacity)
   {
      if (capacity > 0)
      {
         _x = new (std::nothrow) float[capacity];
         _y = new (std::nothrow) float[capacity];

         if (_x == nullptr || _y == nullptr)
         {
            Util::setHaltReason("OOM allocating points in ScatterPlotSeries");
            Util::reset();
         }
      }
   }

   ScatterPlotSeries(const ScatterPlotSeries&) = delete;
   ScatterPlotSeries& operator=(const ScatterPlotSeries&) = delete;

   ///
   /// <summary>
   /// Destructor that frees allocated memory.
   /// </summary>
   ///
   ~ScatterPlotSeries()
   {
      delete[] _x;
      delete[] _y;
      delete[] _movingAverageBuffer;
   }

   ///
   /// <summary>
   /// Adds a new point to the series, growing its storage if needed. x values are
   /// expected to be non-decreasing since the moving average relies on that ordering.
   /// </summary>
   /// <param name="x">X coordinate value.</param>
   /// <param name="y">Y coordinate value.</param>
   ///
   void add(float x, float y)
   {
      if (_count >= _capacity)
      {
         size_t newCapacity = _capacity + (_capacity / 2) + 1;
         float* newX = new (std::nothrow) float[newCapacity];
         float* newY = new (std::nothrow) float[newCapacity];

         if (newX == nullptr || newY == nullptr)
         {
            delete[] newX;
            delete[] newY;
            Util::setHaltReason("OOM allocating points in ScatterPlotSeries");
            Util::reset();
            return;
         }

         if (_count > 0)
         {
            memcpy(newX, _x, _count * sizeof(float));
            memcpy(newY, _y, _count * sizeof(float));
         }

         delete[] _x;
         delete[] _y;
         _x = newX;
         _y = newY;
         _capacity = newCapacity;
      }

      _x[_count] = x;
      _y[_count] = y;
      _count++;
      _movingAverageDirty = true;
   }

   ///
   /// <summary>
   /// Sets the value at a specific index.
   /// </summary>
   /// <param name="index">Index to set.</param>
   /// <param name="x">X coordinate value.</param>
   /// <param name="y">Y coordinate value.</param>
   ///
   void set(size_t index, float x, float y)
   {
      if (index < _count)
      {
         _x[index] = x;
         _y[index] = y;
         _movingAverageDirty = true;
      }
   }

   ///
   /// <summary>
   /// Removes every point from the series and resets its moving-average cache and
   /// incremental redraw tracking, so the series can be reused (e.g. for a new test run).
   /// Does not reset showPoints, showLines, showMovingAverage, color, or movingSampleSize.
   /// </summary>
   ///
   void clear()
   {
      _count = 0;
      _movingAverageReadyCount = 0;
      _movingAverageDirty = true;
      _renderedCount = 0;
      _movingAverageRenderedCount = 0;
      finalized = false;
   }

   ///
   /// <summary>
   /// Gets the X value at the specified index.
   /// </summary>
   /// <param name="index">Index to retrieve.</param>
   /// <returns>X coordinate value at the index, or 0 if the index is out of range.</returns>
   ///
   float getX(size_t index) const
   {
      return (index < _count) ? _x[index] : 0.0f;
   }

   ///
   /// <summary>
   /// Gets the Y value at the specified index.
   /// </summary>
   /// <param name="index">Index to retrieve.</param>
   /// <returns>Y coordinate value at the index, or 0 if the index is out of range.</returns>
   ///
   float getY(size_t index) const
   {
      return (index < _count) ? _y[index] : 0.0f;
   }

   ///
   /// <summary>
   /// Gets the current number of points in the series.
   /// </summary>
   /// <returns>Number of points.</returns>
   ///
   size_t getCount() const { return _count; }

   ///
   /// <summary>
   /// Gets how many leading points of the centered moving average are finalized (safe to
   /// use), recomputing it first if any points have been added or set since the last call.
   /// </summary>
   /// <returns>Number of finalized leading moving-average points.</returns>
   ///
   size_t getMovingAverageReadyCount()
   {
      _recomputeMovingAverage();
      return _movingAverageReadyCount;
   }

   ///
   /// <summary>
   /// Gets the centered moving-average value at the specified raw point index,
   /// recomputing it first if any points have been added or set since the last call.
   /// </summary>
   /// <param name="index">Raw point index (parallel to getX()/getY()).</param>
   /// <returns>The centered moving-average value, or NAN if not yet finalized or out of range.</returns>
   ///
   float getMovingAverageY(size_t index)
   {
      _recomputeMovingAverage();
      return (index < _movingAverageReadyCount) ? _movingAverageBuffer[index] : NAN;
   }

   ///
   /// <summary>
   /// Computes the peak (maximum) value of the series' centered moving average.
   /// </summary>
   /// <returns>The maximum finite moving-average value, or NAN if none exists.</returns>
   ///
   float findMovingAveragePeak()
   {
      size_t readyCount = getMovingAverageReadyCount();

      float peak = NAN;
      for (size_t i = 0; i < readyCount; i++)
      {
         float value = _movingAverageBuffer[i];
         if (isfinite(value) && (!isfinite(peak) || (value > peak)))
         {
            peak = value;
         }
      }
      return peak;
   }

   ///
   /// <summary>
   /// Computes the min/max range of the series' centered moving average.
   /// </summary>
   /// <param name="outMin">Receives the minimum finite value found, or NAN if none exists.</param>
   /// <param name="outMax">Receives the maximum finite value found, or NAN if none exists.</param>
   ///
   void movingAverageRange(float* outMin, float* outMax)
   {
      size_t readyCount = getMovingAverageReadyCount();

      float minValue = NAN;
      float maxValue = NAN;
      for (size_t i = 0; i < readyCount; i++)
      {
         float value = _movingAverageBuffer[i];
         if (!isfinite(value))
         {
            continue;
         }
         if (!isfinite(minValue) || (value < minValue)) minValue = value;
         if (!isfinite(maxValue) || (value > maxValue)) maxValue = value;
      }

      *outMin = minValue;
      *outMax = maxValue;
   }
};

///
/// <summary>
/// Renders one or more ScatterPlotSeries on the display as a shared-axis scatter plot.
/// Callers create series via createSeries(), populate them by calling add() as data
/// becomes available, and set each series' display flags (showPoints, showLines,
/// showMovingAverage) and color. Calling render() scans every owned series, automatically
/// computes the shared X/Y axis range to fit whatever is currently displayed, and draws
/// axis labels plus each series' points/lines/moving-average line. A full clear and
/// redraw only happens when the axis range changes or invalidate() was called since the
/// last render(); otherwise only newly added points are plotted, so render() can be
/// called every frame while data is still being collected.
/// </summary>
///
class ScatterPlot
{
private:
   static constexpr int16_t Y_AXIS_LABEL_GAP = 2;
   static constexpr uint8_t AXIS_LABEL_SIGNIFICANT_DIGITS = 3;

   ArduinoWithDisplay* _display;
   int16_t _x;
   int16_t _y;
   int16_t _width;
   int16_t _height;
   int16_t _yAxisWidth = 50;

   // Chart geometry and shared axis range established by the most recent full redraw.
   int16_t _chartLeft = 0;
   int16_t _chartTop = 0;
   int16_t _chartWidth = 0;
   int16_t _chartHeight = 0;
   float _axisXMin = 0.0f;
   float _axisXMax = 0.0f;
   float _axisYMin = 0.0f;
   float _axisYMax = 0.0f;
   bool _hasRenderedFrame = false;
   bool _forceFullRedraw = false;

   // Series management
   ScatterPlotSeries** _series = nullptr;
   size_t _seriesCount = 0;
   size_t _seriesCapacity = 0;

   // Axis format customization
   const char* _xAxisFormat = nullptr;
   const char* _yAxisFormat = nullptr;

   // Optional initial Y-axis range: the plot never shrinks below this range, but will
   // grow beyond it if the data requires a wider range.
   float _initialYMin = NAN;
   float _initialYMax = NAN;

   // Optional centered title drawn above the chart; when set, the chart area is reduced
   // to make room for it.
   String _title;

   ///
   /// <summary>
   /// Scans every owned series and computes the shared X/Y axis range needed to fit
   /// whatever is currently displayed: a series' raw points contribute to the Y range
   /// only if showPoints or showLines is set, and its moving average contributes only if
   /// showMovingAverage is set, mirroring exactly what render() will go on to draw.
   /// </summary>
   /// <param name="outXMin">Receives the minimum finite X value found, or NAN if none exists.</param>
   /// <param name="outXMax">Receives the maximum finite X value found, or NAN if none exists.</param>
   /// <param name="outYMin">Receives the minimum finite Y value found, or NAN if none exists.</param>
   /// <param name="outYMax">Receives the maximum finite Y value found, or NAN if none exists.</param>
   ///
   void _computeAxisRange(float* outXMin, float* outXMax, float* outYMin, float* outYMax) const
   {
      float xMin = NAN;
      float xMax = NAN;
      float yMin = NAN;
      float yMax = NAN;

      for (size_t s = 0; s < _seriesCount; s++)
      {
         ScatterPlotSeries* series = _series[s];
         size_t count = series->getCount();
         bool includeRaw = series->showPoints || series->showLines;

         for (size_t i = 0; i < count; i++)
         {
            float x = series->getX(i);
            if (isfinite(x))
            {
               if (!isfinite(xMin) || (x < xMin)) xMin = x;
               if (!isfinite(xMax) || (x > xMax)) xMax = x;
            }

            if (includeRaw)
            {
               float y = series->getY(i);
               if (isfinite(y))
               {
                  if (!isfinite(yMin) || (y < yMin)) yMin = y;
                  if (!isfinite(yMax) || (y > yMax)) yMax = y;
               }
            }
         }

         if (series->showMovingAverage)
         {
            float maMin;
            float maMax;
            series->movingAverageRange(&maMin, &maMax);
            if (isfinite(maMin) && (!isfinite(yMin) || (maMin < yMin))) yMin = maMin;
            if (isfinite(maMax) && (!isfinite(yMax) || (maMax > yMax))) yMax = maMax;
         }
      }

      *outXMin = xMin;
      *outXMax = xMax;
      *outYMin = isfinite(_initialYMin) ? ((!isfinite(yMin) || (_initialYMin < yMin)) ? _initialYMin : yMin) : yMin;
      *outYMax = isfinite(_initialYMax) ? ((!isfinite(yMax) || (_initialYMax > yMax)) ? _initialYMax : yMax) : yMax;
   }

   ///
   /// <summary>
   /// Formats a Y-axis label value using the custom Y-axis format if one has been set
   /// via setYAxisFormat(), otherwise falling back to significant-digit formatting. Used
   /// by both chart geometry computation and axis-label drawing so the reserved label
   /// column width always matches what is actually drawn.
   /// </summary>
   /// <param name="value">Y-axis value to format.</param>
   /// <returns>Formatted label text.</returns>
   ///
   String _formatYLabel(float value) const
   {
      if (_yAxisFormat != nullptr)
      {
         Format fmt(_yAxisFormat, Format::Alignment::RIGHT);
         return String(fmt.toString(value).c_str());
      }
      return Util::toSignificantString(value, AXIS_LABEL_SIGNIFICANT_DIGITS);
   }

   ///
   /// <summary>
   /// Computes the chart's pixel geometry from the current axis range, sizing the Y-axis
   /// label column to fit the widest of the min/max labels.
   /// </summary>
   /// <param name="yMin">Minimum value of the shared Y-axis range.</param>
   /// <param name="yMax">Maximum value of the shared Y-axis range.</param>
   ///
   void _computeChartGeometry(float yMin, float yMax)
   {
      String maxLabel = _formatYLabel(yMax);
      String minLabel = _formatYLabel(yMin);
      uint16_t yLabelWidth = max(_display->textWidth(maxLabel.c_str()), _display->textWidth(minLabel.c_str()));
      _yAxisWidth = static_cast<int16_t>(yLabelWidth) + Y_AXIS_LABEL_GAP;

      int16_t axisLineHeight = _display->charH() + 2;
      int16_t titlePadding = _display->charH() / 4;
      int16_t titleHeight = _title.length() > 0 ? (_display->charH() + 2 * titlePadding) : 0;

      _chartLeft = _x + _yAxisWidth;
      _chartTop = _y + titleHeight;
      _chartWidth = _width - _yAxisWidth;
      _chartHeight = _height - axisLineHeight - titleHeight;
   }

   ///
   /// <summary>
   /// Draws the gray X/Y axis lines and the auto-formatted min/max axis labels for the
   /// current chart geometry and axis range.
   /// </summary>
   ///
   void _drawAxes() const
   {
      if (_title.length() > 0)
      {
         int16_t titleX = _chartLeft + (_chartWidth - static_cast<int16_t>(_display->textWidth(_title.c_str()))) / 2;
         int16_t titlePadding = _display->charH() / 4;
         _display->setCursor(max(_chartLeft, titleX), _y + titlePadding);
         _display->print(_title, Color::LABEL);
      }

      _display->fillRect(_chartLeft, _chartTop + _chartHeight - 1, _chartWidth, 1, Color::GRAY);
      _display->fillRect(_chartLeft, _chartTop, 1, _chartHeight, Color::GRAY);

      String maxLabel = _formatYLabel(_axisYMax);
      int16_t maxLabelX = _chartLeft - Y_AXIS_LABEL_GAP - static_cast<int16_t>(_display->textWidth(maxLabel.c_str()));
      _display->setCursor(maxLabelX, _chartTop);
      _display->print(maxLabel, Color::LABEL);

      String minLabel = _formatYLabel(_axisYMin);
      int16_t minLabelX = _chartLeft - Y_AXIS_LABEL_GAP - static_cast<int16_t>(_display->textWidth(minLabel.c_str()));
      _display->setCursor(minLabelX, _chartTop + _chartHeight - _display->charH());
      _display->print(minLabel, Color::LABEL);

      String xMinLabel;
      if (_xAxisFormat != nullptr)
      {
         Format fmt(_xAxisFormat);
         xMinLabel = fmt.toString(_axisXMin).c_str();
      }
      else
      {
         xMinLabel = Util::toSignificantString(_axisXMin, AXIS_LABEL_SIGNIFICANT_DIGITS);
      }
      _display->setCursor(_chartLeft, _chartTop + _chartHeight + 1);
      _display->print(xMinLabel, Color::LABEL);

      String xMaxLabel;
      if (_xAxisFormat != nullptr)
      {
         Format fmt(_xAxisFormat, Format::Alignment::RIGHT);
         xMaxLabel = fmt.toString(_axisXMax).c_str();
      }
      else
      {
         xMaxLabel = Util::toSignificantString(_axisXMax, AXIS_LABEL_SIGNIFICANT_DIGITS);
      }
      int16_t xMaxLabelX = _chartLeft + _chartWidth - static_cast<int16_t>(_display->textWidth(xMaxLabel.c_str()));
      xMaxLabelX = max(_chartLeft, xMaxLabelX);
      _display->setCursor(xMaxLabelX, _chartTop + _chartHeight + 1);
      _display->print(xMaxLabel, Color::LABEL);
   }

   ///
   /// <summary>
   /// Converts a data point to pixel coordinates using the current chart geometry and
   /// axis range, clamping the result to stay within the chart area.
   /// </summary>
   /// <param name="x">X value to convert.</param>
   /// <param name="y">Y value to convert.</param>
   /// <param name="outX">Receives the pixel X coordinate.</param>
   /// <param name="outY">Receives the pixel Y coordinate.</param>
   ///
   void _toPixel(float x, float y, int16_t& outX, int16_t& outY) const
   {
      float xSpan = _axisXMax - _axisXMin;
      if (xSpan <= 0.0f) xSpan = 1.0f;
      float ySpan = _axisYMax - _axisYMin;
      if (ySpan <= 0.0f) ySpan = 1.0f;

      outX = _chartLeft + static_cast<int16_t>(((x - _axisXMin) / xSpan) * (_chartWidth - 1));
      outY = _chartTop + static_cast<int16_t>(((_axisYMax - y) / ySpan) * (_chartHeight - 1));

      outX = constrain(outX, _chartLeft, static_cast<int16_t>(_chartLeft + _chartWidth - 1));
      outY = constrain(outY, _chartTop, static_cast<int16_t>(_chartTop + _chartHeight - 1));
   }

   ///
   /// <summary>
   /// Plots either a series' raw points (connected with lines when showLines is set) or
   /// its moving-average line, starting at startIndex so previously plotted points are
   /// not redrawn. When connecting points, the line segment still reaches back to the
   /// point before startIndex so an incrementally plotted series has no gap.
   /// </summary>
   /// <param name="series">Series to plot.</param>
   /// <param name="startIndex">Index of the first point to plot.</param>
   /// <param name="useMovingAverage">If true, plots the moving-average values (always connected) instead of the raw points.</param>
   /// <returns>The number of points now available to plot (getCount() or getMovingAverageReadyCount()).</returns>
   ///
   size_t _plotSeriesData(ScatterPlotSeries* series, size_t startIndex, bool useMovingAverage) const
   {
      size_t count = useMovingAverage ? series->getMovingAverageReadyCount() : series->getCount();
      if (startIndex >= count)
      {
         return count;
      }

      bool connectPoints = useMovingAverage || series->showLines;
      Color color = series->color;

      bool havePrevPoint = false;
      int16_t prevX = 0;
      int16_t prevY = 0;

      if (connectPoints && (startIndex > 0))
      {
         size_t prevIndex = startIndex - 1;
         float prevValue = useMovingAverage ? series->getMovingAverageY(prevIndex) : series->getY(prevIndex);
         if (isfinite(prevValue))
         {
            _toPixel(series->getX(prevIndex), prevValue, prevX, prevY);
            havePrevPoint = true;
         }
      }

      for (size_t i = startIndex; i < count; i++)
      {
         float value = useMovingAverage ? series->getMovingAverageY(i) : series->getY(i);
         if (!isfinite(value))
         {
            continue;
         }

         int16_t x;
         int16_t y;
         _toPixel(series->getX(i), value, x, y);

         if (connectPoints && havePrevPoint)
         {
            _display->drawLine(prevX, prevY, x, y, color);
         }
         else
         {
            _display->drawPixel(x, y, color);
         }

         prevX = x;
         prevY = y;
         havePrevPoint = true;
      }

      return count;
   }

public:
   ///
   /// <summary>
   /// Initializes a new ScatterPlot at the specified position and dimensions.
   /// </summary>
   /// <param name="display">Pointer to the display object.</param>
   /// <param name="x">X coordinate for the plot.</param>
   /// <param name="y">Y coordinate for the plot.</param>
   /// <param name="width">Width of the plot area in pixels.</param>
   /// <param name="height">Height of the plot area in pixels.</param>
   /// <param name="title">Optional centered title drawn above the chart; defaults to none, in which case the plot uses the full area.</param>
   ///
   ScatterPlot(ArduinoWithDisplay* display, int16_t x, int16_t y, int16_t width, int16_t height, const String& title = String())
      : _display(display), _x(x), _y(y), _width(width), _height(height), _title(title)
   {
   }

   ~ScatterPlot()
   {
      for (size_t i = 0; i < _seriesCount; i++)
      {
         delete _series[i];
      }
      delete[] _series;
   }

   ///
   /// <summary>
   /// Repositions and/or resizes the plot area. Forces a full redraw on the next
   /// render() since the chart geometry changes.
   /// </summary>
   /// <param name="x">X coordinate for the plot.</param>
   /// <param name="y">Y coordinate for the plot.</param>
   /// <param name="width">Width of the plot area in pixels.</param>
   /// <param name="height">Height of the plot area in pixels.</param>
   ///
   void setRect(int16_t x, int16_t y, int16_t width, int16_t height)
   {
      _x = x;
      _y = y;
      _width = width;
      _height = height;
      _forceFullRedraw = true;
   }

   ///
   /// <summary>
   /// Creates a new scatter plot series owned by this plot, with the specified initial
   /// capacity.
   /// </summary>
   /// <param name="capacity">Initial capacity for the series' data arrays.</param>
   /// <returns>Pointer to the newly created series.</returns>
   ///
   ScatterPlotSeries* createSeries(size_t capacity = 10)
   {
      if (_seriesCount >= _seriesCapacity)
      {
         size_t newCapacity = _seriesCapacity + 5;
         ScatterPlotSeries** newSeries = new (std::nothrow) ScatterPlotSeries*[newCapacity];

         if (newSeries == nullptr)
         {
            Util::setHaltReason("OOM allocating series array in ScatterPlot");
            Util::reset();
            return nullptr;
         }

         if (_seriesCount > 0)
         {
            memcpy(newSeries, _series, _seriesCount * sizeof(ScatterPlotSeries*));
         }

         delete[] _series;
         _series = newSeries;
         _seriesCapacity = newCapacity;
      }

      ScatterPlotSeries* newSeries = new ScatterPlotSeries(capacity);
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
   ScatterPlotSeries* getSeries(size_t index)
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
   /// Forces the next render() call to perform a full clear and redraw of the chart area,
   /// even if the computed axis range has not changed. Use this after externally clearing
   /// the display or otherwise invalidating pixels render() would not normally redraw.
   /// </summary>
   ///
   void invalidate()
   {
      _forceFullRedraw = true;
   }

   ///
   /// <summary>
   /// Sets or clears the centered title drawn above the chart. Pass an empty string to
   /// remove the title, restoring the full plot area to the chart. Forces a full redraw
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
   /// Sets a custom format string for X-axis labels. Pass nullptr to revert to the
   /// default significant-digit formatting.
   /// </summary>
   /// <param name="format">Format pattern (e.g., "##.#s"), or nullptr for default.</param>
   ///
   void setXAxisFormat(const char* format)
   {
      _xAxisFormat = format;
   }

   ///
   /// <summary>
   /// Sets a custom format string for Y-axis labels. Pass nullptr to revert to the
   /// default significant-digit formatting.
   /// </summary>
   /// <param name="format">Format pattern (e.g., "#.##F"), or nullptr for default.</param>
   ///
   void setYAxisFormat(const char* format)
   {
      _yAxisFormat = format;
   }

   ///
   /// <summary>
   /// Sets an initial Y-axis range the chart starts with before any data requires a wider
   /// range. The plot's Y range never shrinks below [min, max], but grows beyond it as
   /// needed to fit newly added data. Pass NAN for either value to disable that bound.
   /// </summary>
   /// <param name="min">Initial minimum Y value.</param>
   /// <param name="max">Initial maximum Y value.</param>
   ///
   void setInitialYRange(float min, float max)
   {
      _initialYMin = min;
      _initialYMax = max;
   }

   ///
   /// <summary>
   /// Gets the left pixel coordinate of the chart's plotted-data area (after the reserved
   /// Y-axis label column), as computed by the most recent render(). Useful for aligning
   /// another chart's x-axis (e.g. a HistogramPlot) with this one.
   /// </summary>
   /// <returns>Left pixel coordinate of the chart area.</returns>
   ///
   int16_t getChartLeft() const
   {
      return _chartLeft;
   }

   ///
   /// <summary>
   /// Gets the pixel width of the chart's plotted-data area (excluding the reserved
   /// Y-axis label column), as computed by the most recent render(). Useful for aligning
   /// another chart's x-axis (e.g. a HistogramPlot) with this one.
   /// </summary>
   /// <returns>Width of the chart area, in pixels.</returns>
   ///
   int16_t getChartWidth() const
   {
      return _chartWidth;
   }

   ///
   /// <summary>
   /// Gets the pixel width of the reserved Y-axis label column, as computed by the most
   /// recent render(). Useful for reserving a matching column in another chart (e.g. a
   /// HistogramPlot) so both charts' x-axes line up.
   /// </summary>
   /// <returns>Width of the reserved Y-axis label column, in pixels.</returns>
   ///
   int16_t getYAxisWidth() const
   {
      return _yAxisWidth;
   }

   ///
   /// <summary>
   /// Renders every owned series: automatically computes the shared X/Y axis range to fit
   /// whatever is currently displayed (based on each series' showPoints/showLines/
   /// showMovingAverage flags), then draws axis labels and each series' points, lines,
   /// and/or moving-average line. A full clear and redraw only happens when the computed
   /// axis range changes or invalidate() was called since the last render(); otherwise
   /// only points added since the last call are plotted, so this can be called every
   /// frame while data is still being collected without flickering or repeatedly
   /// redrawing old points.
   /// </summary>
   ///
   void render()
   {
      if (_display == nullptr || _seriesCount == 0)
      {
         return;
      }

      float xMin;
      float xMax;
      float yMin;
      float yMax;
      _computeAxisRange(&xMin, &xMax, &yMin, &yMax);

      if (!isfinite(xMin) || !isfinite(xMax) || !isfinite(yMin) || !isfinite(yMax))
      {
         return;
      }

      bool axisChanged = !_hasRenderedFrame || (xMin != _axisXMin) || (xMax != _axisXMax) || (yMin != _axisYMin) || (yMax != _axisYMax);
      bool needsFullRedraw = _forceFullRedraw || axisChanged;

      _display->setTextSize(2);

      if (needsFullRedraw)
      {
         _computeChartGeometry(yMin, yMax);

         if (_chartWidth < 20 || _chartHeight < 20)
         {
            return;
         }

         // Only the plotted-data interior needs a black flash-clear, since stale points
         // must be erased when the axis range changes. This excludes the axis line pixels
         // (redrawn gray below regardless) and the labels (fixed-width text that redraws
         // over itself in place), so neither ever flashes black.
         _display->fillRect(_chartLeft + 1, _chartTop, _chartWidth - 1, _chartHeight - 1, Color::BLACK);

         _axisXMin = xMin;
         _axisXMax = xMax;
         _axisYMin = yMin;
         _axisYMax = yMax;
         _forceFullRedraw = false;

         _drawAxes();

         for (size_t i = 0; i < _seriesCount; i++)
         {
            _series[i]->resetRenderState();
         }
      }
      else if (_chartWidth < 20 || _chartHeight < 20)
      {
         return;
      }

      for (size_t i = 0; i < _seriesCount; i++)
      {
         ScatterPlotSeries* series = _series[i];

         if (series->showPoints || series->showLines)
         {
            size_t newCount = _plotSeriesData(series, series->renderedCount(), false);
            series->setRenderedCount(newCount);
         }

         if (series->showMovingAverage)
         {
            size_t newReadyCount = _plotSeriesData(series, series->movingAverageRenderedCount(), true);
            series->setMovingAverageRenderedCount(newReadyCount);
         }
      }

      _hasRenderedFrame = true;
   }
};
