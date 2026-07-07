#pragma once

#include "Color.h"
#include "Structs.h"

#ifndef LGFX_AUTODETECT
#define LGFX_AUTODETECT
#endif
#include <LovyanGFX.hpp>

///
/// <summary>
/// Base class for a graphical bar indicator that renders a value within a range.
/// </summary>
///
class Bar
{
protected:
   Rect16 _rect;
   RangeF _range;
   float _lastValue = 0;
   float _value = 0;
   Color _barColor;
   Color _backgroundColor;

   virtual void _draw(LGFX* display) = 0;

public:
   ///
   /// <summary>
   /// Initializes a new instance of the Bar class.
   /// </summary>
   /// <param name="rect">Bounding rectangle of the bar.</param>
   /// <param name="range">Value range mapped across the bar.</param>
   /// <param name="barColor">Color used to render the filled portion of the bar.</param>
   /// <param name="backgroundColor">Color used to render the unfilled portion of the bar.</param>
   ///
   Bar(Rect16 rect, RangeF range, Color barColor, Color backgroundColor)
   {
      _rect = rect;
      _range = range;
      _barColor = barColor;
      _backgroundColor = backgroundColor;
   }

   virtual ~Bar() = default;

   ///
   /// <summary>
   /// Gets the current value.
   /// </summary>
   /// <returns>The current value.</returns>
   ///
   float get() const
   {
      return _value;
   }

   ///
   /// <summary>
   /// Sets the current value to display.
   /// </summary>
   /// <param name="value">The new value.</param>
   ///
   void set(float value)
   {
      _value = value;
   }

   ///
   /// <summary>
   /// Gets the bounding rectangle of the bar.
   /// </summary>
   /// <returns>The bounding rectangle.</returns>
   ///
   Rect16 getRect() const
   {
      return _rect;
   }

   ///
   /// <summary>
   /// Draws the bar, redrawing only the changed portion since the last draw. If the value is NAN,
   /// the entire bar area is filled with red to indicate an invalid reading.
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
   /// Resets the last-drawn value so the next draw call performs a full redraw.
   /// </summary>
   /// <param name="lastValue">Value to treat as the last-drawn value; defaults to NAN to force a full redraw.</param>
   ///
   void reset(float lastValue = NAN)
   {
      _lastValue = lastValue;
   }

   ///
   /// <summary>
   /// Sets the bounding rectangle of the bar and resets it for a full redraw.
   /// </summary>
   /// <param name="rect">The new bounding rectangle.</param>
   ///
   void setRect(Rect16 rect)
   {
      _rect = rect;
      reset();
   }

   ///
   /// <summary>
   /// Sets the value range mapped across the bar and resets it for a full redraw.
   /// </summary>
   /// <param name="range">The new value range.</param>
   ///
   void setRange(RangeF range)
   {
      _range = range;
      reset();
   }

   ///
   /// <summary>
   /// Clears the bar to an empty state at the minimum of its current range and resets it
   /// for a full redraw.
   /// </summary>
   ///
   void clear()
   {
      _value = _range.min;
      reset();
   }
};

///
/// <summary>
/// Bar that fills horizontally from left to right based on the current value.
/// </summary>
///
class HorizontalBar : public Bar
{
protected:
   void _draw(LGFX* display)
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
   ///
   /// <summary>
   /// Initializes a new instance of the HorizontalBar class.
   /// </summary>
   /// <param name="rect">Bounding rectangle of the bar.</param>
   /// <param name="range">Value range mapped across the bar width.</param>
   /// <param name="barColor">Color used to render the filled portion of the bar.</param>
   /// <param name="backgroundColor">Color used to render the unfilled portion of the bar.</param>
   ///
   HorizontalBar(Rect16 rect, RangeF range, Color barColor, Color backgroundColor) : Bar(rect, range, barColor, backgroundColor)
   {
   }
};

///
/// <summary>
/// Bar that fills vertically from bottom to top based on the current value.
/// </summary>
///
class VerticalBar : public Bar
{
protected:
   void _draw(LGFX* display)
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
   ///
   /// <summary>
   /// Initializes a new instance of the VerticalBar class.
   /// </summary>
   /// <param name="rect">Bounding rectangle of the bar.</param>
   /// <param name="range">Value range mapped across the bar height.</param>
   /// <param name="barColor">Color used to render the filled portion of the bar.</param>
   /// <param name="backgroundColor">Color used to render the unfilled portion of the bar.</param>
   ///
   VerticalBar(Rect16 rect, RangeF range, Color barColor, Color backgroundColor) : Bar(rect, range, barColor, backgroundColor)
   {
   }
};
