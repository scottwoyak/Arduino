#pragma once

#include "BarChart.h"
#include "ColorRange.h"
#include "TimedHistogram.h"

class TimedHistogramChart
{
private:
   BarChart _chart;
   TimedHistogram _histogram;
   float* _values;
   RangeF _fullXRange;
   RangeF _visibleXRange;
   ColorRange* _colorRange = nullptr;

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

      uint8_t numBars = _chart.getNumBars();
      float binWidth = (_fullXRange.max - _fullXRange.min) / numBars;
      for (uint8_t i = 0; i < numBars; i++)
      {
         float binCenter = _fullXRange.min + (i + 0.5f) * binWidth;
         _chart.setBarColor(i, _colorRange->getColor(binCenter));
      }
   }

public:
   TimedHistogramChart(Rect16 rect, 
      RangeF range, 
      uint numBins, 
      uint ms, 
      Color color, 
      Color backgroundColor) : _chart(rect, numBins, RangeF(0,1), color, backgroundColor),
                               _histogram(range, numBins, ms)
   {
      // allocate values array
      _values = new float[numBins];
      _fullXRange = range;
      _visibleXRange = range;
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

   void draw(LGFX* display)
   {
      _histogram.get(_values);
      _chart.draw(display, _values);
   }

   void reset()
   {
      _chart.reset();
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
   }
};
