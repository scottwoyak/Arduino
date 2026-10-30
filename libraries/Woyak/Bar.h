#pragma once

#include "ColorX.h"
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
   virtual void draw(LGFX* display)
   {
      if (isnan(_value))
      {
         display->fillRect(_rect.x, _rect.y, _rect.width, _rect.height, (uint16_t)Color::RED);
         _lastValue = NAN;
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
   virtual void clear()
   {
      _value = _range.min;
      reset();
   }

   ///
   /// <summary>
   /// Gets the color used to render the filled portion of the bar.
   /// </summary>
   /// <returns>The current bar color.</returns>
   ///
   Color getColor() const
   {
      return _barColor;
   }

   ///
   /// <summary>
   /// Sets the color used to render the filled portion of the bar and resets it for a
   /// full redraw, but only if the color actually changed.
   /// </summary>
   /// <param name="barColor">The new bar color.</param>
   ///
   void setColor(Color barColor)
   {
      if (barColor != _barColor)
      {
         _barColor = barColor;
         reset();
      }
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

///
/// <summary>
/// Horizontal bar with zero at its horizontal midpoint. Positive values fill rightward
/// from the center line in the positive color; negative values fill leftward from the
/// center line in the negative color. Optionally renders secondary right (positive) and
/// left (negative) magnitude fills behind the main signed value.
/// </summary>
///
class CenterZeroHorizontalBar : public Bar
{
protected:
   Color _negativeColor;
   Color _centerLineColor;
   bool _showCenterLine = true;
   float _rightValue = NAN;
   float _lastRightValue = NAN;
   float _leftValue = NAN;
   float _lastLeftValue = NAN;
   Color _rightFillColor = Color::DARKGREEN;
   Color _leftFillColor = Color565::fromRGB(80, 0, 0);

   void _draw(LGFX* display) override
   {
      int16_t centerX = _rect.x + _rect.width / 2;
      int16_t leftHalfWidth = centerX - _rect.x;
      int16_t rightStartX = _showCenterLine ? (centerX + 1) : centerX;
      int16_t rightHalfWidth = (_rect.x + _rect.width) - rightStartX;

      float maxAbsValue = max(fabsf(_range.min), fabsf(_range.max));
      float normalized = (maxAbsValue > 0.0f && !isnan(_value))
         ? constrain(_value / maxAbsValue, -1.0f, 1.0f)
         : 0.0f;

      float rightNorm = (maxAbsValue > 0.0f && !isnan(_rightValue) && _rightValue > 0.0f)
         ? constrain(_rightValue / maxAbsValue, 0.0f, 1.0f)
         : 0.0f;
      float leftNorm = (maxAbsValue > 0.0f && !isnan(_leftValue) && _leftValue > 0.0f)
         ? constrain(_leftValue / maxAbsValue, 0.0f, 1.0f)
         : 0.0f;

      int16_t netLeftW = normalized < 0.0f ? static_cast<int16_t>((-normalized) * leftHalfWidth + 0.5f) : 0;
      int16_t secLeftW = static_cast<int16_t>(leftNorm * leftHalfWidth + 0.5f);
      int16_t maxLeftW = max(netLeftW, secLeftW);
      int16_t emptyLeftW = leftHalfWidth - maxLeftW;

      if (emptyLeftW > 0)
      {
         display->fillRect(_rect.x, _rect.y, emptyLeftW, _rect.height, static_cast<uint16_t>(_backgroundColor));
      }
      if (secLeftW > netLeftW)
      {
         display->fillRect(_rect.x + emptyLeftW, _rect.y, secLeftW - netLeftW, _rect.height, static_cast<uint16_t>(_leftFillColor));
      }
      if (netLeftW > 0)
      {
         display->fillRect(centerX - netLeftW, _rect.y, netLeftW, _rect.height, static_cast<uint16_t>(_negativeColor));
      }

      int16_t netRightW = normalized > 0.0f ? static_cast<int16_t>(normalized * rightHalfWidth + 0.5f) : 0;
      int16_t secRightW = static_cast<int16_t>(rightNorm * rightHalfWidth + 0.5f);
      int16_t maxRightW = max(netRightW, secRightW);
      int16_t emptyRightW = rightHalfWidth - maxRightW;

      if (netRightW > 0)
      {
         display->fillRect(rightStartX, _rect.y, netRightW, _rect.height, static_cast<uint16_t>(_barColor));
      }
      if (secRightW > netRightW)
      {
         display->fillRect(rightStartX + netRightW, _rect.y, secRightW - netRightW, _rect.height, static_cast<uint16_t>(_rightFillColor));
      }
      if (emptyRightW > 0)
      {
         display->fillRect(rightStartX + maxRightW, _rect.y, emptyRightW, _rect.height, static_cast<uint16_t>(_backgroundColor));
      }

      if (_showCenterLine)
      {
         display->drawFastVLine(centerX, _rect.y, _rect.height, static_cast<uint16_t>(_centerLineColor));
      }
   }

public:
   ///
   /// <summary>
   /// Initializes a new instance of the CenterZeroHorizontalBar class.
   /// </summary>
   /// <param name="rect">Bounding rectangle of the bar.</param>
   /// <param name="range">Symmetric value range mapped across the bar width, with 0 at the horizontal midpoint.</param>
   /// <param name="positiveColor">Color used to fill rightward when the value is above zero.</param>
   /// <param name="negativeColor">Color used to fill leftward when the value is below zero.</param>
   /// <param name="backgroundColor">Color used to render the unfilled portion of the bar.</param>
   /// <param name="centerLineColor">Color used to draw the vertical zero line at the horizontal midpoint.</param>
   ///
   CenterZeroHorizontalBar(
      Rect16 rect,
      RangeF range,
      Color positiveColor,
      Color negativeColor,
      Color backgroundColor = Color::BLACK,
      Color centerLineColor = Color::WHITE)
      : Bar(rect, range, positiveColor, backgroundColor)
   {
      _negativeColor = negativeColor;
      _centerLineColor = centerLineColor;
   }

   ///
   /// <summary>
   /// Configures the colors used for the optional secondary right (positive) and left
   /// (negative) magnitude fills rendered behind the main signed value.
   /// </summary>
   /// <param name="rightFillColor">Color of the secondary right (positive) fill.</param>
   /// <param name="leftFillColor">Color of the secondary left (negative) fill.</param>
   ///
   void setSecondaryColors(Color rightFillColor, Color leftFillColor)
   {
      _rightFillColor = rightFillColor;
      _leftFillColor = leftFillColor;
      reset();
   }

   ///
   /// <summary>
   /// Sets whether the internal vertical center line is drawn at the horizontal midpoint.
   /// </summary>
   /// <param name="showCenterLine">True to draw the center line, false to omit it.</param>
   ///
   void setShowCenterLine(bool showCenterLine)
   {
      _showCenterLine = showCenterLine;
      reset();
   }

   ///
   /// <summary>
   /// Sets the non-negative magnitude for the secondary right (positive) fill.
   /// </summary>
   /// <param name="value">Non-negative magnitude to the right of the center line, or NAN to hide.</param>
   ///
   void setRight(float value)
   {
      _rightValue = value;
   }

   ///
   /// <summary>
   /// Sets the non-negative magnitude for the secondary left (negative) fill.
   /// </summary>
   /// <param name="value">Non-negative magnitude to the left of the center line, or NAN to hide.</param>
   ///
   void setLeft(float value)
   {
      _leftValue = value;
   }

   ///
   /// <summary>
   /// Clears the bar and any secondary fills to zero at the center line and resets it for a full redraw.
   /// </summary>
   ///
   void clear() override
   {
      _value = 0.0f;
      _rightValue = NAN;
      _lastRightValue = NAN;
      _leftValue = NAN;
      _lastLeftValue = NAN;
      reset();
   }

   ///
   /// <summary>
   /// Draws the bar if the main value or either secondary value has changed since the last draw.
   /// </summary>
   /// <param name="display">Target display instance.</param>
   ///
   void draw(LGFX* display) override
   {
      if (isnan(_value))
      {
         display->fillRect(_rect.x, _rect.y, _rect.width, _rect.height, static_cast<uint16_t>(Color::RED));
         _lastValue = NAN;
         return;
      }

      bool rightUnchanged = (_rightValue == _lastRightValue) || (isnan(_rightValue) && isnan(_lastRightValue));
      bool leftUnchanged = (_leftValue == _lastLeftValue) || (isnan(_leftValue) && isnan(_lastLeftValue));
      if (_value == _lastValue && rightUnchanged && leftUnchanged)
      {
         return;
      }

      _draw(display);
      _lastValue = _value;
      _lastRightValue = _rightValue;
      _lastLeftValue = _leftValue;
   }
};

///
/// <summary>
/// Vertical bar with zero at its vertical midpoint. Positive values fill upward from the
/// center line in the positive color; negative values fill downward from the center line
/// in the negative color.
/// </summary>
///
class CenterZeroVerticalBar : public Bar
{
protected:
   Color _negativeColor;
   Color _centerLineColor;
   float _topValue = NAN;
   float _lastTopValue = NAN;
   float _bottomValue = NAN;
   float _lastBottomValue = NAN;
   Color _topFillColor = Color::DARKGREEN;
   Color _bottomFillColor = Color565::fromRGB(80, 0, 0);

   void _draw(LGFX* display) override
   {
      int16_t centerY = _rect.y + _rect.height / 2;
      int16_t topHalfHeight = centerY - _rect.y;
      int16_t bottomStartY = centerY + 1;
      int16_t bottomHalfHeight = (_rect.y + _rect.height) - bottomStartY;

      float maxAbsValue = max(fabsf(_range.min), fabsf(_range.max));
      float normalized = (maxAbsValue > 0.0f && !isnan(_value))
         ? constrain(_value / maxAbsValue, -1.0f, 1.0f)
         : 0.0f;

      float topNorm = (maxAbsValue > 0.0f && !isnan(_topValue) && _topValue > 0.0f)
         ? constrain(_topValue / maxAbsValue, 0.0f, 1.0f)
         : 0.0f;
      float bottomNorm = (maxAbsValue > 0.0f && !isnan(_bottomValue) && _bottomValue > 0.0f)
         ? constrain(_bottomValue / maxAbsValue, 0.0f, 1.0f)
         : 0.0f;

      int16_t netTopH = normalized > 0.0f ? static_cast<int16_t>(normalized * topHalfHeight + 0.5f) : 0;
      int16_t secTopH = static_cast<int16_t>(topNorm * topHalfHeight + 0.5f);
      int16_t maxTopH = max(netTopH, secTopH);
      int16_t emptyTopH = topHalfHeight - maxTopH;

      if (emptyTopH > 0)
      {
         display->fillRect(_rect.x, _rect.y, _rect.width, emptyTopH, static_cast<uint16_t>(_backgroundColor));
      }
      if (secTopH > netTopH)
      {
         display->fillRect(_rect.x, _rect.y + emptyTopH, _rect.width, secTopH - netTopH, static_cast<uint16_t>(_topFillColor));
      }
      if (netTopH > 0)
      {
         display->fillRect(_rect.x, centerY - netTopH, _rect.width, netTopH, static_cast<uint16_t>(_barColor));
      }

      int16_t netBottomH = normalized < 0.0f ? static_cast<int16_t>((-normalized) * bottomHalfHeight + 0.5f) : 0;
      int16_t secBottomH = static_cast<int16_t>(bottomNorm * bottomHalfHeight + 0.5f);
      int16_t maxBottomH = max(netBottomH, secBottomH);
      int16_t emptyBottomH = bottomHalfHeight - maxBottomH;

      if (netBottomH > 0)
      {
         display->fillRect(_rect.x, bottomStartY, _rect.width, netBottomH, static_cast<uint16_t>(_negativeColor));
      }
      if (secBottomH > netBottomH)
      {
         display->fillRect(_rect.x, bottomStartY + netBottomH, _rect.width, secBottomH - netBottomH, static_cast<uint16_t>(_bottomFillColor));
      }
      if (emptyBottomH > 0)
      {
         display->fillRect(_rect.x, bottomStartY + maxBottomH, _rect.width, emptyBottomH, static_cast<uint16_t>(_backgroundColor));
      }

      display->drawFastHLine(_rect.x, centerY, _rect.width, static_cast<uint16_t>(_centerLineColor));
   }

public:
   ///
   /// <summary>
   /// Initializes a new instance of the CenterZeroVerticalBar class.
   /// </summary>
   /// <param name="rect">Bounding rectangle of the bar.</param>
   /// <param name="range">Symmetric value range mapped across the bar height, with 0 at the vertical midpoint.</param>
   /// <param name="positiveColor">Color used to fill upward when the value is above zero.</param>
   /// <param name="negativeColor">Color used to fill downward when the value is below zero.</param>
   /// <param name="backgroundColor">Color used to render the unfilled portion of the bar.</param>
   /// <param name="centerLineColor">Color used to draw the horizontal zero line at the vertical midpoint.</param>
   ///
   CenterZeroVerticalBar(
      Rect16 rect,
      RangeF range,
      Color positiveColor,
      Color negativeColor,
      Color backgroundColor = Color::BLACK,
      Color centerLineColor = Color::WHITE)
      : Bar(rect, range, positiveColor, backgroundColor)
   {
      _negativeColor = negativeColor;
      _centerLineColor = centerLineColor;
   }

   ///
   /// <summary>
   /// Configures the colors used for the optional secondary top (upward) and bottom
   /// (downward) magnitude fills rendered behind the main signed value.
   /// </summary>
   /// <param name="topFillColor">Color of the secondary top (upward) fill.</param>
   /// <param name="bottomFillColor">Color of the secondary bottom (downward) fill.</param>
   ///
   void setSecondaryColors(Color topFillColor, Color bottomFillColor)
   {
      _topFillColor = topFillColor;
      _bottomFillColor = bottomFillColor;
      reset();
   }

   ///
   /// <summary>
   /// Sets the non-negative magnitude for the secondary top (upward) fill.
   /// </summary>
   /// <param name="value">Non-negative magnitude above the center line, or NAN to hide.</param>
   ///
   void setTop(float value)
   {
      _topValue = value;
   }

   ///
   /// <summary>
   /// Sets the non-negative magnitude for the secondary bottom (downward) fill.
   /// </summary>
   /// <param name="value">Non-negative magnitude below the center line, or NAN to hide.</param>
   ///
   void setBottom(float value)
   {
      _bottomValue = value;
   }

   ///
   /// <summary>
   /// Clears the bar and any secondary fills to zero at the center line and resets it for a full redraw.
   /// </summary>
   ///
   void clear() override
   {
      _value = 0.0f;
      _topValue = NAN;
      _lastTopValue = NAN;
      _bottomValue = NAN;
      _lastBottomValue = NAN;
      reset();
   }

   ///
   /// <summary>
   /// Draws the bar if the main value or either secondary value has changed since the last draw.
   /// </summary>
   /// <param name="display">Target display instance.</param>
   ///
   void draw(LGFX* display) override
   {
      if (isnan(_value))
      {
         display->fillRect(_rect.x, _rect.y, _rect.width, _rect.height, static_cast<uint16_t>(Color::RED));
         _lastValue = NAN;
         return;
      }

      bool topUnchanged = (_topValue == _lastTopValue) || (isnan(_topValue) && isnan(_lastTopValue));
      bool bottomUnchanged = (_bottomValue == _lastBottomValue) || (isnan(_bottomValue) && isnan(_lastBottomValue));
      if (_value == _lastValue && topUnchanged && bottomUnchanged)
      {
         return;
      }

      _draw(display);
      _lastValue = _value;
      _lastTopValue = _topValue;
      _lastBottomValue = _bottomValue;
   }
};

///
/// <summary>
/// Vertical bar with zero at its vertical midpoint that simultaneously displays two
/// independent magnitudes: one filling upward from the center line in the top color, and
/// another filling downward from the center line in the bottom color.
/// </summary>
///
class DualCenterZeroVerticalBar : public Bar
{
protected:
   float _bottomValue = NAN;
   float _lastBottomValue = NAN;
   Color _bottomColor;
   Color _centerLineColor;

   void _draw(LGFX* display) override
   {
      int16_t centerY = _rect.y + _rect.height / 2;
      int16_t topHalfHeight = centerY - _rect.y;
      int16_t bottomStartY = centerY + 1;
      int16_t bottomHalfHeight = (_rect.y + _rect.height) - bottomStartY;

      float maxAbsValue = max(fabsf(_range.min), fabsf(_range.max));
      float topNormalized = (maxAbsValue > 0.0f && !isnan(_value))
         ? constrain(_value / maxAbsValue, 0.0f, 1.0f)
         : 0.0f;
      float bottomNormalized = (maxAbsValue > 0.0f && !isnan(_bottomValue))
         ? constrain(_bottomValue / maxAbsValue, 0.0f, 1.0f)
         : 0.0f;

      int16_t topFillH = static_cast<int16_t>(topNormalized * topHalfHeight + 0.5f);
      int16_t emptyTopH = topHalfHeight - topFillH;

      if (emptyTopH > 0)
      {
         display->fillRect(_rect.x, _rect.y, _rect.width, emptyTopH, static_cast<uint16_t>(_backgroundColor));
      }
      if (topFillH > 0)
      {
         display->fillRect(_rect.x, _rect.y + emptyTopH, _rect.width, topFillH, static_cast<uint16_t>(_barColor));
      }

      int16_t bottomFillH = static_cast<int16_t>(bottomNormalized * bottomHalfHeight + 0.5f);
      int16_t emptyBottomH = bottomHalfHeight - bottomFillH;

      if (bottomFillH > 0)
      {
         display->fillRect(_rect.x, bottomStartY, _rect.width, bottomFillH, static_cast<uint16_t>(_bottomColor));
      }
      if (emptyBottomH > 0)
      {
         display->fillRect(_rect.x, bottomStartY + bottomFillH, _rect.width, emptyBottomH, static_cast<uint16_t>(_backgroundColor));
      }

      display->drawFastHLine(_rect.x, centerY, _rect.width, static_cast<uint16_t>(_centerLineColor));
   }

public:
   ///
   /// <summary>
   /// Initializes a new instance of the DualCenterZeroVerticalBar class.
   /// </summary>
   /// <param name="rect">Bounding rectangle of the bar.</param>
   /// <param name="range">Symmetric value range mapped across each half of the bar height, with 0 at the vertical midpoint.</param>
   /// <param name="topColor">Color used to fill upward from the center line for the top value.</param>
   /// <param name="bottomColor">Color used to fill downward from the center line for the bottom value.</param>
   /// <param name="backgroundColor">Color used to render the unfilled portions of the bar.</param>
   /// <param name="centerLineColor">Color used to draw the horizontal zero line at the vertical midpoint.</param>
   ///
   DualCenterZeroVerticalBar(
      Rect16 rect,
      RangeF range,
      Color topColor,
      Color bottomColor,
      Color backgroundColor = Color::BLACK,
      Color centerLineColor = Color::WHITE)
      : Bar(rect, range, topColor, backgroundColor)
   {
      _bottomColor = bottomColor;
      _centerLineColor = centerLineColor;
   }

   ///
   /// <summary>
   /// Sets both the top (upward) and bottom (downward) magnitudes.
   /// </summary>
   /// <param name="topValue">Non-negative magnitude to fill upward from the center line.</param>
   /// <param name="bottomValue">Non-negative magnitude to fill downward from the center line.</param>
   ///
   void set(float topValue, float bottomValue)
   {
      _value = topValue;
      _bottomValue = bottomValue;
   }

   ///
   /// <summary>
   /// Sets the top (upward) magnitude while preserving the current bottom magnitude.
   /// </summary>
   /// <param name="topValue">Non-negative magnitude to fill upward from the center line.</param>
   ///
   void setTop(float topValue)
   {
      _value = topValue;
   }

   ///
   /// <summary>
   /// Sets the bottom (downward) magnitude while preserving the current top magnitude.
   /// </summary>
   /// <param name="bottomValue">Non-negative magnitude to fill downward from the center line.</param>
   ///
   void setBottom(float bottomValue)
   {
      _bottomValue = bottomValue;
   }

   ///
   /// <summary>
   /// Clears both halves of the bar to zero and resets it for a full redraw.
   /// </summary>
   ///
   void clear() override
   {
      _value = 0.0f;
      _bottomValue = 0.0f;
      _lastBottomValue = NAN;
      reset();
   }

   ///
   /// <summary>
   /// Draws the bar if either the top or bottom value has changed since the last draw.
   /// </summary>
   /// <param name="display">Target display instance.</param>
   ///
   void draw(LGFX* display) override
   {
      if (_value == _lastValue && _bottomValue == _lastBottomValue && (!isnan(_lastValue) || !isnan(_lastBottomValue)))
      {
         return;
      }

      _draw(display);
      _lastValue = _value;
      _lastBottomValue = _bottomValue;
   }
};
