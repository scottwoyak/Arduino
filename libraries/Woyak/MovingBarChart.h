#pragma once

#include "Bar.h"

///
/// <summary>
/// Horizontally scrolling strip of single-pixel-wide bars, one per display column, that
/// shifts left as new values are added.
/// </summary>
///
class MovingBarChart
{
private:
   VerticalBar** _bars;
   uint16_t _numBars;

public:
   ///
   /// <summary>
   /// Initializes a new instance of the MovingBarChart class, with one bar per pixel of width.
   /// </summary>
   /// <param name="rect">Bounding rectangle of the chart.</param>
   /// <param name="range">Value range mapped across each bar's height.</param>
   /// <param name="color">Color used to render the filled portion of each bar.</param>
   /// <param name="backgroundColor">Color used to render the unfilled portion of each bar.</param>
   ///
   MovingBarChart(Rect16 rect, RangeF range, Color color, Color backgroundColor)
   {
      _numBars = rect.width;
      _bars = new VerticalBar*[_numBars];

      Rect16 barRect(rect.x, rect.y, 1, rect.height);
      for (uint16_t i = 0; i < _numBars; i++)
      {
         barRect.x = rect.x + i;
         _bars[i] = new VerticalBar(barRect, range, color, backgroundColor);
      }
   }

   ///
   /// <summary>
   /// Releases the allocated bars.
   /// </summary>
   ///
   virtual ~MovingBarChart()
   {
      for (uint16_t i = 0; i < _numBars; i++)
      {
         delete _bars[i];
      }
      delete[] _bars;
   }

   ///
   /// <summary>
   /// Shifts all bars left by one and sets the newest (rightmost) bar to the given value.
   /// </summary>
   /// <param name="value">The new value for the rightmost bar.</param>
   ///
   void set(float value)
   {
      for (uint16_t i = 0; i < _numBars - 1; i++)
      {
         _bars[i]->set(_bars[i + 1]->get());
      }
      _bars[_numBars - 1]->set(value);
   }

   ///
   /// <summary>
   /// Draws any bars that changed since the last draw.
   /// </summary>
   /// <param name="display">Display to draw to.</param>
   ///
   void draw(LGFX* display)
   {
      for (uint16_t i = 0; i < _numBars; i++)
      {
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
      for (uint16_t i = 0; i < _numBars; i++)
      {
         _bars[i]->reset();
      }
   }

   ///
   /// <summary>
   /// Sets the value range mapped across each bar's height and resets for a full redraw.
   /// </summary>
   /// <param name="range">The new value range.</param>
   ///
   void setRange(RangeF range)
   {
      for (uint16_t i = 0; i < _numBars; i++)
      {
         _bars[i]->setRange(range);
      }
   }

   ///
   /// <summary>
   /// Clears all bars to an empty state at the minimum of the current range.
   /// </summary>
   ///
   void clear()
   {
      for (uint16_t i = 0; i < _numBars; i++)
      {
         _bars[i]->clear();
      }
   }
};
