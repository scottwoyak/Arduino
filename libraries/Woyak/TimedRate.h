#pragma once

#include <Arduino.h>

#include "Util.h"

///
/// <summary>
/// Tracks the rate of tick() calls over a rolling time window (rather than a fixed
/// number of samples like <see cref="RollingRate"/>). Timestamps older than the
/// configured duration are evicted automatically, so <see cref="get"/> always reflects
/// the rate measured over (up to) the last <c>durationMs</c> milliseconds.
/// </summary>
///
template<unsigned long (*TimeFunc)() = millis>
class TimedRateBase
{
private:
   unsigned long* _ticks = nullptr;
   size_t _capacity = 0;
   size_t _head = 0;
   size_t _count = 0;
   unsigned long _durationMs;

   ///
   /// <summary>
   /// Evicts timestamps that have aged out of the rolling window.
   /// </summary>
   ///
   void _evictOld()
   {
      unsigned long now = TimeFunc();
      while (_count > 0 && Util::getSpan(_ticks[_head], now) > _durationMs)
      {
         _head = (_head + 1) % _capacity;
         _count--;
      }
   }

   ///
   /// <summary>
   /// Grows the backing buffer to at least the requested capacity, preserving order.
   /// </summary>
   ///
   void _ensureCapacity(size_t capacity)
   {
      if (capacity <= _capacity)
      {
         return;
      }

      unsigned long* newTicks = new unsigned long[capacity];
      for (size_t i = 0; i < _count; i++)
      {
         newTicks[i] = _ticks[(_head + i) % _capacity];
      }

      delete[] _ticks;
      _ticks = newTicks;
      _capacity = capacity;
      _head = 0;
   }

public:
   ///
   /// <summary>
   /// Initializes a new TimedRate tracker.
   /// </summary>
   /// <param name="durationMs">Length of the rolling time window, in milliseconds. Defaults to 1000ms.</param>
   ///
   explicit TimedRateBase(unsigned long durationMs = 1000UL)
      : _durationMs(durationMs)
   {
   }

   ~TimedRateBase()
   {
      delete[] _ticks;
   }

   TimedRateBase(const TimedRateBase&) = delete;
   TimedRateBase& operator=(const TimedRateBase&) = delete;

   ///
   /// <summary>
   /// Records a new tick at the current time, evicting any timestamps that have aged
   /// out of the rolling window first.
   /// </summary>
   ///
   void tick()
   {
      _evictOld();

      if (_count >= _capacity)
      {
         _ensureCapacity(_capacity == 0 ? 16 : _capacity * 2);
      }

      size_t tailIndex = (_head + _count) % _capacity;
      _ticks[tailIndex] = TimeFunc();
      _count++;
   }

   ///
   /// <summary>
   /// Clears all recorded timestamps.
   /// </summary>
   ///
   void reset()
   {
      _head = 0;
      _count = 0;
   }

   ///
   /// <summary>
   /// Gets the number of timestamps currently retained in the rolling window.
   /// </summary>
   ///
   uint16_t getCount()
   {
      _evictOld();
      return static_cast<uint16_t>(_count);
   }

   ///
   /// <summary>
   /// Gets the elapsed time between the oldest and newest retained timestamps.
   /// </summary>
   /// <returns>Elapsed milliseconds, or 0 if fewer than two timestamps are retained.</returns>
   ///
   unsigned long getElapsedMs()
   {
      _evictOld();

      if (_count < 2)
      {
         return 0;
      }

      unsigned long oldest = _ticks[_head];
      unsigned long newest = _ticks[(_head + _count - 1) % _capacity];
      return Util::getSpan(oldest, newest);
   }

   ///
   /// <summary>
   /// Gets the current rate, in ticks per second, measured over the retained timestamps.
   /// </summary>
   /// <returns>Ticks per second, or 0 if not enough data has been collected yet.</returns>
   ///
   float get()
   {
      unsigned long elapsedMs = getElapsedMs();
      if (elapsedMs == 0)
      {
         return 0.0f;
      }

      // count-1 intervals span elapsedMs
      return (_count - 1.0f) * 1000.0f / elapsedMs;
   }
};

using TimedRate = TimedRateBase<>;
