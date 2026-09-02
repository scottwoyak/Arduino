#pragma once

#include "ArduinoWithDisplay.h"
#include "ColorX.h"
#include "DisplayBuffer.h"
#include "DisplayValue.h"
#include "Format.h"
#include "IScatterPlotSeries.h"
#include "ScatterPlotSeries.h"
#include "Structs.h"
#include "TimedScatterPlotSeries.h"
#include "Util.h"
#include <math.h>
#include <new>

///
/// <summary>
/// Renders one or more ScatterPlotSeries/TimedScatterPlotSeries on the display as a
/// shared-axis scatter plot. Callers create series via createSeries()/
/// createRollingSeries()/createTimedSeries(), populate them by calling add() as data
/// becomes available, and set each series' display flags (showPoints, showLines,
/// showMovingAverage) and color. Calling draw() scans every owned series, automatically
/// computes the shared X/Y axis range to fit whatever is currently displayed, and draws
/// axis labels plus each series' points/lines/moving-average line. Every series/layer
/// rasterizes its current frame into one plot-wide shared DisplayBuffer, each stamping a
/// distinct layer index, and the whole buffer is diffed against the previous frame's in a
/// single pass (see DisplayBuffer::draw()), drawing only the pixels that actually
/// changed color; unchanging pixels are never redrawn or flashed.
/// </summary>
/// <remarks>
/// Title/min/max/range labels are rendered through lazily-created DisplayValues (see
/// DisplayValue.h) rather than raw ArduinoWithDisplay::print() calls, so repeated
/// redraws only repaint each label's own small sprite region instead of erasing and
/// reprinting raw text every frame - avoiding the flicker that raw print() calls incur
/// when a new, differently-sized value overwrites an old one.
/// </remarks>
///
class ScatterPlot
{
public:
   ///
   /// <summary>
   /// Controls how the Y axis range is computed from frame to frame.
   /// </summary>
   ///
   enum class AxisMode
   {
      AUTO,       // Y axis range is recomputed each frame to tightly fit the currently displayed data.
      GROW_ONLY,  // Y axis range only ever expands to fit new data and never shrinks back down.
   };

private:
   static constexpr int16_t Y_AXIS_LABEL_GAP = 2;

   ArduinoWithDisplay* _display;
   Rect16 _rect;

   int16_t _x;
   int16_t _y;
   int16_t _width;
   int16_t _height;

   // Chart geometry established by the most recent full redraw.
   int16_t _chartLeft = 0;
   int16_t _chartTop = 0;
   int16_t _chartWidth = 0;
   int16_t _chartHeight = 0;
   int16_t _yAxisWidth = 0;

   float _axisXMin = 0.0f;
   float _axisXMax = 0.0f;
   float _axisYMin = 0.0f;
   float _axisYMax = 0.0f;

   bool _hasRenderedFrame = false;
   bool _forceFullRedraw = false;

   // Tracks whether the chart's pixel geometry has ever been established and the outer
   // plot rect physically painted. Unlike _hasRenderedFrame (which clear()/no-data
   // draw() resets so the next frame knows to wait for a fresh axis range), this stays
   // true across clear() since clear() already physically erases the rect itself - so
   // the next draw() doesn't need to redundantly fillRect() the same area again just
   // because a full redraw was forced.
   bool _geometryEstablished = false;

   // Plot-wide shared 4-bit layer buffer used to rasterize every series/layer each frame
   // and diff against the previous frame, so only pixels that actually changed are redrawn.
   DisplayBuffer _displayBuffer;

   // Y-axis min/max label format. Callers set this via setYAxisFormat(); always forced
   // to right alignment since Y-axis labels are drawn flush against the chart's left edge.
   Format _yAxisFormat = Format("##.##", Format::Alignment::RIGHT);

   // X-axis min/max/range label format. The scatter plot's data-space X values for timed
   // series are in milliseconds, but the label is always shown in seconds, so the format
   // reflects that (see setXAxisFormat()).
   Format _xAxisFormat = Format("##.#s");

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

   // Lazily-created DisplayValues for the title and Y-axis min/max/range labels, so
   // repeated redraws only repaint their own sprite-backed regions. Created on first use
   // in _computeChartGeometry()/_drawYAxisChrome() once font metrics/geometry are known.
   DisplayValue* _titleField = nullptr;
   DisplayValue* _maxLabelField = nullptr;
   DisplayValue* _minLabelField = nullptr;
   DisplayValue* _rangeLabelField = nullptr;

   // Lazily-created DisplayValues for the X-axis min/max labels, mirroring the Y-axis
   // labels above so repeated redraws only repaint their own sprite-backed regions instead
   // of relying on a whole-row background fill (which would flicker on every axis change).
   DisplayValue* _xMinLabelField = nullptr;
   DisplayValue* _xMaxLabelField = nullptr;

   // Lazily-created DisplayValue for the centered X-axis range label, shown when
   // _showXRangeValue is enabled (see setShowXRangeValue()).
   DisplayValue* _xRangeLabelField = nullptr;
   bool _showXMinMaxValue = true;
   bool _showXRangeValue = false;

   // Controls whether the Y axis shows the min/max labels and/or the centered range
   // label, mirroring the X-axis toggles above. Both default to true to preserve the
   // Y axis' original always-shown behavior.
   bool _showYMinMaxValue = true;
   bool _showYRangeValue = true;

   ScatterPlotSeriesBase** _series = nullptr;
   size_t _seriesCount = 0;
   size_t _seriesCapacity = 0;

   float _initialYMin = NAN;
   float _initialYMax = NAN;

   // Controls whether the Y axis range is only ever expanded to fit new data, never shrunk
   // back down, avoiding the axis (and its labels) rescaling smaller as older/larger values
   // age out of a rolling series. See setYAxisMode().
   AxisMode _yAxisMode = AxisMode::AUTO;

   // Controls how the shared DisplayBuffer redraws each frame; see DisplayBuffer::RedrawMode
   // and setRedrawMode(). Defaults to DIFF (only repaint pixels that changed, using bulk
   // column transfers), matching the buffer's own default.
   DisplayBuffer::RedrawMode _redrawMode = DisplayBuffer::RedrawMode::DIFF;

   ///
   /// <summary>
   /// Computes the chart's pixel geometry. The Y-axis label column is sized directly
   /// from the fixed output length of _yAxisFormat (length() * charW()) rather than the
   /// rendered width of the current min/max label text, so the column width - and
   /// therefore _chartLeft/_chartWidth - never shifts as the axis range changes. This
   /// lets draw() rely entirely on DisplayBuffer's diff to erase stale points on an
   /// axis-range-only change, without needing a full physical clear to handle a shifting
   /// chart rectangle.
   /// </summary>
   ///
   void _computeChartGeometry()
   {
      uint16_t yLabelWidth = static_cast<uint16_t>(_yAxisFormat.length()) * _display->charW();
      _yAxisWidth = static_cast<int16_t>(yLabelWidth) + Y_AXIS_LABEL_GAP;

      int16_t axisLineHeight = _display->charH() + 2;
      int16_t titlePadding = _display->charH() / 4;
      int16_t titleHeight = _title.length() > 0 ? (_display->charH() + 2 * titlePadding) : 0;

      _chartLeft = static_cast<int16_t>(_rect.x) + _yAxisWidth;
      _chartTop = static_cast<int16_t>(_rect.y) + titleHeight;
      _chartWidth = static_cast<int16_t>(_rect.width) - _yAxisWidth;
      _chartHeight = static_cast<int16_t>(_rect.height) - axisLineHeight - titleHeight;
   }

   ///
   /// <summary>
   /// Draws the optional centered title and the gray Y axis line plus the min/max/range
   /// Y-axis labels for the current chart geometry and axis range, through lazily-created
   /// DisplayValues. Followed by _drawAxes()'s own X-axis-specific line/label drawing.
   /// </summary>
   ///
   void _drawYAxisChrome(bool geometryChanged)
   {
      int16_t titlePadding = _display->charH() / 4;

      if (_title.length() > 0)
      {
         if (_titleField == nullptr)
         {
            _titleField = new DisplayValue(_display, Format(static_cast<size_t>(_title.length()), Format::Alignment::CENTER), 2, DisplayValue::Alignment::CENTER);
         }
         _titleField->setPosition(_chartLeft + _chartWidth / 2, static_cast<int16_t>(_rect.y) + titlePadding);
         _titleField->draw(_title, _labelColor, _outerBackgroundColor);
      }

      _display->fillRect(_chartLeft, _chartTop, 1, _chartHeight, _axisColor);

      // Only blank the whole Y-axis label column when the chart geometry itself changed (e.g.
      // on first draw or a resize). Each label below already erases its own sprite-backed
      // region before drawing, so redundantly filling the whole column on every axis-range
      // change (which happens whenever the Y values update) just causes a visible flicker/flash.
      if (geometryChanged)
      {
         int16_t yLabelColumnX = static_cast<int16_t>(_rect.x);
         int16_t yLabelColumnWidth = _chartLeft - Y_AXIS_LABEL_GAP - yLabelColumnX;
         _display->fillRect(yLabelColumnX, _chartTop, yLabelColumnWidth, _chartHeight, _outerBackgroundColor);
      }

      if (_showYMinMaxValue)
      {
         if (_maxLabelField == nullptr)
         {
            _maxLabelField = new DisplayValue(_display, _yAxisFormat, 2, DisplayValue::Alignment::RIGHT);
         }
         _maxLabelField->setPosition(_chartLeft - Y_AXIS_LABEL_GAP, _chartTop);
         _maxLabelField->draw(_axisYMax, _labelColor, _outerBackgroundColor);

         if (_minLabelField == nullptr)
         {
            _minLabelField = new DisplayValue(_display, _yAxisFormat, 2, DisplayValue::Alignment::RIGHT);
         }
         _minLabelField->setPosition(_chartLeft - Y_AXIS_LABEL_GAP, _chartTop + _chartHeight - _display->charH());
         _minLabelField->draw(_axisYMin, _labelColor, _outerBackgroundColor);
      }

      if (_showYRangeValue)
      {
         if (_rangeLabelField == nullptr)
         {
            _rangeLabelField = new DisplayValue(_display, _yAxisFormat, 2, DisplayValue::Alignment::RIGHT);
         }
         int16_t rangeLabelY = _chartTop + (_chartHeight - _display->charH()) / 2;
         _rangeLabelField->setPosition(_chartLeft - Y_AXIS_LABEL_GAP, rangeLabelY);
         _rangeLabelField->draw(_axisYMax - _axisYMin, Color::GRAY, _outerBackgroundColor);
      }
   }

   ///
   /// <summary>
   /// Scans every owned series to compute the shared X/Y axis range needed to fit
   /// whatever is currently displayed (raw points/lines, moving average, and/or stddev
   /// band, depending on each series' display flags), clamped to also include any fixed
   /// initial Y range set via setInitialYRange().
   /// </summary>
   /// <param name="outXMin">Receives the computed X axis minimum.</param>
   /// <param name="outXMax">Receives the computed X axis maximum.</param>
   /// <param name="outYMin">Receives the computed Y axis minimum.</param>
   /// <param name="outYMax">Receives the computed Y axis maximum.</param>
   ///
   void _computeAxisRange(float* outXMin, float* outXMax, float* outYMin, float* outYMax)
   {
      float xMin = NAN;
      float xMax = NAN;
      float yMin = NAN;
      float yMax = NAN;

      for (size_t s = 0; s < _seriesCount; s++)
      {
         ScatterPlotSeriesBase* series = _series[s];
         bool includeRaw = series->showPoints || series->showLines;

         float seriesXMin;
         float seriesXMax;
         float seriesYMin;
         float seriesYMax;
         series->getRawRange(&seriesXMin, &seriesXMax, &seriesYMin, &seriesYMax);

         float fixedXMin;
         float fixedXMax;
         if (series->getFixedXRange(&fixedXMin, &fixedXMax))
         {
            seriesXMin = fixedXMin;
            seriesXMax = fixedXMax;
         }

         if (isfinite(seriesXMin) && (!isfinite(xMin) || (seriesXMin < xMin))) xMin = seriesXMin;
         if (isfinite(seriesXMax) && (!isfinite(xMax) || (seriesXMax > xMax))) xMax = seriesXMax;

         if (includeRaw)
         {
            if (isfinite(seriesYMin) && (!isfinite(yMin) || (seriesYMin < yMin))) yMin = seriesYMin;
            if (isfinite(seriesYMax) && (!isfinite(yMax) || (seriesYMax > yMax))) yMax = seriesYMax;
         }

         if (series->showMovingAverage)
         {
            float maMin;
            float maMax;
            series->movingAverageRange(&maMin, &maMax);
            if (isfinite(maMin) && (!isfinite(yMin) || (maMin < yMin))) yMin = maMin;
            if (isfinite(maMax) && (!isfinite(yMax) || (maMax > yMax))) yMax = maMax;
         }

         if (series->showStdDevBand)
         {
            float bandMin;
            float bandMax;
            series->stdDevBandRange(&bandMin, &bandMax);
            if (isfinite(bandMin) && (!isfinite(yMin) || (bandMin < yMin))) yMin = bandMin;
            if (isfinite(bandMax) && (!isfinite(yMax) || (bandMax > yMax))) yMax = bandMax;
         }
      }

      *outXMin = xMin;
      *outXMax = xMax;
      *outYMin = isfinite(_initialYMin) ? ((!isfinite(yMin) || (_initialYMin < yMin)) ? _initialYMin : yMin) : yMin;
      *outYMax = isfinite(_initialYMax) ? ((!isfinite(yMax) || (_initialYMax > yMax)) ? _initialYMax : yMax) : yMax;
   }

   ///
   /// <summary>
   /// (Re)draws just the gray Y-axis and X-axis lines themselves (not their labels/title),
   /// covering the same chart-relative column 0 and bottom row that the shared DisplayBuffer
   /// is bound to. DisplayBuffer::draw(RedrawMode::FULL) unconditionally repaints every
   /// pixel of that rectangle - including column 0 and the bottom row - back to the
   /// background color every frame, which would otherwise erase these lines; called after
   /// every _displayBuffer.draw() call (regardless of redraw mode) so the lines are always
   /// repainted on top, last.
   /// </summary>
   ///
   void _drawAxisLines()
   {
      _display->fillRect(_chartLeft, _chartTop, 1, _chartHeight, _axisColor);
      _display->fillRect(_chartLeft, _chartTop + _chartHeight - 1, _chartWidth, 1, _axisColor);
   }

   ///
   /// <summary>
   /// Draws the Y-axis chrome via _drawYAxisChrome(), then draws the gray X-axis line and
   /// its min/max labels for the current chart geometry and axis range.
   /// </summary>
   ///
   void _drawAxes(bool geometryChanged)
   {
      _drawYAxisChrome(geometryChanged);

      _display->fillRect(_chartLeft, _chartTop + _chartHeight - 1, _chartWidth, 1, _axisColor);

      // Only blank the whole X-axis label row when the chart geometry itself changed; the
      // min/max labels below are sprite-backed DisplayValues that already erase their own
      // region before drawing, so redundantly filling the whole row on every axis-range
      // change (e.g. whenever the X values update) just causes a visible flicker/flash.
      if (geometryChanged)
      {
         _display->fillRect(_chartLeft, _chartTop + _chartHeight + 1, _chartWidth, _display->charH(), _outerBackgroundColor);
      }

      if (_showXRangeValue)
      {
         if (_xRangeLabelField == nullptr)
         {
            _xRangeLabelField = new DisplayValue(_display, _xAxisFormat, 2, DisplayValue::Alignment::CENTER);
         }
         int16_t rangeLabelX = _chartLeft + _chartWidth / 2;
         _xRangeLabelField->setPosition(rangeLabelX, _chartTop + _chartHeight + 1);
         _xRangeLabelField->draw((_axisXMax - _axisXMin) / 1000.0f, Color::GRAY, _outerBackgroundColor);
      }

      if (_showXMinMaxValue)
      {
         if (_xMinLabelField == nullptr)
         {
            _xMinLabelField = new DisplayValue(_display, _xAxisFormat, 2, DisplayValue::Alignment::LEFT);
         }
         _xMinLabelField->setPosition(_chartLeft, _chartTop + _chartHeight + 1);
         _xMinLabelField->draw(_axisXMin / 1000.0f, _labelColor, _outerBackgroundColor);

         if (_xMaxLabelField == nullptr)
         {
            _xMaxLabelField = new DisplayValue(_display, _xAxisFormat, 2, DisplayValue::Alignment::RIGHT);
         }
         _xMaxLabelField->setPosition(_chartLeft + _chartWidth, _chartTop + _chartHeight + 1);
         _xMaxLabelField->draw(_axisXMax / 1000.0f, _labelColor, _outerBackgroundColor);
      }
   }

   ///
   /// <summary>
   /// Converts a data-space (x, y) point into chart-local pixel coordinates, reserving
   /// column 0 for the Y-axis line and the bottom row for the X-axis line so plotted data
   /// never overwrites either.
   /// </summary>
   /// <param name="x">Data-space X value to convert.</param>
   /// <param name="y">Data-space Y value to convert.</param>
   /// <param name="outX">Receives the chart-local pixel X coordinate.</param>
   /// <param name="outY">Receives the chart-local pixel Y coordinate.</param>
   ///
   void _toPixel(float x, float y, int16_t& outX, int16_t& outY) const
   {
      float xSpan = _axisXMax - _axisXMin;
      float ySpan = _axisYMax - _axisYMin;

      if (xSpan <= 0.0f)
      {
         outX = static_cast<int16_t>((_chartWidth - 1) / 2);
      }
      else
      {
         // Column 0 is reserved for the Y-axis line drawn by _drawYAxisChrome(), so data
         // is scaled/offset into [1, _chartWidth - 1] instead of [0, _chartWidth - 1] to
         // avoid a plotted point ever overwriting that line, mirroring how the Y span
         // below is scaled by (_chartHeight - 2) to reserve the bottom row for the X axis.
         outX = 1 + static_cast<int16_t>(((x - _axisXMin) / xSpan) * (_chartWidth - 2));
      }

      if (ySpan <= 0.0f)
      {
         outY = static_cast<int16_t>((_chartHeight - 2) / 2);
      }
      else
      {
         outY = static_cast<int16_t>(((_axisYMax - y) / ySpan) * (_chartHeight - 2));
      }

      outX = constrain(outX, static_cast<int16_t>(1), static_cast<int16_t>(_chartWidth - 1));
      outY = constrain(outY, static_cast<int16_t>(0), static_cast<int16_t>(_chartHeight - 2));
   }

   ///
   /// <summary>
   /// Rasterizes one series' raw points (or its moving-average line) into the shared
   /// DisplayBuffer on the given layer, connecting consecutive points with a line when
   /// the series requests lines (or when rasterizing the moving average). Points/moving-
   /// average entries with no value (NAN, e.g. a bin/time-slot that never received a real
   /// sample) are skipped entirely rather than drawn, so the line connects straight from
   /// the last real point to the next one - i.e. linear interpolation across the gap -
   /// instead of passing through an intermediate placeholder value.
   /// </summary>
   /// <param name="series">Series whose data should be rasterized.</param>
   /// <param name="useMovingAverage">True to rasterize the moving-average line instead of raw points.</param>
   /// <param name="layer">DisplayBuffer layer index to stamp the rasterized pixels with.</param>
   ///
   void _rasterizeSeriesData(ScatterPlotSeriesBase* series, bool useMovingAverage, uint8_t layer)
   {
      size_t count = useMovingAverage ? series->getMovingAverageReadyCount() : series->getCount();
      if (count == 0 || layer == 0)
      {
         return;
      }

      bool connectPoints = useMovingAverage || series->showLines;

      bool havePrevPoint = false;
      int16_t prevX = 0;
      int16_t prevY = 0;

      for (size_t i = 0; i < count; i++)
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
            _displayBuffer.drawLine(prevX, prevY, x, y, layer);
         }
         else if (series->pointSize > 1)
         {
            _displayBuffer.drawPoint(x, y, series->pointSize, layer);
         }
         else
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
   /// Rasterizes one series' rolling mean +/- stddev band (low and high edges) into the
   /// shared DisplayBuffer on the given layer, connecting consecutive ready points with a
   /// line on each edge.
   /// </summary>
   /// <param name="series">Series whose stddev band should be rasterized.</param>
   /// <param name="layer">DisplayBuffer layer index to stamp the rasterized pixels with.</param>
   ///
   void _rasterizeStdDevBand(ScatterPlotSeriesBase* series, uint8_t layer)
   {
      size_t count = series->getStdDevReadyCount();
      if (count == 0 || layer == 0)
      {
         return;
      }

      bool haveLowPrev = false;
      int16_t lowPrevX = 0;
      int16_t lowPrevY = 0;
      bool haveHighPrev = false;
      int16_t highPrevX = 0;
      int16_t highPrevY = 0;

      for (size_t i = 0; i < count; i++)
      {
         float x = series->getX(i);

         float low = series->getStdDevLowY(i);
         if (isfinite(low))
         {
            int16_t px;
            int16_t py;
            _toPixel(x, low, px, py);
            if (haveLowPrev)
            {
               _displayBuffer.drawLine(lowPrevX, lowPrevY, px, py, layer);
            }
            else
            {
               _displayBuffer.setPixel(px, py, layer);
            }
            lowPrevX = px;
            lowPrevY = py;
            haveLowPrev = true;
         }
         else
         {
            haveLowPrev = false;
         }

         float high = series->getStdDevHighY(i);
         if (isfinite(high))
         {
            int16_t px;
            int16_t py;
            _toPixel(x, high, px, py);
            if (haveHighPrev)
            {
               _displayBuffer.drawLine(highPrevX, highPrevY, px, py, layer);
            }
            else
            {
               _displayBuffer.setPixel(px, py, layer);
            }
            highPrevX = px;
            highPrevY = py;
            haveHighPrev = true;
         }
         else
         {
            haveHighPrev = false;
         }
      }
   }

   ///
   /// <summary>
   /// Takes ownership of series, growing the internal series array (by 5 slots) first if
   /// needed.
   /// </summary>
   /// <param name="series">Series to add; ownership transfers to this plot.</param>
   ///
   void _addSeries(ScatterPlotSeriesBase* series)
   {
      if (_seriesCount >= _seriesCapacity)
      {
         size_t newCapacity = _seriesCapacity + 5;
         ScatterPlotSeriesBase** newSeries = new (std::nothrow) ScatterPlotSeriesBase*[newCapacity];

         if (newSeries == nullptr)
         {
            Util::setHaltReason("OOM allocating series array in ScatterPlot");
            Util::reset();
            return;
         }

         if (_seriesCount > 0)
         {
            memcpy(newSeries, _series, _seriesCount * sizeof(ScatterPlotSeriesBase*));
         }

         delete[] _series;
         _series = newSeries;
         _seriesCapacity = newCapacity;
      }

      _series[_seriesCount++] = series;
   }

private:
   ///
   /// <summary>
   /// Creates a scatter plot occupying the given pixel rect on display. Private since
   /// every plot must be constructed with explicit X/Y axis label formats (see the
   /// public constructors below) rather than silently defaulting to "##.##", which
   /// rarely matches the data actually being plotted.
   /// </summary>
   /// <param name="display">Display the plot will be drawn to.</param>
   /// <param name="x">Left pixel coordinate of the plot's outer rect.</param>
   /// <param name="y">Top pixel coordinate of the plot's outer rect.</param>
   /// <param name="width">Pixel width of the plot's outer rect.</param>
   /// <param name="height">Pixel height of the plot's outer rect.</param>
   /// <param name="title">Optional centered title drawn above the chart.</param>
   ///
   ScatterPlot(
      ArduinoWithDisplay* display,
      int16_t x,
      int16_t y,
      int16_t width,
      int16_t height,
      const String& title = String())
      : _display(display),
         _rect(Rect16{ static_cast<uint16_t>(x), static_cast<uint16_t>(y), static_cast<uint16_t>(width), static_cast<uint16_t>(height) }),
         _x(x),
         _y(y),
         _width(width),
         _height(height),
         _title(title)
   {
      ASSERT(_display != nullptr);
   }

public:
   ///
   /// <summary>
   /// Creates a scatter plot occupying the given pixel rect on display, with custom X/Y
   /// axis label formats (see setXAxisFormat()/setYAxisFormat()).
   /// </summary>
   /// <param name="display">Display the plot will be drawn to.</param>
   /// <param name="x">Left pixel coordinate of the plot's outer rect.</param>
   /// <param name="y">Top pixel coordinate of the plot's outer rect.</param>
   /// <param name="width">Pixel width of the plot's outer rect.</param>
   /// <param name="height">Pixel height of the plot's outer rect.</param>
   /// <param name="xAxisFormat">Format string for the X axis min/max labels.</param>
   /// <param name="yAxisFormat">Format string for the Y axis min/max/range labels.</param>
   /// <param name="title">Optional centered title drawn above the chart.</param>
   ///
   ScatterPlot(
      ArduinoWithDisplay* display,
      int16_t x,
      int16_t y,
      int16_t width,
      int16_t height,
         const char* xAxisFormat,
         const char* yAxisFormat,
         const String& title = String())
         : ScatterPlot(display, x, y, width, height, title)
      {
         setXAxisFormat(xAxisFormat);
         setYAxisFormat(yAxisFormat);
      }

      ///
      /// <summary>
      /// Creates a scatter plot occupying the given pixel rect on display, with custom X/Y
      /// axis label formats (see setXAxisFormat()/setYAxisFormat()).
      /// </summary>
      /// <param name="display">Display the plot will be drawn to.</param>
      /// <param name="x">Left pixel coordinate of the plot's outer rect.</param>
      /// <param name="y">Top pixel coordinate of the plot's outer rect.</param>
      /// <param name="width">Pixel width of the plot's outer rect.</param>
      /// <param name="height">Pixel height of the plot's outer rect.</param>
      /// <param name="xAxisFormat">Format string for the X axis min/max labels.</param>
      /// <param name="yAxisFormat">Format string for the Y axis min/max/range labels.</param>
      /// <param name="title">Optional centered title drawn above the chart.</param>
      ///
      ScatterPlot(
         ArduinoWithDisplay* display,
         int16_t x,
         int16_t y,
         int16_t width,
         int16_t height,
         const std::string& xAxisFormat,
         const std::string& yAxisFormat,
         const String& title = String())
         : ScatterPlot(display, x, y, width, height, xAxisFormat.c_str(), yAxisFormat.c_str(), title)
      {
      }

      ///
      /// <summary>
      /// Creates a scatter plot occupying the given pixel rect on display, with custom X/Y
      /// axis label formats (see setXAxisFormat()/setYAxisFormat()).
      /// </summary>
      /// <param name="display">Display the plot will be drawn to.</param>
      /// <param name="rect">Pixel rect the plot's outer area will occupy.</param>
      /// <param name="xAxisFormat">Format string for the X axis min/max labels.</param>
      /// <param name="yAxisFormat">Format string for the Y axis min/max/range labels.</param>
      /// <param name="title">Optional centered title drawn above the chart.</param>
      ///
      ScatterPlot(ArduinoWithDisplay* display, Rect16 rect, const char* xAxisFormat, const char* yAxisFormat, const String& title = String())
         : ScatterPlot(
            display,
            static_cast<int16_t>(rect.x),
            static_cast<int16_t>(rect.y),
            static_cast<int16_t>(rect.width),
            static_cast<int16_t>(rect.height),
            xAxisFormat,
            yAxisFormat,
            title)
      {
      }

      ///
      /// <summary>
      /// Creates a scatter plot occupying the given pixel rect on display, with custom X/Y
      /// axis label formats (see setXAxisFormat()/setYAxisFormat()).
      /// </summary>
      /// <param name="display">Display the plot will be drawn to.</param>
      /// <param name="rect">Pixel rect the plot's outer area will occupy.</param>
      /// <param name="xAxisFormat">Format string for the X axis min/max labels.</param>
      /// <param name="yAxisFormat">Format string for the Y axis min/max/range labels.</param>
      /// <param name="title">Optional centered title drawn above the chart.</param>
      ///
      ScatterPlot(ArduinoWithDisplay* display, Rect16 rect, const std::string& xAxisFormat, const std::string& yAxisFormat, const String& title = String())
         : ScatterPlot(display, rect, xAxisFormat.c_str(), yAxisFormat.c_str(), title)
      {
      }

   ///
   /// <summary>
   /// Destroys the plot, deleting every owned series and lazily-created label DisplayValue.
   /// </summary>
   ///
   virtual ~ScatterPlot()
   {
      delete _titleField;
      delete _maxLabelField;
      delete _minLabelField;
      delete _rangeLabelField;
      delete _xMinLabelField;
      delete _xMaxLabelField;
      delete _xRangeLabelField;

      for (size_t i = 0; i < _seriesCount; i++)
      {
         delete _series[i];
      }
      delete[] _series;
   }

   ScatterPlot(const ScatterPlot&) = delete;
   ScatterPlot& operator=(const ScatterPlot&) = delete;

   ///
   /// <summary>
   /// Repositions/resizes the plot's outer pixel rect. Forces a full redraw on the next
   /// draw() since the chart geometry changes.
   /// </summary>
   /// <param name="x">Left pixel coordinate of the plot's outer rect.</param>
   /// <param name="y">Top pixel coordinate of the plot's outer rect.</param>
   /// <param name="width">Pixel width of the plot's outer rect.</param>
   /// <param name="height">Pixel height of the plot's outer rect.</param>
   ///
   void setRect(int16_t x, int16_t y, int16_t width, int16_t height)
   {
      _x = x;
      _y = y;
      _width = width;
      _height = height;
      _rect = Rect16{ static_cast<uint16_t>(x), static_cast<uint16_t>(y), static_cast<uint16_t>(width), static_cast<uint16_t>(height) };
      _forceFullRedraw = true;
      _geometryEstablished = false;
   }

   ///
   /// <summary>
   /// Creates a new growing-array scatter plot series owned by this plot, with the
   /// specified initial capacity. The returned series can still be switched into
   /// fixed-range bin mode via setFixedXRange(xMin, xMax, numBins).
   /// </summary>
   /// <param name="capacity">Initial capacity for the series' data arrays.</param>
   /// <returns>Pointer to the newly created series.</returns>
   ///
   ScatterPlotSeries* createSeries(size_t capacity)
   {
      ScatterPlotSeries* newSeries = new ScatterPlotSeries(capacity);
      _addSeries(newSeries);
      return newSeries;
   }

   ///
   /// <summary>
   /// Creates a new growing-array scatter plot series owned by this plot, with a
   /// default initial capacity of 10.
   /// </summary>
   /// <returns>Pointer to the newly created series.</returns>
   ///
   ScatterPlotSeries* createSeries()
   {
      return createSeries(10);
   }

   ///
   /// <summary>
   /// Creates a new rolling-count scatter plot series owned by this plot (see
   /// ScatterPlotSeries::setRollingCount()): every individual raw sample stays visible in a
   /// fixed-size circular buffer of exactly maxSamples points, overwriting the oldest
   /// sample once full, with the X axis locked to the fixed slot-index range.
   /// </summary>
   /// <param name="maxSamples">Fixed number of samples the rolling buffer holds.</param>
   /// <returns>Pointer to the newly created series, already in rolling-count mode.</returns>
   ///
   ScatterPlotSeries* createRollingSeries(size_t maxSamples)
   {
      ScatterPlotSeries* newSeries = new ScatterPlotSeries(maxSamples);
      newSeries->setRollingCount(maxSamples);
      _addSeries(newSeries);
      return newSeries;
   }

   ///
   /// <summary>
   /// Creates a new timed scatter plot series owned by this plot (see
   /// TimedScatterPlotSeries): every raw sample is retained unbinned, timestamped
   /// with millis() at add() time, and evicted once older than historyMs. Call
   /// updateWindow(nowMs) on the returned series once per frame, before draw(), to keep
   /// the X axis locked to [nowMs - historyMs, nowMs] in sync with the wall clock.
   /// </summary>
   /// <param name="historyMs">Duration, in milliseconds, of the rolling window.</param>
   /// <param name="initialCapacity">Initial capacity for the raw point storage, grown automatically if exceeded.</param>
   /// <returns>Pointer to the newly created series.</returns>
   ///
   TimedScatterPlotSeries* createTimedSeries(unsigned long historyMs, size_t initialCapacity = 64)
   {
      TimedScatterPlotSeries* newSeries = new TimedScatterPlotSeries(historyMs, initialCapacity);
      _addSeries(newSeries);
      return newSeries;
   }

   ///
   /// <summary>
   /// Gets the series at the given index.
   /// </summary>
   /// <param name="index">Zero-based index of the series to retrieve.</param>
   /// <returns>Pointer to the series at index, or nullptr if index is out of range.</returns>
   ///
   IScatterPlotSeries* getSeries(size_t index)
   {
      return (index < _seriesCount) ? _series[index] : nullptr;
   }

   ///
   /// <summary>
   /// Gets the number of series currently owned by this plot.
   /// </summary>
   /// <returns>Number of owned series.</returns>
   ///
   size_t getSeriesCount() const
   {
      return _seriesCount;
   }

   ///
   /// <summary>
   /// Deletes the series at the given index and removes it from this plot, shifting
   /// subsequent series down by one index.
   /// </summary>
   /// <param name="index">Zero-based index of the series to delete.</param>
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
   /// Deletes every series currently owned by this plot.
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
   /// Physically erases the plot's outer rect and clears every owned series' data,
   /// forcing a full redraw on the next draw().
   /// </summary>
   ///
   void clear()
   {
      _display->fillRect(_x, _y, _width, _height, _outerBackgroundColor);

      _displayBuffer.clear(true);

      _hasRenderedFrame = false;
      _forceFullRedraw = true;

      for (size_t i = 0; i < _seriesCount; i++)
      {
         _series[i]->clear();
      }
   }

   ///
   /// <summary>
   /// Sets a custom format for the Y axis min/max/range labels. Forces a full redraw on
   /// the next draw() since the reserved Y-axis label column width is derived from
   /// this format's fixed length.
   /// </summary>
   /// <param name="format">Format object defining precision and alignment.</param>
   ///
   void setYAxisFormat(const char* format)
   {
      // Y-axis labels are always drawn flush against the chart's left edge, so force
      // right alignment on our own copy regardless of how the caller's format is aligned
      // (e.g. a sensor's format may be left-aligned for use elsewhere, like a table).
      _yAxisFormat = Format(format, Format::Alignment::RIGHT);
      _forceFullRedraw = true;
      _geometryEstablished = false;

      // Each label field's sprite is sized from _yAxisFormat's fixed length, so they must
      // be recreated (rather than reused) whenever the format itself changes.
      delete _maxLabelField;
      _maxLabelField = nullptr;
      delete _minLabelField;
      _minLabelField = nullptr;
      delete _rangeLabelField;
      _rangeLabelField = nullptr;
   }

   void setYAxisFormat(const std::string& format)
   {
      setYAxisFormat(format.c_str());
   }

   void setXAxisFormat(const char* format)
   {
      _xAxisFormat = Format(format);
      _forceFullRedraw = true;
      _geometryEstablished = false;

      // Each label field's sprite is sized from _xAxisFormat's fixed length, so they must
      // be recreated (rather than reused) whenever the format itself changes.
      delete _xMinLabelField;
      _xMinLabelField = nullptr;
      delete _xMaxLabelField;
      _xMaxLabelField = nullptr;
   }

   void setXAxisFormat(const std::string& format)
   {
      setXAxisFormat(format.c_str());
   }

   ///
   /// <summary>
   /// Controls whether the X axis shows separate min/max labels at each corner. Enabled
   /// by default.
   /// </summary>
   /// <param name="showXMinMaxValue">True to show the min/max labels; false to hide them.</param>
   ///
   void setShowXMinMaxValue(bool showXMinMaxValue)
   {
      _showXMinMaxValue = showXMinMaxValue;
      _forceFullRedraw = true;
   }

   ///
   /// <summary>
   /// Controls whether the X axis shows a single centered range label (max - min),
   /// mirroring the Y axis' centered range label. Useful for a fixed-width time axis
   /// (e.g. TimedScatterPlotSeries) where only the span matters and showing "-30000" /
   /// "0" would be more confusing than just "30000". Disabled by default.
   /// </summary>
   /// <param name="showXRangeValue">True to show the centered range label; false to hide it.</param>
   ///
   void setShowXRangeValue(bool showXRangeValue)
   {
      _showXRangeValue = showXRangeValue;
      _forceFullRedraw = true;
   }

   ///
   /// <summary>
   /// Controls whether the Y axis shows separate min/max labels at the top/bottom of the
   /// axis. Enabled by default.
   /// </summary>
   /// <param name="showYMinMaxValue">True to show the min/max labels; false to hide them.</param>
   ///
   void setShowYMinMaxValue(bool showYMinMaxValue)
   {
      _showYMinMaxValue = showYMinMaxValue;
      _forceFullRedraw = true;
   }

   ///
   /// <summary>
   /// Controls whether the Y axis shows a single centered range label (max - min).
   /// Enabled by default.
   /// </summary>
   /// <param name="showYRangeValue">True to show the centered range label; false to hide it.</param>
   ///
   void setShowYRangeValue(bool showYRangeValue)
   {
      _showYRangeValue = showYRangeValue;
      _forceFullRedraw = true;
   }

   ///
   /// <summary>
   /// Sets or clears the centered title drawn above the chart. Pass an empty string to
   /// remove the title, restoring the full plot area to the chart. Forces a full redraw
   /// on the next draw() since the chart geometry changes.
   /// </summary>
   /// <param name="title">Title text to display, or an empty string for none.</param>
   ///
   void setTitle(const String& title)
   {
      _title = title;
      _forceFullRedraw = true;
      _geometryEstablished = false;

      // The title field's sprite is sized to the title's own fixed length, so it must be
      // recreated (rather than reused) whenever the title text itself changes.
      delete _titleField;
      _titleField = nullptr;
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
   /// the next draw() since the plot-area background is repainted immediately.
   /// </summary>
   /// <param name="outerBackgroundColor">Color used outside the plotted-data area (e.g. behind axis labels/title).</param>
   /// <param name="plotAreaBackgroundColor">Color used behind the plotted-data area itself.</param>
   /// <param name="axisColor">Color used to draw the axis lines.</param>
   /// <param name="labelColor">Color used to draw axis labels and the title.</param>
   ///
   void setColors(Color outerBackgroundColor, Color plotAreaBackgroundColor, Color axisColor, Color labelColor)
   {
      _outerBackgroundColor = outerBackgroundColor;
      _plotAreaBackgroundColor = plotAreaBackgroundColor;
      _axisColor = axisColor;
      _labelColor = labelColor;
      _forceFullRedraw = true;
      _geometryEstablished = false;
   }

   ///
   /// <summary>
   /// Forces the next draw() call to perform a full clear and redraw of the chart area,
   /// even if the computed axis range has not changed.
   /// </summary>
   ///
   void invalidate()
   {
      _forceFullRedraw = true;
   }

   ///
   /// <summary>
   /// Gets the left pixel coordinate of the chart's plotted-data area (after the reserved
   /// Y-axis label column), as computed by the most recent draw(). Useful for aligning
   /// another chart's plotted-data area with this one.
   /// </summary>
   /// <returns>Left pixel coordinate of the chart's plotted-data area.</returns>
   ///
   int16_t getChartLeft() const
   {
      return _chartLeft;
   }

   ///
   /// <summary>
   /// Gets the pixel width of the chart's plotted-data area (excluding the reserved
   /// Y-axis label column), as computed by the most recent draw(). Useful for aligning
   /// another chart's plotted-data area with this one.
   /// </summary>
   /// <returns>Pixel width of the chart's plotted-data area.</returns>
   ///
   int16_t getChartWidth() const
   {
      return _chartWidth;
   }

   ///
   /// <summary>
   /// Gets the pixel width of the reserved Y-axis label column, as computed by the most
   /// recent draw(). Useful for reserving a matching column in another chart (e.g. a
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
   /// Sets a fixed lower/upper bound the Y axis range must always include, in addition to
   /// whatever range the current series data spans.
   /// </summary>
   /// <param name="min">Minimum Y value the axis range must include.</param>
   /// <param name="max">Maximum Y value the axis range must include.</param>
   ///
   void setInitialYRange(float min, float max)
   {
      _initialYMin = min;
      _initialYMax = max;
   }

   ///
   /// <summary>
   /// Controls how the Y axis range is computed from frame to frame. AxisMode::GROW_ONLY
   /// keeps the axis range stable once it grows to fit a peak, only growing further if new
   /// data exceeds it - useful for live/rolling data where letting the axis (and its
   /// min/max/range labels) continually rescale smaller as older, larger values age out
   /// is more distracting than useful.
   /// </summary>
   /// <param name="mode">The axis mode to use for the Y axis range.</param>
   ///
   void setYAxisMode(AxisMode mode)
   {
      _yAxisMode = mode;
   }

   ///
   /// <summary>
   /// Controls how the shared DisplayBuffer redraws each frame - see DisplayBuffer::RedrawMode.
   /// FULL always repaints every pixel; DIFF only repaints pixels that changed since the
   /// previous frame. Both use the buffer's bulk row/column transfer paths.
   /// </summary>
   /// <param name="mode">The redraw mode to use for subsequent draw() calls.</param>
   ///
   void setRedrawMode(DisplayBuffer::RedrawMode mode)
   {
      _redrawMode = mode;
   }

   ///
   /// <summary>
   /// Computes the shared X/Y axis range across every owned series, then redraws the
   /// chart: performs a full redraw (axes/labels/backgrounds) when the axis range or
   /// geometry has changed, then rasterizes each series' points/lines/moving-average/
   /// stddev band into the shared DisplayBuffer and diffs it against the previous frame,
   /// drawing only the pixels that actually changed. No-op if this plot has no series.
   /// </summary>
   ///
   void draw()
   {
      ASSERT(_display != nullptr);

      if (_seriesCount == 0)
      {
         return;
      }

      float xMin;
      float xMax;
      float yMin;
      float yMax;
      _computeAxisRange(&xMin, &xMax, &yMin, &yMax);

      if ((_yAxisMode == AxisMode::GROW_ONLY) && _hasRenderedFrame && isfinite(_axisYMin) && isfinite(_axisYMax))
      {
         if (isfinite(yMin)) yMin = min(yMin, _axisYMin);
         if (isfinite(yMax)) yMax = max(yMax, _axisYMax);
      }

      if (!isfinite(xMin) || !isfinite(xMax) || !isfinite(yMin) || !isfinite(yMax))
      {
         if (_hasRenderedFrame)
         {
            _display->fillRect(_x, _y, _width, _height, _outerBackgroundColor);
            _displayBuffer.clear(true);
            _hasRenderedFrame = false;
            _forceFullRedraw = true;
         }
         return;
      }

      bool axisChanged = !_hasRenderedFrame || (xMin != _axisXMin) || (xMax != _axisXMax) || (yMin != _axisYMin) || (yMax != _axisYMax);
      bool needsFullRedraw = _forceFullRedraw || axisChanged;

      _display->setTextSize(2);

      bool geometryChanged = false;

      if (needsFullRedraw)
      {
         const int16_t oldChartLeft = _chartLeft;
         const int16_t oldChartTop = _chartTop;
         const int16_t oldChartWidth = _chartWidth;
         const int16_t oldChartHeight = _chartHeight;

         _computeChartGeometry();

         if (_chartWidth < 20 || _chartHeight < 20)
         {
            return;
         }

         geometryChanged = !_geometryEstablished
            || (_chartLeft != oldChartLeft) || (_chartTop != oldChartTop)
            || (_chartWidth != oldChartWidth) || (_chartHeight != oldChartHeight);

         if (geometryChanged)
         {
            _display->fillRect(_x, _y, _width, _height, _outerBackgroundColor);
         }

         _geometryEstablished = true;

         _axisXMin = xMin;
         _axisXMax = xMax;
         _axisYMin = yMin;
         _axisYMax = yMax;
         _forceFullRedraw = false;

         _drawAxes(geometryChanged);
      }
      else if (_chartWidth < 20 || _chartHeight < 20)
      {
         return;
      }

      _displayBuffer.bind(_display, _chartLeft, _chartTop, _chartWidth, _chartHeight);
      _displayBuffer.setBackgroundColor(_plotAreaBackgroundColor);

      _displayBuffer.clear();

      uint8_t nextLayer = 1;

      for (size_t i = 0; i < _seriesCount; i++)
      {
         ScatterPlotSeriesBase* series = _series[i];

         if ((series->showPoints || series->showLines) && nextLayer <= DisplayBuffer::MAX_LAYERS)
         {
            uint8_t layer = nextLayer++;
            _displayBuffer.setPaletteColor(layer, series->color);
            _rasterizeSeriesData(series, false, layer);
         }
      }

      for (size_t i = 0; i < _seriesCount; i++)
      {
         ScatterPlotSeriesBase* series = _series[i];

         if (series->showMovingAverage && nextLayer <= DisplayBuffer::MAX_LAYERS)
         {
            uint8_t layer = nextLayer++;
            _displayBuffer.setPaletteColor(layer, series->movingAverageColor);
            _rasterizeSeriesData(series, true, layer);
         }
      }

      for (size_t i = 0; i < _seriesCount; i++)
      {
         ScatterPlotSeriesBase* series = _series[i];

         if (series->showStdDevBand && nextLayer <= DisplayBuffer::MAX_LAYERS)
         {
            uint8_t layer = nextLayer++;
            _displayBuffer.setPaletteColor(layer, series->stdDevBandColor);
            _rasterizeStdDevBand(series, layer);
         }
      }

      _displayBuffer.draw(_redrawMode);

      _drawAxisLines();

      _hasRenderedFrame = true;
   }
};

