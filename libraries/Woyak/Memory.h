#pragma once

#include <Arduino.h>

///
/// <summary>
/// Tracks heap usage (internal SRAM plus PSRAM) relative to a baseline captured via reset(),
/// so sketches can report how much memory has been consumed since that point (e.g. at
/// startup, or after allocating/freeing a set of buffers) without each sketch reimplementing
/// its own getTotalFreeHeap()/delta bookkeeping.
/// </summary>
///
class Memory
{
private:
   uint32_t _baselineFreeBytes = 0;

   ///
   /// <summary>Gets the total free heap, combining internal SRAM and PSRAM.</summary>
   ///
   static uint32_t _totalFreeBytes()
   {
      return ESP.getFreeHeap() + ESP.getFreePsram();
   }

public:
   ///
   /// <summary>
   /// Initializes a new instance of the Memory class, capturing the current free heap as the
   /// baseline for delta().
   /// </summary>
   ///
   Memory()
   {
      reset();
   }

   ///
   /// <summary>
   /// Captures the current total free heap as the new baseline for delta().
   /// </summary>
   ///
   void reset()
   {
      _baselineFreeBytes = _totalFreeBytes();
   }

   ///
   /// <summary>
   /// Gets the number of bytes consumed since the last reset() (negative if more memory has
   /// since been freed than allocated).
   /// </summary>
   /// <returns>Bytes consumed since the baseline, in bytes.</returns>
   ///
   int32_t delta() const
   {
      return static_cast<int32_t>(_baselineFreeBytes) - static_cast<int32_t>(_totalFreeBytes());
   }
};
