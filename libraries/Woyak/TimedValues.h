#pragma once

#include <algorithm>
#include <Arduino.h>

/// <summary>
/// Stores values for a fixed duration using a circular buffer of timestamped entries.
/// </summary>
///
/// <remarks>
/// Values are returned in newest-first order, and expired entries are discarded when the buffer is queried or written.
/// </remarks>
///
/// <typeparam name="T">The numeric type of values to store.</typeparam>
template<typename T = float, unsigned long (*TimeFunc)() = millis>
class TimedValuesBase
{
private:
   struct Entry
   {
	  T value = T(0);
	  unsigned long ticks = 0;
   };

   Entry* _entries = nullptr;
   size_t _capacity = 0;
   size_t _count = 0;
   size_t _head = 0;
   unsigned long _durationMs = 1;

   /// <summary>
   /// Removes values that have aged out of the time window.
   /// </summary>
   void _trimExpired()
   {
	  if (_count == 0)
	  {
		 return;
	  }

	  const unsigned long now = TimeFunc();
	  while (_count > 0)
	  {
		 const Entry& entry = _entries[_head];
		 if ((now - entry.ticks) < _durationMs)
		 {
			break;
		 }

		 _head++;
		 if (_head >= _capacity)
		 {
			_head = 0;
		 }

		 _count--;
	  }
   }

   /// <summary>
   /// Ensures the buffer can hold at least the requested number of entries.
   /// </summary>
   /// <param name="capacity">Desired capacity.</param>
   void _ensureCapacity(size_t capacity)
   {
	  if (capacity <= _capacity)
	  {
		 return;
	  }

	  Entry* next = new Entry[capacity];
	  for (size_t i = 0; i < _count; i++)
	  {
		 next[i] = _entries[(_head + i) % _capacity];
	  }

	  delete[] _entries;
	  _entries = next;
	  _capacity = capacity;
	  _head = 0;
   }

public:
   /// <summary>
   /// Initializes a timed value buffer with the specified duration and initial capacity.
   /// </summary>
   /// <param name="durationMs">The time window in milliseconds.</param>
   /// <param name="initialCapacity">The initial number of values that can be retained before growing.</param>
   explicit TimedValuesBase(unsigned long durationMs, size_t initialCapacity = 16)
   {
	  _durationMs = std::max(durationMs, static_cast<unsigned long>(1));
	  _capacity = std::max(initialCapacity, static_cast<size_t>(1));
	  _entries = new Entry[_capacity];
   }

   TimedValuesBase(const TimedValuesBase&) = delete;
   TimedValuesBase& operator=(const TimedValuesBase&) = delete;

   /// <summary>
   /// Releases the allocated buffer.
   /// </summary>
   ~TimedValuesBase()
   {
	  delete[] _entries;
   }

   /// <summary>
   /// Gets the number of values the buffer can retain before growing.
   /// </summary>
   /// <returns>The current buffer capacity.</returns>
   size_t size() const
   {
	  return _capacity;
   }

   /// <summary>
   /// Gets the number of values currently retained within the time window.
   /// </summary>
   /// <returns>The number of unexpired values.</returns>
   size_t count()
   {
	  _trimExpired();
	  return _count;
   }

   /// <summary>
   /// Adds a new value and records the current timestamp.
   /// </summary>
   /// <param name="value">The value to store.</param>
   void set(T value)
   {
	  _trimExpired();

	  if (_count >= _capacity)
	  {
		 _ensureCapacity(_capacity * 2);
	  }

	  const size_t tail = (_head + _count) % _capacity;
	  _entries[tail].value = value;
	  _entries[tail].ticks = TimeFunc();
	  _count++;
   }

   /// <summary>
   /// Gets a value relative to the newest retained entry.
   /// </summary>
   /// <param name="index">0 for the newest value, 1 for the previous value, and so on.</param>
   /// <returns>The requested value, or default-constructed value when the index is out of range.</returns>
   T get(size_t index)
   {
	  _trimExpired();

	  if (index >= _count)
	  {
		 return T(0);
	  }

	  size_t newestOffset = _count - 1 - index;
	  size_t entryIndex = (_head + newestOffset) % _capacity;
	  return _entries[entryIndex].value;
   }

   /// <summary>
   /// Gets the age in milliseconds for a retained value.
   /// </summary>
   /// <param name="index">0 for the newest value, 1 for the previous value, and so on.</param>
   /// <returns>The age in milliseconds, or the configured duration when the index is out of range.</returns>
   unsigned long ageMs(size_t index)
   {
	  _trimExpired();

	  if (index >= _count)
	  {
		 return _durationMs;
	  }

	  size_t newestOffset = _count - 1 - index;
	  size_t entryIndex = (_head + newestOffset) % _capacity;
	  return TimeFunc() - _entries[entryIndex].ticks;
   }

   /// <summary>
   /// Copies the retained values into caller-provided buffers in newest-first order.
   /// </summary>
   /// <param name="values">Destination for retained values.</param>
   /// <param name="agesMs">Optional destination for ages in milliseconds.</param>
   /// <param name="maxCount">Maximum number of entries to copy.</param>
   /// <returns>The number of copied entries.</returns>
   size_t snapshot(T* values, unsigned long* agesMs = nullptr, size_t maxCount = SIZE_MAX)
   {
	  _trimExpired();

	  size_t copyCount = std::min(_count, maxCount);
	  unsigned long now = TimeFunc();
	  for (size_t i = 0; i < copyCount; i++)
	  {
		 size_t newestOffset = copyCount - 1 - i;
		 size_t entryIndex = (_head + newestOffset) % _capacity;
		 values[i] = _entries[entryIndex].value;
		 if (agesMs != nullptr)
		 {
			agesMs[i] = now - _entries[entryIndex].ticks;
		 }
	  }

	  return copyCount;
   }

   /// <summary>
   /// Gets the configured time window.
   /// </summary>
   /// <returns>The duration in milliseconds.</returns>
   unsigned long durationMs() const
   {
	  return _durationMs;
   }

   /// <summary>
   /// Updates the time window and clears retained values.
   /// </summary>
   /// <param name="durationMs">The new time window in milliseconds.</param>
   void setDurationMs(unsigned long durationMs)
   {
	  _durationMs = std::max(durationMs, static_cast<unsigned long>(1));
	  reset();
   }

   /// <summary>
   /// Clears all retained values.
   /// </summary>
   void reset()
   {
	  _head = 0;
	  _count = 0;
   }
};

/// <summary>
/// Convenience alias for TimedValuesBase using float values.
/// </summary>
using TimedValues = TimedValuesBase<float>;
