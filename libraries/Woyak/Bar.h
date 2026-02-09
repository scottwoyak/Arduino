#pragma once

#include "Structs.h"
#include <Adafruit_GFX.h> 

//-------------------------------------------------------------------------------------------------
class Bar
{
protected:
   Rect16 _rect;
   RangeF _range;
   float _lastValue;
   float _value;
   Color _barColor;
   Color _backgroundColor;

   virtual void _draw(Adafruit_GFX* display) = 0;

public:
   Bar(Rect16 rect, RangeF range, Color barColor, Color backgroundColor)
   {
      _rect = rect;
      _range = range;
      _lastValue = NAN;
      _value = NAN;
      _barColor = barColor;
      _backgroundColor = backgroundColor;
   }

   virtual ~Bar() {}

   float get()
   {
      return _value;
   }

   void set(float value)
   {
      _value = value;
   }

   Rect16 getRect()
   {
      return _rect;
   }

   void draw(Adafruit_GFX* display)
   {
      if (isnan(_value))
      {
         display->fillRect(_rect.x, _rect.y, _rect.width, _rect.height, (uint16_t)Color::RED);
      }
      else if (_value == _lastValue)
      {
         return;
      }
      else
      {
         _draw(display);
         _lastValue = _value;
      }
   }

   void reset(float lastValue=NAN)
   {
      _lastValue = lastValue;
   }

   void setRect(Rect16 rect)
   {
      _rect = rect;
      reset();
   }
};


//-------------------------------------------------------------------------------------------------
class HorizontalBar : public Bar
{
protected:
   void _draw(Adafruit_GFX* display)
   {
      if (isnan(_lastValue))
      {
         // full draw
         uint16_t length = constrain((_value - _range.min) / (_range.max - _range.min), 0, 1) * _rect.width;

         display->fillRect(_rect.x, _rect.y, length, _rect.height, (uint16_t)_barColor);
         display->fillRect(_rect.x + length, _rect.y, _rect.width - length, _rect.height, (uint16_t)_backgroundColor);
      }
      else
      {
         // just draw the changes
         uint16_t lastLength = constrain((_lastValue - _range.min) / (_range.max - _range.min), 0, 1) * _rect.width;
         uint16_t length = constrain((_value - _range.min) / (_range.max - _range.min), 0, 1) * _rect.width;

         if (length != lastLength)
         {
            uint16_t xNew = _rect.x + length;
            uint16_t xLast = _rect.x + lastLength;

            uint16_t x = min(xNew, xLast);
            uint16_t w = abs(xNew - xLast);
            Color fillColor = (xNew > xLast) ? _barColor : _backgroundColor;
            display->fillRect(x, _rect.y, w, _rect.height, (uint16_t)fillColor);
         }
      }
   }

public:
   HorizontalBar(Rect16 rect, RangeF range, Color barColor, Color backgroundColor) : Bar(rect, range, barColor, backgroundColor)
   {
   }
};

//-------------------------------------------------------------------------------------------------
class VerticalBar : public Bar
{
protected:
   void _draw(Adafruit_GFX* display)
   {
      if (isnan(_lastValue))
      {
         // full draw
         uint16_t height = constrain((_value - _range.min) / (_range.max - _range.min), 0, 1) * _rect.height;

         display->fillRect(_rect.x, _rect.y + _rect.height - height, _rect.width, height, (uint16_t)_barColor);
         display->fillRect(_rect.x, _rect.y, _rect.width, _rect.height - height, (uint16_t)_backgroundColor);
      }
      else
      {
         uint16_t lastHeight = constrain((_lastValue - _range.min) / (_range.max - _range.min), 0, 1) * _rect.height;
         uint16_t height = constrain((_value - _range.min) / (_range.max - _range.min), 0, 1) * _rect.height;

         if (height != lastHeight)
         {
            uint16_t yNew = (_rect.y + _rect.height) - height;
            uint16_t yLast = (_rect.y + _rect.height) - lastHeight;

            uint16_t y = min(yNew, yLast);
            uint16_t h = abs(yNew - yLast);
            Color fillColor = (yNew < yLast) ? _barColor : _backgroundColor;
            display->fillRect(_rect.x, y, _rect.width, h, (uint16_t)fillColor);
         }
      }
   }

public:
   VerticalBar(Rect16 rect, RangeF range, Color barColor, Color backgroundColor) : Bar(rect, range, barColor, backgroundColor)
   {
   }
};
