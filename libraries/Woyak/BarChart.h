#pragma once

#include "Bar.h"

///
/// <summary>
/// Fixed set of vertical bars rendered across a rectangle, with support for showing
/// only a sub-range of bars (e.g. panning/zooming a histogram).
/// </summary>
///
class BarChart
{
private:
   VerticalBar** _bars;
   uint8_t _numBars;
   Rect16 _rect;
   RangeU16 _visibleBars;
   RangeF _valueRange;

   ///
   /// <summary>
   /// Resizes and repositions the visible bars to evenly fill the chart's rectangle.
   /// </summary>
   ///
   void _adjustBarWidths()
   {
      // adjust the bar rects
      uint16_t numVisibleBars = _visibleBars.max - _visibleBars.min + 1;
      float barWidth = ((float) _rect.width) / numVisibleBars;
      Rect16 barRect(_rect.x, _rect.y, barWidth, _rect.height);
      for (uint16_t i = _visibleBars.min; i <= _visibleBars.max; i++)
      {
         uint16_t x = floor(_rect.x + i * barWidth);
         uint16_t w = floor(_rect.x + (i + 1) * barWidth) - x;
         barRect.x = x;
         barRect.width = w;
         _bars[i]->setRect(barRect);
      }
   }

public:
   ///
   /// <summary>
   /// Initializes a new instance of the BarChart class.
   /// </summary>
   /// <param name="rect">Bounding rectangle of the chart.</param>
   /// <param name="numBars">Total number of bars in the chart.</param>
   /// <param name="valueRange">Value range mapped across each bar's height.</param>
   /// <param name="color">Color used to render the filled portion of each bar.</param>
   /// <param name="backgroundColor">Color used to render the unfilled portion of each bar.</param>
   ///
   BarChart(Rect16 rect, uint8_t numBars, RangeF valueRange, Color color, Color backgroundColor)
   {
      _rect = rect;
      _numBars = numBars;
      _bars = new VerticalBar*[_numBars];
      _visibleBars = RangeU16(0, _numBars - 1);
      _valueRange = valueRange;

      uint16_t barWidth = _rect.width / _numBars;
      Rect16 barRect(_rect.x, _rect.y, barWidth, _rect.height);
      for (uint8_t i = 0; i < _numBars; i++)
      {
         barRect.x = rect.x + i * barWidth;
         _bars[i] = new VerticalBar(barRect, valueRange, color, backgroundColor);
      }

      _adjustBarWidths();
   }

   ///
   /// <summary>
   /// Releases the allocated bars.
   /// </summary>
   ///
   virtual ~BarChart()
   {
      for (uint8_t i = 0; i < _numBars; i++)
      {
         delete _bars[i];
      }
      delete[] _bars;
   }

   ///
   /// <summary>
   /// Sets each visible bar's value from the given array and draws any that changed.
   /// </summary>
   /// <param name="display">Display to draw to.</param>
   /// <param name="values">Array of values indexed by bar; must have at least numBars entries.</param>
   ///
   void draw(LGFX* display, float values[])
   {
      for (uint16_t i = _visibleBars.min; i <= _visibleBars.max; i++)
      {
         _bars[i]->set(values[i]);
         _bars[i]->draw(display);
      }
   }

   ///
   /// <summary>
   /// Resets all bars so the next draw call performs a full redraw.
   /// </summary>
   ///
   void reset()
   {
      for (uint8_t i = 0; i < _numBars; i++)
      {
         _bars[i]->reset();
      }
   }

   ///
   /// <summary>
   /// Gets the total number of bars in the chart.
   /// </summary>
   /// <returns>The total number of bars.</returns>
   ///
   uint8_t getNumBars()
   {
      return _numBars;
   }

   ///
   /// <summary>
   /// Sets the range of bars to display and resizes them to fill the chart's rectangle.
   /// </summary>
   /// <param name="visibleBars">Zero-based inclusive range of bar indices to display.</param>
   ///
   void setVisibleBars(RangeU16 visibleBars)
   {
      _visibleBars = visibleBars;
      _adjustBarWidths();
      reset();
   }

   ///
   /// <summary>
   /// Gets the range of bars currently displayed.
   /// </summary>
   /// <returns>Zero-based inclusive range of visible bar indices.</returns>
   ///
   RangeU16 getVisibleBars()
   {
      return _visibleBars;
   }
};

