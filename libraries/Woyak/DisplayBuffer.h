#pragma once

#include <new>
#include <string.h>
#include <Arduino.h>

#include "ArduinoWithDisplay.h"
#include "Color.h"
#include "Util.h"

///
/// <summary>
/// A plot/chart-relative pixel buffer that packs a small "layer index" per pixel (1, 2, or 4
/// bits, several pixels per byte), paired with a small per-frame palette mapping each layer
/// index to the Color it should be drawn with. Callers stamp pixels/lines with a layer index
/// (via setPixel()/drawLine()) rather than drawing directly to the display, then call
/// diffAndDraw() once per frame to push only the pixels whose layer actually changed since
/// the previous frame to the display - unchanging pixels (typically the vast majority during
/// steady-state scrolling/redraws) are never redrawn or flashed.
///
/// The number of bits used per pixel (1, 2, or 4) is auto-detected lazily on the first
/// diffAndDraw() call after a bind()/reset(), based on the highest layer index actually
/// assigned a color via setPaletteColor() so far - e.g. a buffer that only ever uses layer 1
/// needs just 1 bit/pixel, up to 3 needs 2 bits/pixel, and up to MAX_LAYERS needs 4
/// bits/pixel. Once detected, the bit depth is locked for the buffer's lifetime and only
/// re-detected after the next bind() that changes its dimensions (which reallocates anyway).
///
/// This is intentionally display-content-agnostic (no knowledge of series, bins, or time),
/// so it can be reused by any component that wants cheap frame-to-frame diffing of a small
/// multi-color raster region (e.g. TimedScatterPlot, or any other custom chart/gauge).
/// Index 0 is reserved to mean "unlit" and always draws as Color::BLACK; up to
/// MAX_LAYERS (15) other layers may be assigned distinct colors each frame via setPaletteColor().
/// </summary>
///
class DisplayBuffer
{
private:
   ArduinoWithDisplay* _display = nullptr;
   int16_t _left = 0;
   int16_t _top = 0;
   int16_t _width = 0;
   int16_t _height = 0;

   // Current/previous frame buffers, _bitsPerPixel bits per pixel, several pixels packed per
   // byte, laid out column-major (bytesPerColumn bytes per pixel column) so a whole column's
   // worth of vertical line-drawing touches contiguous memory.
   uint8_t* _mask = nullptr;
   uint8_t* _prevMask = nullptr;
   size_t _bytesPerColumn = 0;

   // Number of bits used to pack each pixel's layer index; starts at the worst case (4) so
   // the very first frame after a bind()/reset() can safely stamp any layer, then is
   // narrowed (and the buffers repacked) on the first diffAndDraw() call once the actual
   // highest layer used this binding is known. Locked afterward until the next bind().
   uint8_t _bitsPerPixel = 4;
   bool _depthLocked = false;
   uint8_t _maxLayerUsed = 0;

public:
   ///
   /// <summary>Maximum number of distinct non-zero layer indices (1..MAX_LAYERS) a single frame can use.</summary>
   ///
   static constexpr uint8_t MAX_LAYERS = 15;

private:
   // Maps a frame's layer indices (1..MAX_LAYERS) to the color each should be drawn with;
   // callers rebuild this at the start of every frame via setPaletteColor() as each layer is
   // assigned. Index 0 is always Color::BLACK (unlit).
   Color _paletteColors[MAX_LAYERS + 1] = { Color::BLACK };

   ///
   /// <summary>Computes how many bytes are needed to pack a column of the given height at the given bit depth.</summary>
   ///
   static size_t _computeBytesPerColumn(int16_t height, uint8_t bitsPerPixel)
   {
      return static_cast<size_t>((height * bitsPerPixel + 7) / 8);
   }

   ///
   /// <summary>Writes a pixel's layer index into a column-major buffer packed at the given bit depth.</summary>
   ///
   static void _writePixel(uint8_t* buffer, size_t bytesPerColumn, uint8_t bitsPerPixel, int16_t x, int16_t y, uint8_t layer)
   {
      const uint8_t pixelsPerByte = static_cast<uint8_t>(8 / bitsPerPixel);
      const uint8_t bitMask = static_cast<uint8_t>((1 << bitsPerPixel) - 1);
      const uint8_t shift = static_cast<uint8_t>((y % pixelsPerByte) * bitsPerPixel);

      uint8_t* column = buffer + (static_cast<size_t>(x) * bytesPerColumn);
      uint8_t& byteRef = column[y / pixelsPerByte];
      byteRef = static_cast<uint8_t>((byteRef & ~static_cast<uint8_t>(bitMask << shift)) | static_cast<uint8_t>((layer & bitMask) << shift));
   }

   ///
   /// <summary>Reads a pixel's layer index from a column-major buffer packed at the given bit depth.</summary>
   ///
   static uint8_t _readPixel(const uint8_t* buffer, size_t bytesPerColumn, uint8_t bitsPerPixel, int16_t x, int16_t y)
   {
      const uint8_t pixelsPerByte = static_cast<uint8_t>(8 / bitsPerPixel);
      const uint8_t bitMask = static_cast<uint8_t>((1 << bitsPerPixel) - 1);
      const uint8_t shift = static_cast<uint8_t>((y % pixelsPerByte) * bitsPerPixel);

      const uint8_t* column = buffer + (static_cast<size_t>(x) * bytesPerColumn);
      return static_cast<uint8_t>((column[y / pixelsPerByte] >> shift) & bitMask);
   }

   ///
   /// <summary>
   /// Determines the minimum bit depth (1, 2, or 4) needed to represent layer indices up to
   /// and including the given maximum layer value.
   /// </summary>
   ///
   static uint8_t _minBitsPerPixel(uint8_t maxLayer)
   {
      if (maxLayer <= 1)
      {
         return 1;
      }
      if (maxLayer <= 3)
      {
         return 2;
      }
      return 4;
   }

   ///
   /// <summary>
   /// Called once per frame at the start of diffAndDraw(). On the very first call after a
   /// bind()/reset(), narrows the buffers from the initial worst-case 4 bits/pixel down to
   /// the minimum bit depth actually needed for the highest layer used so far (tracked via
   /// setPaletteColor()), repacking both the current and previous frame buffers in place.
   /// Locks the depth afterward so later frames (which may transiently use fewer layers)
   /// don't repack again mid-lifetime.
   /// </summary>
   ///
   void _lockBitDepthIfNeeded()
   {
      if (_depthLocked || _mask == nullptr)
      {
         return;
      }

      _depthLocked = true;

      const uint8_t newBitsPerPixel = _minBitsPerPixel(_maxLayerUsed);
      if (newBitsPerPixel == _bitsPerPixel)
      {
         return;
      }

      const size_t newBytesPerColumn = _computeBytesPerColumn(_height, newBitsPerPixel);
      const size_t newBufSize = static_cast<size_t>(_width) * newBytesPerColumn;

      uint8_t* newMask = new (std::nothrow) uint8_t[newBufSize];
      uint8_t* newPrevMask = new (std::nothrow) uint8_t[newBufSize];
      if (newMask == nullptr || newPrevMask == nullptr)
      {
         delete[] newMask;
         delete[] newPrevMask;
         return;
      }

      memset(newMask, 0, newBufSize);
      memset(newPrevMask, 0, newBufSize);

      for (int16_t x = 0; x < _width; x++)
      {
         for (int16_t y = 0; y < _height; y++)
         {
            uint8_t layer = _readPixel(_mask, _bytesPerColumn, _bitsPerPixel, x, y);
            _writePixel(newMask, newBytesPerColumn, newBitsPerPixel, x, y, layer);

            uint8_t prevLayer = _readPixel(_prevMask, _bytesPerColumn, _bitsPerPixel, x, y);
            _writePixel(newPrevMask, newBytesPerColumn, newBitsPerPixel, x, y, prevLayer);
         }
      }

      delete[] _mask;
      delete[] _prevMask;
      _mask = newMask;
      _prevMask = newPrevMask;
      _bytesPerColumn = newBytesPerColumn;
      _bitsPerPixel = newBitsPerPixel;
   }

public:
   ///
   /// <summary>
   /// Constructs an unbound display buffer; call bind() before use.
   /// </summary>
   ///
   DisplayBuffer() = default;

   ///
   /// <summary>
   /// Constructs a display buffer bound to the given display and chart-relative rectangle.
   /// </summary>
   /// <param name="display">Pointer to the display object pixels are ultimately drawn to.</param>
   /// <param name="left">Absolute display column of the buffer's left edge.</param>
   /// <param name="top">Absolute display row of the buffer's top edge.</param>
   /// <param name="width">Buffer width in pixels.</param>
   /// <param name="height">Buffer height in pixels.</param>
   ///
   DisplayBuffer(ArduinoWithDisplay* display, int16_t left, int16_t top, int16_t width, int16_t height)
   {
      bind(display, left, top, width, height);
   }

   DisplayBuffer(const DisplayBuffer&) = delete;
   DisplayBuffer& operator=(const DisplayBuffer&) = delete;

   ~DisplayBuffer()
   {
      delete[] _mask;
      delete[] _prevMask;
   }

   ///
   /// <summary>
   /// (Re)binds this buffer to the given display and chart-relative rectangle, allocating
   /// or reallocating its nibble buffers only when the pixel dimensions actually changed.
   /// The buffer's origin (left/top) may be updated even when the dimensions are unchanged.
   /// Newly (re)allocated buffers start fully cleared (layer 0 everywhere).
   /// </summary>
   /// <param name="display">Pointer to the display object pixels are ultimately drawn to.</param>
   /// <param name="left">Absolute display column of the buffer's left edge.</param>
   /// <param name="top">Absolute display row of the buffer's top edge.</param>
   /// <param name="width">Buffer width in pixels.</param>
   /// <param name="height">Buffer height in pixels.</param>
   /// <returns>True if the buffers are sized correctly and ready to use.</returns>
   ///
   bool bind(ArduinoWithDisplay* display, int16_t left, int16_t top, int16_t width, int16_t height)
   {
      _display = display;
      _left = left;
      _top = top;

      if ((width == _width) && (height == _height) && (_mask != nullptr))
      {
         return true;
      }

      delete[] _mask;
      delete[] _prevMask;
      _mask = _prevMask = nullptr;

      _width = width;
      _height = height;
      _bitsPerPixel = 4;
      _depthLocked = false;
      _maxLayerUsed = 0;
      _bytesPerColumn = _computeBytesPerColumn(height, _bitsPerPixel);

      const size_t bufSize = static_cast<size_t>(width) * _bytesPerColumn;
      if (bufSize == 0)
      {
         return true;
      }

      _mask = new (std::nothrow) uint8_t[bufSize];
      _prevMask = new (std::nothrow) uint8_t[bufSize];

      if (_mask == nullptr || _prevMask == nullptr)
      {
         Util::setHaltReason("OOM allocating DisplayBuffer mask");
         Util::reset();
         return false;
      }

      memset(_mask, 0, bufSize);
      memset(_prevMask, 0, bufSize);
      return true;
   }

   ///
   /// <summary>Gets this buffer's width in pixels.</summary>
   ///
   int16_t width() const { return _width; }

   ///
   /// <summary>Gets this buffer's height in pixels.</summary>
   ///
   int16_t height() const { return _height; }

   ///
   /// <summary>
   /// Assigns the color a given layer index should be drawn with for the current frame.
   /// Must be called before diffAndDraw() for every layer index stamped this frame (layer
   /// 0/unlit always draws as Color::BLACK and cannot be reassigned).
   /// </summary>
   /// <param name="layer">Layer index (1..MAX_LAYERS) to assign a color to.</param>
   /// <param name="color">Color this layer should draw as this frame.</param>
   ///
   void setPaletteColor(uint8_t layer, Color color)
   {
      if (layer >= 1 && layer <= MAX_LAYERS)
      {
         _paletteColors[layer] = color;
         if (layer > _maxLayerUsed)
         {
            _maxLayerUsed = layer;
         }
      }
   }

   ///
   /// <summary>
   /// Clears the current frame's buffer to layer 0 (unlit) everywhere, without affecting
   /// the previous frame's buffer (used by diffAndDraw() to know what changed).
   /// </summary>
   ///
   void clear()
   {
      const size_t bufSize = _bytesPerColumn * static_cast<size_t>(_width);
      if (bufSize > 0)
      {
         memset(_mask, 0, bufSize);
      }
   }

   ///
   /// <summary>
   /// Resets both the current and previous frame buffers to layer 0 (unlit) everywhere.
   /// Call this after physically clearing the corresponding display region to black, so the
   /// next diffAndDraw() doesn't think already-black pixels are still lit from before and
   /// skip redrawing them.
   /// </summary>
   ///
   void reset()
   {
      const size_t bufSize = _bytesPerColumn * static_cast<size_t>(_width);
      if (bufSize > 0)
      {
         memset(_mask, 0, bufSize);
         memset(_prevMask, 0, bufSize);
      }
   }

   ///
   /// <summary>
   /// Stamps a single pixel's layer index into the current frame's buffer, addressing
   /// column x, row y (both buffer-relative). No-ops if the coordinates fall outside the
   /// buffer bounds. A nonzero layer always overwrites whatever layer previously occupied
   /// that pixel this frame, so later-drawn layers win any overlap, matching normal draw order.
   /// </summary>
   /// <param name="x">Buffer-relative pixel column.</param>
   /// <param name="y">Buffer-relative pixel row.</param>
   /// <param name="layer">Layer index (1..MAX_LAYERS) to stamp at this pixel.</param>
   ///
   void setPixel(int16_t x, int16_t y, uint8_t layer)
   {
      if (x < 0 || x >= _width || y < 0 || y >= _height)
      {
         return;
      }

      _writePixel(_mask, _bytesPerColumn, _bitsPerPixel, x, y, layer);
   }

   ///
   /// <summary>
   /// Gets the layer index stamped at a pixel in the current frame's buffer. Out-of-bounds
   /// coordinates are treated as layer 0 (unlit).
   /// </summary>
   ///
   uint8_t getPixel(int16_t x, int16_t y) const
   {
      if (x < 0 || x >= _width || y < 0 || y >= _height)
      {
         return 0;
      }

      return _readPixel(_mask, _bytesPerColumn, _bitsPerPixel, x, y);
   }

   ///
   /// <summary>
   /// Stamps every pixel along a line from (x0,y0) to (x1,y1) with the given layer index,
   /// using integer Bresenham stepping so the exact same pair of endpoints always lights
   /// the exact same set of pixels, frame after frame, matching what drawLine() would
   /// physically draw.
   /// </summary>
   /// <param name="x0">Buffer-relative start column.</param>
   /// <param name="y0">Buffer-relative start row.</param>
   /// <param name="x1">Buffer-relative end column.</param>
   /// <param name="y1">Buffer-relative end row.</param>
   /// <param name="layer">Layer index (1..MAX_LAYERS) to stamp along this line.</param>
   ///
   void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t layer)
   {
      int x = x0;
      int y = y0;
      int dx = abs(x1 - x0);
      int sx = (x0 < x1) ? 1 : -1;
      int dy = -abs(y1 - y0);
      int sy = (y0 < y1) ? 1 : -1;
      int err = dx + dy;

      for (;;)
      {
         setPixel(x, y, layer);
         if (x == x1 && y == y1)
         {
            break;
         }
         int e2 = 2 * err;
         if (e2 >= dy)
         {
            err += dy;
            x += sx;
         }
         if (e2 <= dx)
         {
            err += dx;
            y += sy;
         }
      }
   }

   ///
   /// <summary>
   /// Diffs this frame's buffer against the previous frame's and draws only the pixels
   /// whose layer index actually changed, looking up each pixel's color via the palette set
   /// through setPaletteColor() (layer 0 always draws as Color::BLACK). Pixels whose layer
   /// did not change are left untouched, so unchanging content is never redrawn or flashed.
   /// The current frame's buffer is copied into the previous-frame buffer afterward so the
   /// next call diffs against what was actually just drawn.
   /// </summary>
   ///
   void diffAndDraw()
   {
      if (_display == nullptr)
      {
         return;
      }

      _lockBitDepthIfNeeded();

      const uint8_t pixelsPerByte = static_cast<uint8_t>(8 / _bitsPerPixel);
      const uint8_t bitMask = static_cast<uint8_t>((1 << _bitsPerPixel) - 1);

      for (int16_t x = 0; x < _width; x++)
      {
         uint8_t* column = _mask + (static_cast<size_t>(x) * _bytesPerColumn);
         uint8_t* prevColumn = _prevMask + (static_cast<size_t>(x) * _bytesPerColumn);

         for (size_t byteIdx = 0; byteIdx < _bytesPerColumn; byteIdx++)
         {
            uint8_t changed = column[byteIdx] ^ prevColumn[byteIdx];
            if (changed == 0)
            {
               continue;
            }

            for (uint8_t slot = 0; slot < pixelsPerByte; slot++)
            {
               const uint8_t shift = static_cast<uint8_t>(slot * _bitsPerPixel);
               if ((changed >> shift) & bitMask)
               {
                  int16_t y = static_cast<int16_t>((byteIdx * pixelsPerByte) + slot);
                  if (y >= _height)
                  {
                     break;
                  }
                  uint8_t layer = static_cast<uint8_t>((column[byteIdx] >> shift) & bitMask);
                  _display->drawPixel(_left + x, _top + y, _paletteColors[layer]);
               }
            }
         }

         memcpy(prevColumn, column, _bytesPerColumn);
      }
   }
};
