#pragma once

#include <cmath>

#include <LovyanGFX.hpp>

///
/// <summary>
/// An off-screen sprite covering a fixed region of a display. Content is composed in the
/// sprite and pushed to the display in one operation (avoiding flicker), optionally
/// limited to a changed ("dirty") rectangle to reduce the amount of data transferred.
/// </summary>
///
class DirtySprite
{
private:
   lgfx::LGFX_Sprite* _sprite = nullptr;
   lgfx::LovyanGFX* _display = nullptr;
   int16_t _x = 0;
   int16_t _y = 0;

public:
   ///
   /// <summary>
   /// Creates the sprite (in internal RAM if possible, otherwise PSRAM). Does nothing if
   /// already created.
   /// </summary>
   /// <param name="display">Display the sprite is pushed to.</param>
   /// <param name="x">Display X of the sprite's left edge.</param>
   /// <param name="y">Display Y of the sprite's top edge.</param>
   /// <param name="width">Sprite width in pixels.</param>
   /// <param name="height">Sprite height in pixels.</param>
   ///
   void begin(lgfx::LovyanGFX* display, int16_t x, int16_t y, int16_t width, int16_t height)
   {
      if (_sprite != nullptr)
      {
         return;
      }

      _display = display;
      _x = x;
      _y = y;
      _sprite = new lgfx::LGFX_Sprite(display);
      _sprite->setColorDepth(16);
      _sprite->setPsram(false);
      if (!_sprite->createSprite(width, height))
      {
         _sprite->setPsram(true);
         _sprite->createSprite(width, height);
      }
   }

   ///
   /// <summary>
   /// Gets whether begin() has been called.
   /// </summary>
   /// <returns>True if the sprite exists.</returns>
   ///
   bool isCreated() const
   {
      return _sprite != nullptr;
   }

   ///
   /// <summary>
   /// Gets the underlying sprite for drawing. Coordinates are relative to the sprite.
   /// </summary>
   /// <returns>The sprite.</returns>
   ///
   lgfx::LGFX_Sprite* sprite()
   {
      return _sprite;
   }

   ///
   /// <summary>
   /// Gets the display X of the sprite's left edge.
   /// </summary>
   /// <returns>Display X.</returns>
   ///
   int16_t x() const
   {
      return _x;
   }

   ///
   /// <summary>
   /// Gets the display Y of the sprite's top edge.
   /// </summary>
   /// <returns>Display Y.</returns>
   ///
   int16_t y() const
   {
      return _y;
   }

   ///
   /// <summary>
   /// Pushes the whole sprite to the display.
   /// </summary>
   ///
   void push()
   {
      _sprite->pushSprite(_x, _y);
   }

   ///
   /// <summary>
   /// Pushes only part of the sprite to the display. The rectangle is in display
   /// coordinates and is clipped to the sprite's area.
   /// </summary>
   /// <param name="left">Left edge (inclusive).</param>
   /// <param name="top">Top edge (inclusive).</param>
   /// <param name="right">Right edge (inclusive).</param>
   /// <param name="bottom">Bottom edge (inclusive).</param>
   ///
   void push(int16_t left, int16_t top, int16_t right, int16_t bottom)
   {
      left = max<int16_t>(_x, left);
      top = max<int16_t>(_y, top);
      int16_t rightExclusive = min<int16_t>(_x + _sprite->width(), right + 1);
      int16_t bottomExclusive = min<int16_t>(_y + _sprite->height(), bottom + 1);
      if (rightExclusive > left && bottomExclusive > top)
      {
         _display->setClipRect(left, top, rightExclusive - left, bottomExclusive - top);
         _sprite->pushSprite(_x, _y);
         _display->clearClipRect();
      }
   }

   // ----------- Antialiased drawing

   ///
   /// <summary>
   /// Draws an antialiased ring in grayscale. Coverage is computed analytically for both
   /// the outer and inner edge so both are smooth.
   /// </summary>
   /// <param name="cx">Center X relative to the sprite.</param>
   /// <param name="cy">Center Y relative to the sprite.</param>
   /// <param name="outerRadius">Outer radius in pixels.</param>
   /// <param name="thickness">Ring thickness in pixels.</param>
   ///
   void drawAntialiasedRing(int16_t cx, int16_t cy, int16_t outerRadius, int16_t thickness)
   {
      const float outer = outerRadius;
      const float inner = outerRadius - thickness;
      for (int16_t y = cy - outerRadius - 1; y <= cy + outerRadius + 1; y++)
      {
         for (int16_t x = cx - outerRadius - 1; x <= cx + outerRadius + 1; x++)
         {
            float d = sqrtf((float)((x - cx) * (x - cx) + (y - cy) * (y - cy)));
            float coverage = fminf(1.0f, fmaxf(0.0f, outer + 0.5f - d)) - fminf(1.0f, fmaxf(0.0f, inner + 0.5f - d));
            if (coverage > 0.0f)
            {
               uint8_t v = (uint8_t)(coverage * 255.0f);
               _sprite->drawPixel(x, y, _sprite->color888(v, v, v));
            }
         }
      }
   }
};
