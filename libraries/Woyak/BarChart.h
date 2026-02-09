#pragma once

#include "Bar.h"
//-------------------------------------------------------------------------------------------------
class BarChart
{
private:
   VerticalBar** _bars;
   uint _numBars;
   Rect16 _rect;
   RangeU16 _visibleBars;
   RangeF _valueRange;

   void _adjustBarWidths()
   {
      // adjust the bar rects
      uint numVisibleBars = _visibleBars.max - _visibleBars.min + 1;
      float barWidth = ((float) _rect.width) / numVisibleBars;
      Rect16 barRect(_rect.x, _rect.y, barWidth, _rect.height);
      for (uint i = _visibleBars.min; i <= _visibleBars.max; i++)
      {
         uint16_t x = floor(_rect.x + i * barWidth);
         uint16_t w = floor(_rect.x + (i + 1) * barWidth) - x;
         barRect.x = x;
         barRect.width = w;
         _bars[i]->setRect(barRect);
      }
   }

public:
   BarChart(Rect16 rect, uint8_t numBars, RangeF valueRange, Color color, Color backgroundColor)
   {
      _rect = rect;
      _numBars = numBars;
      _bars = new VerticalBar * [_numBars];
      _visibleBars = RangeU16(0, _numBars - 1);
      _valueRange = valueRange;

      uint8_t barWidth = _rect.width / _numBars;
      Rect16 barRect(_rect.x, _rect.y, barWidth, _rect.height);
      for (uint i = 0; i < _numBars; i++)
      {
         barRect.x = rect.x + i * barWidth;
         _bars[i] = new VerticalBar(barRect, valueRange, color, backgroundColor);
      }

      _adjustBarWidths();
   }

   virtual ~BarChart()
   {
      for (uint i = 0; _bars[i] != nullptr; i++)
      {
         delete _bars[i];
      }
      delete _bars;
   }

   void draw(Adafruit_GFX* display, float values[])
   {
      for (uint i = _visibleBars.min; i <= _visibleBars.max; i++)
      {
         _bars[i]->set(values[i]);
         _bars[i]->draw(display);
      }
   }

   void reset()
   {
      for (uint i = 0; i < _numBars; i++)
      {
         _bars[i]->reset();
      }
   }

   uint getNumBars()
   {
      return _numBars;
   }

   void setVisibleBars(RangeU16 visibleBars)
   {
      _visibleBars = visibleBars;
      _adjustBarWidths();
      reset();
   }

   RangeU16 getVisibleBars()
   {
      return _visibleBars;
   }
};

//-------------------------------------------------------------------------------------------------
class RollingBarChart
{
private:
   VerticalBar** _bars;
   uint _numBars;

public:
   RollingBarChart(Rect16 rect, RangeF range, Color color, Color backgroundColor)
   {
      _numBars = rect.width;
      _bars = new VerticalBar * [_numBars];

      Rect16 barRect = { rect.x, rect.y, 1, rect.height };
      for (uint i = 0; i < _numBars; i++)
      {
         barRect.x = rect.x + i;
         _bars[i] = new VerticalBar(barRect, range, color, backgroundColor);
      }
   }

   virtual ~RollingBarChart()
   {
      for (uint i = 0; _bars[i] != nullptr; i++)
      {
         delete _bars[i];
      }
      delete _bars;
   }

   void set(float value)
   {
      for (uint i = 0; i < _numBars - 1; i++)
      {
         _bars[i]->set(_bars[i + 1]->get());
      }
      _bars[_numBars - 1]->set(value);
   }

   void draw(Adafruit_GFX* display)
   {
      for (uint i = 0; i < _numBars; i++)
      {
         _bars[i]->draw(display);
      }
   }

   void reset()
   {
      for (uint i = 0; i < _numBars; i++)
      {
         _bars[i]->reset();
      }
   }
};

