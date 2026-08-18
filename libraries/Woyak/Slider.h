#pragma once

#include "Structs.h"

///
/// <summary>
/// Base class for a value indicator drawn as a single moving marker within a fixed rect,
/// redrawing only the marker's old and new positions instead of the whole rect.
/// </summary>
///
class Slider
{
protected:
   Rect16 _rect;
   RangeF _range;
   float _lastValue;
   float _value;
   Color _color;
   Color _backgroundColor;

   ///
   /// <summary>
   /// Draws the marker for the current value, given the previous value in _lastValue.
   /// </summary>
   /// <param name="display">Display to draw to.</param>
   ///
   virtual void _draw(LGFX* display) = 0;

public:
   ///
   /// <summary>
   /// Creates a slider with the given position, value range, and colors.
   /// </summary>
   /// <param name="rect">Rectangle occupied by the slider.</param>
   /// <param name="range">Value range mapped across the slider's width.</param>
   /// <param name="color">Color of the marker.</param>
   /// <param name="backgroundColor">Background color of the slider.</param>
   ///
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

   ///
   /// <summary>
   /// Gets the current value.
   /// </summary>
   /// <returns>The current value.</returns>
   ///
   float get()
   {
      return _value;
   }

   ///
   /// <summary>
   /// Sets the current value.
   /// </summary>
   /// <param name="value">The new value.</param>
   ///
   void set(float value)
   {
      _value = value;
   }

   ///
   /// <summary>
   /// Changes the value range represented by the slider, resetting it if the range changed.
   /// </summary>
   /// <param name="range">The new value range.</param>
   ///
   void setRange(RangeF range)
   {
      if (range.min == _range.min && range.max == _range.max)
      {
         return;
      }

      _range = range;
      reset();
   }

   ///
   /// <summary>
   /// Gets the rectangle occupied by the slider.
   /// </summary>
   /// <returns>The slider's rectangle.</returns>
   ///
   Rect16 getRect()
   {
      return _rect;
   }

   ///
   /// <summary>
   /// Draws the slider, filling the rect red if the value is NaN, otherwise drawing only
   /// the changes since the last draw.
   /// </summary>
   /// <param name="display">Display to draw to.</param>
   ///
   void draw(LGFX* display)
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

   ///
   /// <summary>
   /// Resets the slider so the next draw() performs a full redraw.
   /// </summary>
   /// <param name="lastValue">Value to treat as the previous value (default NAN, forcing a full redraw).</param>
   ///
   void reset(float lastValue = NAN)
   {
      _lastValue = lastValue;
   }

   ///
   /// <summary>
   /// Changes the rectangle occupied by the slider, resetting it for a full redraw.
   /// </summary>
   /// <param name="rect">The new rectangle.</param>
   ///
   void setRect(Rect16 rect)
   {
      _rect = rect;
      reset();
   }
};


///
/// <summary>
/// Slider that draws its value marker as a vertical line moving horizontally across the rect.
/// </summary>
///
class HorizontalSlider : public Slider
{
protected:
   void _draw(LGFX* display)
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
   ///
   /// <summary>
   /// Creates a horizontal slider with the given position, value range, and colors.
   /// </summary>
   /// <param name="rect">Rectangle occupied by the slider.</param>
   /// <param name="range">Value range mapped across the slider's width.</param>
   /// <param name="color">Color of the marker.</param>
   /// <param name="backgroundColor">Background color of the slider.</param>
   ///
   HorizontalSlider(Rect16 rect, RangeF range, Color color, Color backgroundColor) : Slider(rect, range, color, backgroundColor)
   {
   }
};

