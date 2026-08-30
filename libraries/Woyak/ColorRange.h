#pragma once

#include "Color.h"
#include "Util.h"

///
/// <summary>
/// Maps numeric values to colors by interpolating between an ordered set of value/color
/// stops. Values at or before the first stop use that stop's color, values at or after
/// the last stop use that stop's color, and values between two stops are smoothly
/// blended between them.
/// </summary>
///
class ColorRange
{
private:
   struct Stop
   {
      float value;
      Color color;
   };

   static constexpr uint8_t MAX_STOPS = 8;
   Stop _stops[MAX_STOPS];
   uint8_t _numStops = 0;

public:
   ///
   /// <summary>
   /// Initializes a new instance of the ColorRange class with no stops. Use addStop() to
   /// define the value/color anchor points, in increasing order of value.
   /// </summary>
   ///
   ColorRange() = default;

   ///
   /// <summary>
   /// Adds a value/color anchor point. Stops must be added in increasing order of value.
   /// </summary>
   /// <param name="value">The value at which this color applies exactly.</param>
   /// <param name="color">The color to use at this value.</param>
   ///
   void addStop(float value, Color color)
   {
      ASSERT(_numStops < MAX_STOPS);

      _stops[_numStops].value = value;
      _stops[_numStops].color = color;
      _numStops++;
   }

   ///
   /// <summary>
   /// Gets the color for the given value, interpolating between the nearest surrounding
   /// stops.
   /// </summary>
   /// <param name="value">The value to look up.</param>
   /// <returns>The interpolated color.</returns>
   ///
   Color getColor(float value) const
   {
      ASSERT(_numStops > 0);

      if (value <= _stops[0].value)
      {
         return _stops[0].color;
      }

      if (value >= _stops[_numStops - 1].value)
      {
         return _stops[_numStops - 1].color;
      }

      for (uint8_t i = 0; i < _numStops - 1; i++)
      {
         if (value <= _stops[i + 1].value)
         {
#ifdef COLOR_565
            float ratio = (value - _stops[i].value) / (_stops[i + 1].value - _stops[i].value);
            return Color565::blend(_stops[i].color, _stops[i + 1].color, ratio);
#else
            return _stops[i].color;
#endif
         }
      }

      return _stops[_numStops - 1].color;
   }
};
