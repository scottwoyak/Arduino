#pragma once

#include <new>
#include <string.h>
#include <Arduino.h>

#include "ArduinoWithDisplay.h"
#include "ColorX.h"
#include "Util.h"

///
/// <summary>
/// A plot/chart-relative pixel buffer that packs a small "layer index" per pixel (1, 2, or 4
/// bits, several pixels per byte), paired with a small per-frame palette mapping each layer
/// index to the Color it should be drawn with. Callers stamp pixels/lines with a layer index
/// (via setPixel()/drawLine()) rather than drawing directly to the display, then call
/// draw() once per frame to push only the pixels whose layer actually changed since the
/// previous frame to the display - unchanging pixels (typically the vast majority during
/// steady-state scrolling/redraws) are never redrawn or flashed.
///
/// The number of bits used per pixel (1, 2, or 4) is auto-detected lazily on the first
/// draw() call after a bind()/reset(), based on the highest layer index actually
/// assigned a color via setPaletteColor() so far - e.g. a buffer that only ever uses layer 1
/// needs just 1 bit/pixel, up to 3 needs 2 bits/pixel, and up to MAX_LAYERS needs 4
/// bits/pixel. Once detected, the bit depth is locked for the buffer's lifetime and only
/// re-detected after the next bind() that changes its dimensions (which reallocates anyway).
///
/// This is intentionally display-content-agnostic (no knowledge of series, bins, or time),
/// so it can be reused by any component that wants cheap frame-to-frame diffing of a small
/// multi-color raster region (e.g. TimedScatterPlot, or any other custom chart/gauge).
/// Index 0 is reserved to mean "unlit"/background and defaults to Color::BLACK, but may be
/// reassigned via setBackgroundColor(); up to MAX_LAYERS (15) other layers may be assigned
/// distinct colors each frame via setPaletteColor().
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

   // Scratch row buffer, one resolved RGB565 color per column, reused every frame to bulk-
   // push a full repainted row via pushImageRow() (one SPI/DMA transfer per row) instead of
   // calling drawPixel() once per pixel. Only allocated (width * 2 bytes) on demand, so
   // callers that never do a full repaint never pay for it.
   uint16_t* _rowColors = nullptr;
   int16_t _rowColorsWidth = 0;

   // Scratch column buffer, one resolved RGB565 color per row, reused every frame to bulk-
   // push a contiguous vertical run of changed pixels within a column via pushImageColumn()
   // (one SPI/DMA transfer per run) instead of calling drawPixel() once per changed pixel.
   // Sized to the buffer's height (the longest possible run), and only allocated on demand.
   uint16_t* _colColors = nullptr;
   int16_t _colColorsHeight = 0;

   // Number of bits used to pack each pixel's layer index; starts at the worst case (4) so
   // the very first frame after a bind()/reset() can safely stamp any layer, then is
   // narrowed (and the buffers repacked) on the first draw() call once the actual
   // highest layer used this binding is known. Locked afterward until the next bind().
   uint8_t _bitsPerPixel = 4;
   bool _depthLocked = false;
   uint8_t _maxLayerUsed = 0;

   // True until the next draw() call has run. While true, every pixel is treated as
   // changed regardless of what _prevMask currently holds, since _prevMask's content right
   // after a bind()/reset() is only a placeholder (see reset()) and could otherwise alias a
   // real layer value once the buffer's bit depth is narrowed by _lockBitDepthIfNeeded() -
   // e.g. at 1 bit/pixel only values 0/1 are representable, so a placeholder byte could
   // decode to the same value as a real, never-before-drawn pixel and be wrongly skipped
   // instead of painted.
   bool _forceFullRepaint = true;

public:
   ///
   /// <summary>Maximum number of distinct non-zero layer indices (1..MAX_LAYERS) a single frame can use.</summary>
   ///
   static constexpr uint8_t MAX_LAYERS = 15;

   ///
   /// <summary>
   /// Selects how draw() repaints the display. Both modes use the bulk row/column transfer
   /// paths (pushImageRow()/pushImageColumn()) for performance.
   /// </summary>
   ///
   enum class RedrawMode
   {
      FULL, // Unconditionally repaint every pixel, using bulk row transfers.
      DIFF, // Only repaint pixels that changed since the previous frame, using bulk column transfers for contiguous runs (the default).
   };

private:
   // Maps a frame's layer indices (1..MAX_LAYERS) to the color each should be drawn with;
   // callers rebuild this at the start of every frame via setPaletteColor() as each layer is
   // assigned. Index 0 (unlit/background) defaults to Color::BLACK but may be reassigned via
   // setBackgroundColor().
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
   /// Called once per frame at the start of draw(). On the first call after a
   /// bind()/reset(), narrows the buffers from the initial worst-case 4 bits/pixel down to
   /// the minimum bit depth actually needed for the highest layer used so far (tracked via
   /// setPaletteColor()), repacking both the current and previous frame buffers in place.
   /// The depth is never narrowed again after that (so later frames that transiently use
   /// fewer layers don't repack needlessly), but it can still grow later if a caller starts
   /// using a higher layer index than the currently packed depth can represent (e.g. enabling
   /// a moving-average/stddev overlay after the buffer already locked in at 1 bit/pixel for
   /// raw points alone) - otherwise the extra layer's bits would be silently masked away and
   /// it would draw using layer 0's (background) color instead of its own.
   /// </summary>
   ///
   void _lockBitDepthIfNeeded()
   {
      if (_mask == nullptr)
      {
         return;
      }

      const uint8_t newBitsPerPixel = _minBitsPerPixel(_maxLayerUsed);
      if (_depthLocked && (newBitsPerPixel <= _bitsPerPixel))
      {
         return;
      }

      _depthLocked = true;

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

   ///
   /// <summary>
   /// Lazily (re)allocates the scratch row buffer used by draw()'s full-repaint bulk-push
   /// path, sized to the buffer's current width. Only reallocates when the width actually
   /// changed (e.g. after a bind() resize), so steady-state frames pay no allocation cost.
   /// </summary>
   /// <returns>True if the row buffer is allocated and ready to use.</returns>
   ///
   bool _ensureRowColorsBuffer()
   {
      if (_rowColors != nullptr && _rowColorsWidth == _width)
      {
         return true;
      }

      delete[] _rowColors;
      _rowColors = new (std::nothrow) uint16_t[_width];
      _rowColorsWidth = _width;
      return _rowColors != nullptr;
   }

   ///
   /// <summary>
   /// Lazily (re)allocates the scratch column buffer used by draw()'s diffed-redraw
   /// batching path, sized to the buffer's current height (the longest possible contiguous
   /// changed-pixel run within a column). Only reallocates when the height actually changed.
   /// </summary>
   /// <returns>True if the column buffer is allocated and ready to use.</returns>
   ///
   bool _ensureColColorsBuffer()
   {
      if (_colColors != nullptr && _colColorsHeight == _height)
      {
         return true;
      }

      delete[] _colColors;
      _colColors = new (std::nothrow) uint16_t[_height];
      _colColorsHeight = _height;
      return _colColors != nullptr;
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
   /// <param name="expectedMaxLayer">
   /// Highest non-zero layer index this buffer is expected to use (0-MAX_LAYERS), so the
   /// initial allocation can be sized for that bit depth instead of the worst case (see
   /// bind() for details). Defaults to MAX_LAYERS to preserve prior behavior.
   /// </param>
   ///
   DisplayBuffer(ArduinoWithDisplay* display, int16_t left, int16_t top, int16_t width, int16_t height, uint8_t expectedMaxLayer = MAX_LAYERS)
   {
      bind(display, left, top, width, height, expectedMaxLayer);
   }

   DisplayBuffer(const DisplayBuffer&) = delete;
   DisplayBuffer& operator=(const DisplayBuffer&) = delete;

   ~DisplayBuffer()
   {
      delete[] _mask;
      delete[] _prevMask;
      delete[] _rowColors;
      delete[] _colColors;
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
   /// <param name="expectedMaxLayer">
   /// Highest non-zero layer index this buffer is expected to use (0-MAX_LAYERS), so the
   /// initial allocation can be sized for that bit depth (1, 2, or 4 bits/pixel) instead of
   /// always assuming the worst case (4 bits/pixel, up to MAX_LAYERS). If a later
   /// setPaletteColor() call actually uses a higher layer than expected, the buffer still
   /// grows automatically via _lockBitDepthIfNeeded() on the next draw(). Defaults to
   /// MAX_LAYERS to preserve prior behavior.
   /// </param>
   /// <returns>True if the buffers are sized correctly and ready to use.</returns>
   ///
   bool bind(ArduinoWithDisplay* display, int16_t left, int16_t top, int16_t width, int16_t height, uint8_t expectedMaxLayer = MAX_LAYERS)
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
      _bitsPerPixel = _minBitsPerPixel(expectedMaxLayer);
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
         Util::reset(0.0f, "OOM allocating DisplayBuffer mask");
         return false;
      }

      memset(_mask, 0, bufSize);
      memset(_prevMask, 0, bufSize);
      _forceFullRepaint = true;
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
   /// Gets the size, in bytes, of one fully packed frame at this buffer's current bit depth.
   /// </summary>
   ///
   size_t frameSizeBytes() const
   {
      return _bytesPerColumn * static_cast<size_t>(_width);
   }

   ///
   /// <summary>
   /// Packs a whole row-major layer-index image (one byte per pixel, width * height bytes)
   /// directly into this buffer's back buffer - the buffer not currently reflecting the
   /// physically displayed frame - using this buffer's current bit-packed column-major
   /// layout. This lets a caller that alternates between a small number of precomputed
   /// images (e.g. two target frames) pay the per-pixel packing cost once per image
   /// generation, rather than re-stamping every pixel via setPixel() on every redraw.
   /// After staging an image this way, call swapBuffers() to make it the current frame, then
   /// draw() to paint only the pixels that actually changed versus the frame physically on
   /// the display - swapBuffers() is an O(1) pointer swap, so no full-frame copy is needed
   /// either, unlike draw()'s default current/previous bookkeeping.
   /// Call setPaletteColor() for every layer index used by this (and any other) frame *before*
   /// calling drawImage(), so the bit depth is locked in at the width needed for those layers
   /// (see setPaletteColor()'s remarks) - packing before the bit depth is finalized would bake
   /// in a layout that a later bit-depth change would invalidate.
   /// </summary>
   /// <param name="layerImage">Row-major source image (layerImage[y * width + x]), width * height bytes.</param>
   ///
   void drawImage(const uint8_t* layerImage)
   {
      for (int16_t x = 0; x < _width; x++)
      {
         for (int16_t y = 0; y < _height; y++)
         {
            uint8_t layer = layerImage[(static_cast<size_t>(y) * _width) + x];
            _writePixel(_prevMask, _bytesPerColumn, _bitsPerPixel, x, y, layer);
         }
      }
   }

   ///
   /// <summary>
   /// Swaps the current and previous frame buffers - an O(1) pointer swap, not a copy. Meant
   /// to be paired with drawImage() (which stages a new image into the back/previous buffer)
   /// and draw() (which diffs the new current buffer against the new previous buffer, i.e.
   /// the frame that was actually last painted to the display, and repaints only what
   /// changed).
   /// </summary>
   ///
   void swapBuffers()
   {
      uint8_t* temp = _mask;
      _mask = _prevMask;
      _prevMask = temp;
   }

   ///
   /// <summary>
   /// Assigns the color a given layer index should be drawn with for the current frame.
   /// Must be called before draw() for every layer index stamped this frame (layer
   /// 0/unlit's color is set separately via setBackgroundColor()).
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

            // Grow the packed bit depth immediately, rather than waiting for the next
            // draw() call, so any setPixel() calls made this frame (before
            // draw() runs) use a bit depth wide enough to represent this layer
            // instead of silently truncating it against a still-too-narrow depth.
            _lockBitDepthIfNeeded();
         }
      }
   }

   ///
   /// <summary>
   /// Sets the color layer 0 (unlit/background) draws as. Defaults to Color::BLACK.
   /// </summary>
   /// <param name="color">Color the background/unlit layer should draw as.</param>
   ///
   void setBackgroundColor(Color color)
   {
      _paletteColors[0] = color;
   }

   ///
   /// <summary>
   /// Clears the current frame's buffer to layer 0 (unlit) everywhere. By default the
   /// previous frame's buffer is left untouched so the next draw() call can still correctly
   /// diff against what is actually still physically on the display. Callers are expected to
   /// use the default (alsoResetPrevious = false) before (re)stamping a frame's worth of
   /// content when the previous frame's buffer still accurately reflects what's physically on
   /// the display (i.e. the normal per-frame redraw case) - a pixel that was lit last frame
   /// but isn't re-stamped this frame is then correctly detected as changed and erased by the
   /// next draw() call. If the display area was just physically erased by some other means
   /// (e.g. fillRect()), pass alsoResetPrevious = true instead so both buffers agree with
   /// what's now actually on screen and the next draw() correctly treats unlit pixels as
   /// unchanged rather than repainting them. Resizing via bind() already does this internally.
   /// </summary>
   /// <param name="alsoResetPrevious">
   /// When true, also clears the previous frame's buffer (use after the display area was
   /// physically erased by some other means). When false (default), only the current frame's
   /// buffer is cleared, preserving the previous frame's buffer for diffing.
   /// </param>
   ///
   void clear(bool alsoResetPrevious = false)
   {
      const size_t bufSize = _bytesPerColumn * static_cast<size_t>(_width);
      if (bufSize > 0)
      {
         memset(_mask, 0, bufSize);
         if (alsoResetPrevious)
         {
            memset(_prevMask, 0, bufSize);
         }
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
   /// Draws a point marker centered on (x, y) with the given layer index: a single pixel
   /// when size is 1 or less, otherwise a small filled circle whose radius is size - 1, so it
   /// spans exactly 2 * size - 1 pixels in diameter (e.g. size 2 draws a 3-pixel-wide circle,
   /// size 3 draws a 5-pixel-wide circle). Each row of the circle is filled with a single
   /// drawLine() call (or setPixel() for single-pixel rows) instead of testing every pixel
   /// individually, since a horizontal span is drawn just as fast via drawLine() as via a
   /// per-pixel loop but with far fewer distance checks.
   /// </summary>
   /// <param name="x">Buffer-relative center column.</param>
   /// <param name="y">Buffer-relative center row.</param>
   /// <param name="size">Marker size, in pixels; sizes above 1 draw a circle of radius size - 1.</param>
   /// <param name="layer">Layer index (1..MAX_LAYERS) to stamp the marker's pixels with.</param>
   ///
   void drawPoint(int16_t x, int16_t y, uint8_t size, uint8_t layer)
   {
      if (size <= 1)
      {
         setPixel(x, y, layer);
         return;
      }

      int16_t radius = static_cast<int16_t>(size) - 1;
      int16_t radiusSquared = radius * radius;

      for (int16_t dy = -radius; dy <= radius; dy++)
      {
         int16_t halfWidth = static_cast<int16_t>(sqrtf(static_cast<float>(radiusSquared - (dy * dy))));

         if (halfWidth == 0)
         {
            setPixel(x, y + dy, layer);
         }
         else
         {
            drawLine(x - halfWidth, y + dy, x + halfWidth, y + dy, layer);
         }
      }
   }

   ///
   /// <summary>
   /// Draws this frame to the display, either repainting every pixel unconditionally (Sprite-
   /// style redraw) or diffing against the previous frame and only repainting pixels whose
   /// layer index actually changed, looking up each pixel's color via the palette set through
   /// setPaletteColor()/setBackgroundColor(). A full repaint is also automatically forced
   /// (regardless of fullRepaint) on the first draw() after a bind()/clear() that leaves the
   /// previous frame's buffer untrustworthy to diff against - see bind()/clear()'s remarks.
   /// When diffing, pixels whose layer did not change are left untouched, so unchanging
   /// content is never redrawn or flashed; unchanged bytes (up to 8 pixels at a time) are
   /// skipped with a single XOR check rather than inspecting each pixel individually. The
   /// entire scan is wrapped in startWrite()/endWrite() so all the draw calls for this frame
   /// are sent as one batched transaction instead of one per pixel.
   /// After drawing, this frame's buffer and the previous-frame buffer are swapped (see
   /// swapBuffers()'s remarks) - an O(1) pointer swap, not a copy - so the next call diffs
   /// against what was actually just drawn. Callers that keep re-stamping the same content
   /// every frame (e.g. via clear() + setPixel()/drawLine()) simply keep doing so each frame
   /// as before; clear() operates on whichever buffer is current after this swap, so it still
   /// ends up clearing the right one. Callers alternating between two already-complete,
   /// unchanging images (see drawImage()'s remarks) can likewise just call drawImage() to
   /// stage the next image into the new back buffer and then draw() again, with no manual
   /// swapBuffers() call needed either way.
   /// </summary>
   /// <param name="mode">Redraw mode to use for this call (see RedrawMode); defaults to DIFF (only repaint changed pixels using bulk column transfers).</param>
   ///
   void draw(RedrawMode mode = RedrawMode::DIFF)
   {
      if (_display == nullptr)
      {
         return;
      }

      _lockBitDepthIfNeeded();

      const uint8_t pixelsPerByte = static_cast<uint8_t>(8 / _bitsPerPixel);
      const uint8_t bitMask = static_cast<uint8_t>((1 << _bitsPerPixel) - 1);

      // While a full repaint is pending (see _forceFullRepaint's declaration), every byte
      // must be treated as changed so every pixel gets (re)painted, since _prevMask's
      // content isn't a trustworthy baseline to diff against yet.
      const bool repaintAll = (mode == RedrawMode::FULL) || _forceFullRepaint;

      _display->startWrite();

      // A full repaint means every pixel in every row is being redrawn anyway, so resolve
      // and bulk-push one row at a time via pushImageRow() (one SPI/DMA transfer per row)
      // instead of falling through to the per-pixel drawPixel() path below - far fewer,
      // much larger transfers for the common "everything changed" case.
      if (repaintAll && _ensureRowColorsBuffer())
      {
         for (int16_t y = 0; y < _height; y++)
         {
            for (int16_t x = 0; x < _width; x++)
            {
               uint8_t layer = _readPixel(_mask, _bytesPerColumn, _bitsPerPixel, x, y);
               _rowColors[x] = static_cast<uint16_t>(_paletteColors[layer]);
            }
            _display->pushImageRow(_left, _top + y, _rowColors, _width);
         }

         _display->endWrite();

         _forceFullRepaint = false;
         swapBuffers();
         return;
      }

      // Diffed redraw: within each column, gather each contiguous run of changed pixels
      // (byte-level XOR check still skips unchanged bytes in bulk) into _colColors. The run
      // is bulk-pushed in one pushImageColumn() call - a single-pixel "run" still costs just
      // one small transfer, matching the old per-pixel behavior in the worst (sparse-change)
      // case.
      _ensureColColorsBuffer();

      for (int16_t x = 0; x < _width; x++)
      {
         uint8_t* column = _mask + (static_cast<size_t>(x) * _bytesPerColumn);
         uint8_t* prevColumn = _prevMask + (static_cast<size_t>(x) * _bytesPerColumn);

         int16_t runStartY = -1;
         int16_t runLength = 0;

         auto flushRun = [&]()
         {
            if (runLength > 1)
            {
               _display->pushImageColumn(_left + x, _top + runStartY, _colColors, runLength);
            }
            else if (runLength == 1)
            {
               _display->drawPixel(_left + x, _top + runStartY, static_cast<Color>(_colColors[0]));
            }
            runLength = 0;
         };

         for (size_t byteIdx = 0; byteIdx < _bytesPerColumn; byteIdx++)
         {
            uint8_t changed = repaintAll ? static_cast<uint8_t>(0xFF) : static_cast<uint8_t>(column[byteIdx] ^ prevColumn[byteIdx]);
            if (changed == 0)
            {
               flushRun();
               continue;
            }

            for (uint8_t slot = 0; slot < pixelsPerByte; slot++)
            {
               int16_t y = static_cast<int16_t>((byteIdx * pixelsPerByte) + slot);
               if (y >= _height)
               {
                  break;
               }

               const uint8_t shift = static_cast<uint8_t>(slot * _bitsPerPixel);
               if ((changed >> shift) & bitMask)
               {
                  uint8_t layer = static_cast<uint8_t>((column[byteIdx] >> shift) & bitMask);
                  if (runLength == 0)
                  {
                     runStartY = y;
                  }
                  else if (y != runStartY + runLength)
                  {
                     flushRun();
                     runStartY = y;
                  }

                  if (runLength < _height)
                  {
                     _colColors[runLength] = static_cast<uint16_t>(_paletteColors[layer]);
                  }
                  runLength++;
               }
               else
               {
                  flushRun();
               }
            }
         }

         flushRun();
      }

      _display->endWrite();

      _forceFullRepaint = false;
      swapBuffers();
   }
};
