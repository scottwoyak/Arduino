#pragma once

#include "Bar.h"
#include "ColorRange.h"

///
/// <summary>
/// Horizontally scrolling strip of single-pixel-wide bars, one per display column, that
/// shifts left as new values are added.
/// </summary>
///
class MovingBarChart
{
private:
   Bar** _bars;
   uint16_t _numBars;
   ColorRange* _colorRange = nullptr;
   bool _isCenterZero = false;

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
      _bars = new Bar*[_numBars];

      Rect16 barRect(rect.x, rect.y, 1, rect.height);
      for (uint16_t i = 0; i < _numBars; i++)
      {
         barRect.x = rect.x + i;
         _bars[i] = new VerticalBar(barRect, range, color, backgroundColor);
      }
   }

   ///
   /// <summary>
   /// Initializes a new center-zero instance of the MovingBarChart class, with one
   /// CenterZeroVerticalBar per pixel of width. Positive values fill upward from the
   /// vertical midpoint in the positive color; negative values fill downward in the
   /// negative color.
   /// </summary>
   /// <param name="rect">Bounding rectangle of the chart.</param>
   /// <param name="range">Symmetric value range mapped across each bar's height, with 0 at the vertical midpoint.</param>
   /// <param name="positiveColor">Color used to fill upward when the value is above zero.</param>
   /// <param name="negativeColor">Color used to fill downward when the value is below zero.</param>
   /// <param name="backgroundColor">Color used to render the unfilled portion of each bar.</param>
   /// <param name="centerLineColor">Color used to draw the horizontal zero line at the vertical midpoint.</param>
   ///
   MovingBarChart(
      Rect16 rect,
      RangeF range,
      Color positiveColor,
      Color negativeColor,
      Color backgroundColor,
      Color centerLineColor = Color::WHITE)
   {
      _isCenterZero = true;
      _numBars = rect.width;
      _bars = new Bar*[_numBars];

      Rect16 barRect(rect.x, rect.y, 1, rect.height);
      for (uint16_t i = 0; i < _numBars; i++)
      {
         barRect.x = rect.x + i;
         _bars[i] = new CenterZeroVerticalBar(barRect, range, positiveColor, negativeColor, backgroundColor, centerLineColor);
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
   /// Assigns a color range used to color each bar based on its value, overriding the
   /// single bar color passed to the constructor. Colors are re-applied whenever a bar's
   /// value shifts, adding a per-value cost; revert this call if it impacts performance.
   /// </summary>
   /// <param name="colorRange">The color range to use; must outlive this chart.</param>
   ///
   void setColorRange(ColorRange* colorRange)
   {
      _colorRange = colorRange;
      for (uint16_t i = 0; i < _numBars; i++)
      {
         _bars[i]->setColor(_colorRange->getColor(_bars[i]->get()));
      }
   }

   ///
   /// <summary>
   /// Configures the secondary top (upward) and bottom (downward) fill colors on all bars
   /// in a center-zero chart.
   /// </summary>
   /// <param name="topFillColor">Color of the secondary top (upward) fill.</param>
   /// <param name="bottomFillColor">Color of the secondary bottom (downward) fill.</param>
   ///
   void setSecondaryColors(Color topFillColor, Color bottomFillColor)
   {
      if (!_isCenterZero)
      {
         return;
      }

      for (uint16_t i = 0; i < _numBars; i++)
      {
         static_cast<CenterZeroVerticalBar*>(_bars[i])->setSecondaryColors(topFillColor, bottomFillColor);
      }
   }

   ///
   /// <summary>
   /// Shifts all bars left by one and sets the newest (rightmost) bar to the given value.
   /// </summary>
   /// <param name="value">The new value for the rightmost bar.</param>
   ///
   void set(float value)
   {
      set(value, NAN, NAN);
   }

   ///
   /// <summary>
   /// Shifts all bars left by one and sets the newest (rightmost) bar's main signed value
   /// along with its secondary top (upward) and bottom (downward) magnitudes.
   /// </summary>
   /// <param name="value">The new signed value for the rightmost bar.</param>
   /// <param name="topValue">Non-negative magnitude for the secondary top fill, or NAN to hide.</param>
   /// <param name="bottomValue">Non-negative magnitude for the secondary bottom fill, or NAN to hide.</param>
   ///
   void set(float value, float topValue, float bottomValue)
   {
      for (uint16_t i = 0; i < _numBars - 1; i++)
      {
         _bars[i]->set(_bars[i + 1]->get());
         if (_isCenterZero)
         {
            CenterZeroVerticalBar* dst = static_cast<CenterZeroVerticalBar*>(_bars[i]);
            CenterZeroVerticalBar* src = static_cast<CenterZeroVerticalBar*>(_bars[i + 1]);
            dst->setTop(src->getTop());
            dst->setBottom(src->getBottom());
         }
         if (_colorRange != nullptr)
         {
            _bars[i]->setColor(_bars[i + 1]->getColor());
         }
      }
      setAt(_numBars - 1, value, topValue, bottomValue);
   }

   ///
   /// <summary>
   /// Sets the main signed value and secondary top (upward) and bottom (downward)
   /// magnitudes at a specific column index without shifting the chart.
   /// </summary>
   /// <param name="index">Zero-based bar column index.</param>
   /// <param name="value">The signed value for the column.</param>
   /// <param name="topValue">Non-negative magnitude for the secondary top fill, or NAN to hide.</param>
   /// <param name="bottomValue">Non-negative magnitude for the secondary bottom fill, or NAN to hide.</param>
   ///
   void setAt(uint16_t index, float value, float topValue = NAN, float bottomValue = NAN)
   {
      if (index >= _numBars)
      {
         return;
      }

      _bars[index]->set(value);
      if (_isCenterZero)
      {
         CenterZeroVerticalBar* bar = static_cast<CenterZeroVerticalBar*>(_bars[index]);
         bar->setTop(topValue);
         bar->setBottom(bottomValue);
      }
      if (_colorRange != nullptr)
      {
         _bars[index]->setColor(_colorRange->getColor(value));
      }
   }

   ///
   /// <summary>
   /// Gets the number of single-pixel bar columns in the chart.
   /// </summary>
   /// <returns>The number of bar columns.</returns>
   ///
   uint16_t getNumBars() const
   {
      return _numBars;
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
