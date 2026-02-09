#pragma once

#include "BarChart.h"
#include "TimedHistogram.h"

class TimedHistogramChart
{
private:
   BarChart _chart;
   TimedHistogram _histogram;
   float* _values;
   RangeF _fullXRange;
   RangeF _visibleXRange;

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

   void set(float value)
   {
      _histogram.set(value);
   }

   void draw(Adafruit_GFX* display)
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