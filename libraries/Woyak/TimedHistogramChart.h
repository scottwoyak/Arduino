#pragma once

#include "BarChart.h"
#include "ColorRange.h"
#include "TimedHistogram.h"

///
/// <summary>
/// A windowed histogram chart with a current-value marker drawn as a horizontal bar
/// along the chart's bottom axis, filling from the left edge up to the current value's
/// position (like a progress bar). The marker overlaps the axis line, erasing in the
/// axis color so it reads as part of the axis rather than a separate element.
/// </summary>
///
class TimedHistogramChart
{
private:
   // height (in pixels) of the axis/marker bar along the chart's bottom edge
   static constexpr uint16_t AXIS_HEIGHT = 2;

   BarChart _chart;
   TimedHistogram _histogram;
   float* _values;
   RangeF _fullXRange;
   RangeF _visibleXRange;
   ColorRange* _colorRange = nullptr;

   Rect16 _axisRect;
   Color _axisMarkerColor;
   Color _axisColor;
   float _currentValue = NAN;
   float _lastDrawnValue = NAN;

   ///
   /// <summary>
   /// Assigns each bar's color based on its value range midpoint, using the color range
   /// if one was provided.
   /// </summary>
   ///
   void _applyBarColors()
   {
      if (_colorRange == nullptr)
      {
         return;
      }

      uint16_t numBars = _chart.getNumBars();
      float binWidth = (_fullXRange.max - _fullXRange.min) / numBars;
      for (uint16_t i = 0; i < numBars; i++)
      {
         float binCenter = _fullXRange.min + (i + 0.5f) * binWidth;
         _chart.setBarColor(i, _colorRange->getColor(binCenter));
      }
   }

   ///
   /// <summary>
   /// Draws the current-value marker as a horizontal bar along the axis, only
   /// repainting the delta area between the old and new fill positions.
   /// </summary>
   /// <param name="display">Display to draw to.</param>
   ///
   void _drawAxis(LGFX* display)
   {
      if (isnan(_currentValue))
      {
         display->fillRect(_axisRect.x, _axisRect.y, _axisRect.width, _axisRect.height, (uint16_t)Color::RED);
         _lastDrawnValue = NAN;
         return;
      }

      uint16_t pos = constrain((_currentValue - _visibleXRange.min) / (_visibleXRange.max - _visibleXRange.min), 0, 1) * _axisRect.width;

      if (isnan(_lastDrawnValue))
      {
         // full redraw: paint the whole axis in the axis color, then overlay the marker
         display->fillRect(_axisRect.x, _axisRect.y, _axisRect.width, _axisRect.height, (uint16_t)_axisColor);
         display->fillRect(_axisRect.x, _axisRect.y, pos, _axisRect.height, (uint16_t)_axisMarkerColor);
         _lastDrawnValue = _currentValue;
         return;
      }

      uint16_t lastPos = constrain((_lastDrawnValue - _visibleXRange.min) / (_visibleXRange.max - _visibleXRange.min), 0, 1) * _axisRect.width;

      if (pos == lastPos)
      {
         return;
      }

      if (pos > lastPos)
      {
         // bar grew: fill in the newly covered area
         display->fillRect(_axisRect.x + lastPos, _axisRect.y, pos - lastPos, _axisRect.height, (uint16_t)_axisMarkerColor);
      }
      else
      {
         // bar shrank: clear the now-uncovered area back to the axis color
         display->fillRect(_axisRect.x + pos, _axisRect.y, lastPos - pos, _axisRect.height, (uint16_t)_axisColor);
      }

      _lastDrawnValue = _currentValue;
   }

public:
   ///
   /// <summary>
   /// Initializes a new instance of the TimedHistogramChart class.
   /// </summary>
   /// <param name="rect">Bounding rectangle of the histogram bars.</param>
   /// <param name="range">Full value range spanned by the histogram's bins.</param>
   /// <param name="numBins">Number of bins across the value range.</param>
   /// <param name="ms">Duration (in milliseconds) of history retained per bin.</param>
   /// <param name="color">Color used to render the filled portion of each bar.</param>
   /// <param name="backgroundColor">Color used to render the unfilled portion of each bar.</param>
   /// <param name="axisMarkerColor">Color of the current-value marker bar drawn along the axis.</param>
   /// <param name="axisColor">Color of the axis line itself, used to erase the marker bar as it shrinks.</param>
   ///
   TimedHistogramChart(Rect16 rect,
      RangeF range,
      uint numBins,
      uint ms,
      Color color,
      Color backgroundColor,
      Color axisMarkerColor = Color::WHITE,
      Color axisColor = Color::GRAY) : _chart(rect, numBins, RangeF(0, 1), color, backgroundColor),
                               _histogram(range, numBins, ms)
   {
      // allocate values array
      _values = new float[numBins];
      _fullXRange = range;
      _visibleXRange = range;

      _axisMarkerColor = axisMarkerColor;
      _axisColor = axisColor;
      _axisRect = { rect.x, (uint16_t)(rect.bottom() + 1), rect.width, AXIS_HEIGHT };
   }

   virtual ~TimedHistogramChart()
   {
      delete _values;
   }

   ///
   /// <summary>
   /// Assigns a color range used to color each bar based on its value, overriding the
   /// single bar color passed to the constructor.
   /// </summary>
   /// <param name="colorRange">The color range to use; must outlive this chart.</param>
   ///
   void setColorRange(ColorRange* colorRange)
   {
      _colorRange = colorRange;
      _applyBarColors();
      _chart.reset();
   }

   void set(float value)
   {
      _histogram.set(value);
   }

   ///
   /// <summary>
   /// Sets the current-value marker drawn along the chart's bottom axis. Distinct from
   /// set(), which records a value into the histogram's bins; this only moves the
   /// marker bar, so it can be updated independently (e.g. on every received value
   /// rather than at the histogram's own sampling cadence).
   /// </summary>
   /// <param name="value">The current value, or NAN to show no data (drawn as a solid red bar).</param>
   ///
   void setCurrentValue(float value)
   {
      _currentValue = value;
   }

   void draw(LGFX* display)
   {
      _histogram.get(_values);
      _chart.draw(display, _values);
      _drawAxis(display);
   }

   void reset()
   {
      _chart.reset();
      _lastDrawnValue = NAN;
   }

   RangeF getCurrentValuesRange()
   {
      return _histogram.getCurrentValuesRange();
   }

   float min() const
   {
      return _histogram.min();
   }

   float max() const
   {
      return _histogram.max();
   }

   float average() const
   {
      return _histogram.average();
   }

   RangeF getVisibleRange()
   {
      return _visibleXRange;
   }


   uint valueToBar(float value)
   {
      uint16_t bar = ((value - _fullXRange.min) / (_fullXRange.max - _fullXRange.min)) * _chart.getNumBars();

      /*
      Serial.print("valueToBar range=");
      Serial.print(_fullXRange.min);
      Serial.print("-");
      Serial.print(_fullXRange.max);
      Serial.print(" value=");
      Serial.print(value);
      Serial.print(" bar=");
      Serial.println(bar);
      */

      return constrain(bar, 0, _chart.getNumBars() - 1);
   }

   void setVisibleRange(RangeF range)
   {
      if (_visibleXRange.min == range.min && _visibleXRange.max == range.max)
      {
         return;
      }
      _visibleXRange = range;

      uint minBar = valueToBar(_visibleXRange.min);
      uint maxBar = valueToBar(_visibleXRange.max);
      /*
      Serial.print(minBar);
      Serial.print(" ");
      Serial.print(maxBar);
      Serial.println();
      */

      _chart.setVisibleBars(RangeU16(minBar, maxBar));

      // the axis marker's position depends on _visibleXRange, so force a full redraw
      _lastDrawnValue = NAN;
   }
};
