#pragma once

#include <stdint.h>

struct RangeF
{
   float min;
   float max;
};

struct RangeU16
{
   uint16_t min;
   uint16_t max;
};

struct Rect16
{
   uint16_t x;
   uint16_t y;
   uint16_t width;
   uint16_t height;

   ///
   /// <summary>
   /// Gets the X coordinate of the rectangle's left edge (x).
   /// </summary>
   /// <returns>The X coordinate of the left edge.</returns>
   ///
   uint16_t left() const
   {
      return x;
   }

   ///
   /// <summary>
   /// Gets the Y coordinate of the rectangle's top edge (y).
   /// </summary>
   /// <returns>The Y coordinate of the top edge.</returns>
   ///
   uint16_t top() const
   {
      return y;
   }

   ///
   /// <summary>
   /// Gets the X coordinate of the rectangle's right edge (x + width).
   /// </summary>
   /// <returns>The X coordinate of the right edge.</returns>
   ///
   uint16_t right() const
   {
      return x + width;
   }

   ///
   /// <summary>
   /// Gets the Y coordinate of the rectangle's bottom edge (y + height).
   /// </summary>
   /// <returns>The Y coordinate of the bottom edge.</returns>
   ///
   uint16_t bottom() const
   {
      return y + height;
   }
};

struct Point16
{
   int16_t x;
   int16_t y;

   Point16() = default;

   Point16(int16_t x, int16_t y) : x(x), y(y)
   {
   }
};

