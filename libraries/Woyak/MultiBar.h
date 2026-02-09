#pragma once

#include "Bar.h"

//-------------------------------------------------------------------------------------------------
class MultiHorizontalBar
{
private:
   HorizontalBar** _bars;
   uint _numBars;
   bool _needsClearing = true;

public:
   MultiHorizontalBar(Rect16 rect, RangeF range, uint8_t numBars, Color startColor, Color endColor, Color backgroundColor)
   {
      _numBars = numBars;
      _bars = new HorizontalBar * [numBars];
      uint16_t rectDelta = rect.height / (numBars + 1);
      Color lastColor = backgroundColor;
      for (uint i = 0; i < numBars; i++)
      {
         Color barColor = (Color)Color565::blend(startColor, endColor, (i / (numBars - 1.0)));
         float min = range.min + ((float)i / numBars) * (range.max - range.min);
         float max = min + (range.max - range.min) / numBars;
         _bars[i] = new HorizontalBar(rect, RangeF(min, max), barColor, lastColor);

         lastColor = barColor;
         rect.y += rectDelta;
         rect.height -= rectDelta;
      }
   }

   virtual ~MultiHorizontalBar()
   {
      for (uint i = 0; _bars[i] != nullptr; i++)
      {
         delete _bars[i];
      }
      delete _bars;
   }

   void set(float value)
   {
      for (uint i = 0; i < _numBars; i++)
      {
         _bars[i]->set(value);
      }
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
      _bars[0]->reset();
      for (uint i = 1; i < _numBars; i++)
      {
         _bars[i]->reset(0);
      }
   }
};

