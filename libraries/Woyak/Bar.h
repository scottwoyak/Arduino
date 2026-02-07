#pragma once

#include <Adafruit_GFX.h> 

struct RangeF
{
   float min;
   float max;
};

struct Rect16
{
   uint16_t x;
   uint16_t y;
   uint16_t width;
   uint16_t height;
};

class HorizontalBar
{
private:
   Rect16 _rect;
   RangeF _range;
   uint16_t _lastLength = 0;
   Color _barColor;
   Color _backgroundColor;

public:
   HorizontalBar(Rect16 rect, RangeF range, Color barColor, Color backgroundColor)
   {
      _rect = rect;
      _range = range;
      _barColor = barColor;
      _backgroundColor = backgroundColor;
   }

   void draw(Adafruit_GFX* display, float value)
   {
      uint16_t length = constrain((value - _range.min) / (_range.max - _range.min), 0, 1) * _rect.width;

      if (length > _lastLength)
      {
         uint16_t x = _rect.x + _lastLength;
         uint16_t w = length - _lastLength;
         display->fillRect(x, _rect.y, w, _rect.height, (uint16_t)_barColor);
      }
      else
      {
         uint16_t x = length;
         uint16_t w = _lastLength - length;
         display->fillRect(x, _rect.y, w, _rect.height, (uint16_t)_backgroundColor);
      }

      _lastLength = length;
   }
};

class MultiHorizontalBar
{
private:
   HorizontalBar** _bars;
   uint _numBars;

public:
   MultiHorizontalBar(Rect16 rect, RangeF range, uint8_t numBars, Color startColor, Color endColor, Color backgroundColor)
   {
      _numBars = numBars;
      _bars = new HorizontalBar * [numBars];
      uint16_t rectDelta = rect.height / (numBars+1);
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
      _bars[numBars] = nullptr;
   }

   virtual ~MultiHorizontalBar()
   {
      for (uint i = 0; _bars[i] != nullptr; i++)
      {
         delete _bars[i];
      }
      delete _bars;
   }

   void draw(Adafruit_GFX* display, float value)
   {
      for (uint i = 0; i < _numBars; i++)
      {
         _bars[i]->draw(display, value);
      }
   }
};

