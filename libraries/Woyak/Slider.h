#pragma once

#include "Structs.h"
#include <Adafruit_GFX.h> 

//-------------------------------------------------------------------------------------------------
class Slider
{
protected:
   Rect16 _rect;
   RangeF _range;
   float _lastValue;
   float _value;
   Color _color;
   Color _backgroundColor;

   virtual void _draw(Adafruit_GFX* display) = 0;

public:
   Slider(Rect16 rect, RangeF range, Color color, Color backgroundColor)
   {
      _rect = rect;
      _range = range;
      _lastValue = NAN;
      _value = NAN;
      _color = color;
      _backgroundColor = backgroundColor;
   }

   virtual ~Slider() {}

   float get()
   {
      return _value;
   }

   void set(float value)
   {
      _value = value;
   }

   void setRange(RangeF range)
   {
      if (range.min == _range.min && range.max == _range.max)
      {
         return;
      }

      _range = range;
      reset();
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
class HorizontalSlider : public Slider
{
protected:
   void _draw(Adafruit_GFX* display)
   {
      if (isnan(_lastValue))
      {
         // full draw
         uint16_t pos = constrain((_value - _range.min) / (_range.max - _range.min), 0, 1) * _rect.width;

         display->fillRect(_rect.x, _rect.y, _rect.width, _rect.height, (uint16_t)_backgroundColor);
         display->fillRect(_rect.x + pos, _rect.y, 1, _rect.height, (uint16_t)_backgroundColor);
      }
      else
      {
         // just draw the changes
         uint16_t lastPos = constrain((_lastValue - _range.min) / (_range.max - _range.min), 0, 1) * _rect.width;
         uint16_t pos = constrain((_value - _range.min) / (_range.max - _range.min), 0, 1) * _rect.width;

         if (pos != lastPos)
         {
            display->fillRect(_rect.x + lastPos, _rect.y, 1, _rect.height, (uint16_t)_backgroundColor);
            display->fillRect(_rect.x + pos, _rect.y, 1, _rect.height, (uint16_t)_color);
         }
      }
   }

public:
   HorizontalSlider(Rect16 rect, RangeF range, Color color, Color backgroundColor) : Slider(rect, range, color, backgroundColor)
   {
   }
};
