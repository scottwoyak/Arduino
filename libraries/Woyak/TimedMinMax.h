#pragma once

#include <algorithm>
#include <cmath>
#include <new>
#include <Arduino.h>

///
/// <summary>
/// Tracks the running minimum and maximum of a sliding time window so the current
/// extrema can be read in O(1) instead of rescanning every retained sample. Values are
/// kept in a pair of monotonic deques (one for the minimum, one for the maximum);
/// candidates that can never become an extremum again (because a more extreme value
/// arrived after them) are dropped as soon as a new value is added, and the remainder
/// are evicted once they age out of the configured time window.
/// </summary>
///
template<unsigned long (*TimeFunc)() = millis>
class TimedMinMaxBase
{
private:
   struct Deque
   {
      float* values = nullptr;
      unsigned long* ticks = nullptr;
      size_t head = 0;
      size_t count = 0;
   };

   Deque _minDeque;
   Deque _maxDeque;
   size_t _capacity = 0;
   unsigned long _durationMs = 1;

   /// <summary>
   /// Copies a deque's queued candidates into freshly allocated storage and swaps it in.
   /// </summary>
   /// <param name="deque">Deque whose storage is being replaced.</param>
   /// <param name="values">Newly allocated value storage, sized to the new capacity.</param>
   /// <param name="ticks">Newly allocated tick storage, sized to the new capacity.</param>
   void _relocate(Deque& deque, float* values, unsigned long* ticks)
   {
      for (size_t i = 0; i < deque.count; i++)
      {
         size_t oldIndex = (deque.head + i) % _capacity;
         values[i] = deque.values[oldIndex];
         ticks[i] = deque.ticks[oldIndex];
      }

      delete[] deque.values;
      delete[] deque.ticks;
      deque.values = values;
      deque.ticks = ticks;
      deque.head = 0;
   }

   /// <summary>
   /// Ensures both deques can hold at least the requested number of candidates, growing
   /// (and never shrinking) them as needed while preserving any candidates already queued.
   /// </summary>
   /// <param name="capacity">Minimum required capacity.</param>
   /// <returns>True if the deques have at least the requested capacity.</returns>
   bool _ensureCapacity(size_t capacity)
   {
      if (capacity <= _capacity)
      {
         return true;
      }

      float* minValues = new (std::nothrow) float[capacity];
      unsigned long* minTicks = new (std::nothrow) unsigned long[capacity];
      float* maxValues = new (std::nothrow) float[capacity];
      unsigned long* maxTicks = new (std::nothrow) unsigned long[capacity];
      if (minValues == nullptr || minTicks == nullptr || maxValues == nullptr || maxTicks == nullptr)
      {
         delete[] minValues;
         delete[] minTicks;
         delete[] maxValues;
         delete[] maxTicks;
         return false;
      }

      _relocate(_minDeque, minValues, minTicks);
      _relocate(_maxDeque, maxValues, maxTicks);

      _capacity = capacity;
      return true;
   }

   /// <summary>
   /// Queues a new candidate into a single deque, dropping any trailing candidates that
   /// can never become the extremum again now that this value has arrived, growing
   /// storage first if the deque is already full.
   /// </summary>
   /// <param name="deque">Deque to push the candidate onto.</param>
   /// <param name="trackMax">True to track the maximum, false to track the minimum.</param>
   /// <param name="value">Candidate value.</param>
   /// <param name="tick">Absolute timestamp the value was sampled at.</param>
   void _push(Deque& deque, bool trackMax, float value, unsigned long tick)
   {
      while (deque.count > 0)
      {
         size_t backIndex = (deque.head + deque.count - 1) % _capacity;
         bool backObsolete = trackMax ? (deque.values[backIndex] <= value) : (deque.values[backIndex] >= value);
         if (!backObsolete)
         {
            break;
         }
         deque.count--;
      }

      if (deque.count >= _capacity && !_ensureCapacity(_capacity == 0 ? 16 : _capacity * 2))
      {
         return;
      }

      size_t tailIndex = (deque.head + deque.count) % _capacity;
      deque.values[tailIndex] = value;
      deque.ticks[tailIndex] = tick;
      deque.count++;
   }

   /// <summary>
   /// Evicts candidates in a single deque older than the current window boundary.
   /// </summary>
   /// <param name="deque">Deque to evict expired candidates from.</param>
   /// <param name="oldestSurvivingTick">Timestamp of the oldest sample still retained by the window.</param>
   void _evictBefore(Deque& deque, unsigned long oldestSurvivingTick)
   {
      while (deque.count > 0 && deque.ticks[deque.head] < oldestSurvivingTick)
      {
         deque.head = (deque.head + 1) % _capacity;
         deque.count--;
      }
   }

   /// <summary>
   /// Evicts candidates from both deques that have aged out of the time window.
   /// </summary>
   void _evictExpired()
   {
      const unsigned long now = TimeFunc();
      const unsigned long oldestSurvivingTick = (now >= _durationMs) ? (now - _durationMs) : 0UL;
      _evictBefore(_minDeque, oldestSurvivingTick);
      _evictBefore(_maxDeque, oldestSurvivingTick);
   }

public:
   /// <summary>
   /// Initializes timed min/max tracking for the given duration.
   /// </summary>
   /// <param name="durationMs">The time window in milliseconds.</param>
   /// <param name="initialCapacity">The initial number of candidates that can be retained before growing.</param>
   explicit TimedMinMaxBase(unsigned long durationMs, size_t initialCapacity = 16)
   {
      _durationMs = std::max(durationMs, 1UL);
      _ensureCapacity(std::max(initialCapacity, static_cast<size_t>(1)));
   }

   TimedMinMaxBase(const TimedMinMaxBase&) = delete;
   TimedMinMaxBase& operator=(const TimedMinMaxBase&) = delete;

   /// <summary>
   /// Releases all deque storage.
   /// </summary>
   ~TimedMinMaxBase()
   {
      delete[] _minDeque.values;
      delete[] _minDeque.ticks;
      delete[] _maxDeque.values;
      delete[] _maxDeque.ticks;
   }

   /// <summary>
   /// Ensures the tracker can hold at least the requested number of candidates without
   /// reallocating, useful for pre-sizing before a hot loop.
   /// </summary>
   /// <param name="capacity">Minimum required capacity.</param>
   /// <returns>True if the tracker has at least the requested capacity.</returns>
   bool ensureCapacity(size_t capacity)
   {
      return _ensureCapacity(capacity);
   }

   /// <summary>
   /// Adds a value using the current time.
   /// </summary>
   /// <param name="value">Value to add.</param>
   void set(float value)
   {
      set(value, TimeFunc());
   }

   /// <summary>
   /// Adds a value at an explicit timestamp, for backfilling historical samples whose
   /// original sample time is already known.
   /// </summary>
   /// <param name="value">Value to add.</param>
   /// <param name="tick">Absolute timestamp the value was sampled at. Must be non-decreasing across calls.</param>
   void set(float value, unsigned long tick)
   {
      _evictExpired();
      _push(_minDeque, false, value, tick);
      _push(_maxDeque, true, value, tick);
   }

   /// <summary>
   /// Gets the minimum value within the time window.
   /// </summary>
   /// <returns>Minimum value, or NaN when no values are retained.</returns>
   float min()
   {
      _evictExpired();
      if (_minDeque.count == 0)
      {
         return NAN;
      }
      return _minDeque.values[_minDeque.head];
   }

   /// <summary>
   /// Gets the maximum value within the time window.
   /// </summary>
   /// <returns>Maximum value, or NaN when no values are retained.</returns>
   float max()
   {
      _evictExpired();
      if (_maxDeque.count == 0)
      {
         return NAN;
      }
      return _maxDeque.values[_maxDeque.head];
   }

   /// <summary>
   /// Gets max-minus-min within the time window.
   /// </summary>
   /// <returns>Range value, or NaN when no values are retained.</returns>
   float range()
   {
      float low = min();
      float high = max();
      if (!std::isfinite(low) || !std::isfinite(high))
      {
         return NAN;
      }
      return high - low;
   }

   /// <summary>
   /// Gets the configured duration in milliseconds.
   /// </summary>
   /// <returns>Total tracked duration in milliseconds.</returns>
   unsigned long durationMs() const
   {
      return _durationMs;
   }

   /// <summary>
   /// Sets the tracked time duration and clears existing candidates.
   /// </summary>
   /// <param name="durationMs">New total duration in milliseconds.</param>
   void setDurationMs(unsigned long durationMs)
   {
      _durationMs = std::max(durationMs, 1UL);
      reset();
   }

   /// <summary>
   /// Discards all queued candidates.
   /// </summary>
   void reset()
   {
      _minDeque.head = 0;
      _minDeque.count = 0;
      _maxDeque.head = 0;
      _maxDeque.count = 0;
   }
};

using TimedMinMax = TimedMinMaxBase<millis>;
