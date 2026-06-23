#pragma once

#include <Arduino.h>

/// <summary>
/// Stores a fixed-size rolling set of float values in a circular buffer.
/// </summary>
class RollingValues
{
private:
   size_t _index = 0;
   float* _values;
   size_t _size;
   float _initialValue = 0.0f;
   bool _isFull = false;

public:
   /// <summary>
   /// Initializes a new rolling value buffer with the specified size.
   /// </summary>
   /// <param name="size">The number of values to retain.</param>
   /// <param name="initialValue">The value to fill empty slots with. Defaults to 0.</param>
   explicit RollingValues(size_t size, float initialValue = 0.0f)
      : _values(new float[size]), _size(size), _initialValue(initialValue)
   {
      for (size_t i = 0; i < _size; i++)
      {
         _values[i] = _initialValue;
      }
   }

   RollingValues(const RollingValues&) = delete;
   RollingValues& operator=(const RollingValues&) = delete;

   /// <summary>
   /// Releases the allocated buffer.
   /// </summary>
   ~RollingValues()
   {
      delete[] _values;
   }

   /// <summary>
   /// Gets the number of values the buffer can hold.
   /// </summary>
   size_t size() const
   {
      return _size;
   }

   /// <summary>
   /// Gets the number of values that have been written to the buffer.
   /// </summary>
   size_t count() const
   {
      return _isFull ? _size : _index;
   }

   /// <summary>
   /// Adds a new value and returns whether a previous value was overwritten.
   /// </summary>
   /// <param name="value">The value to store.</param>
   /// <param name="removed">Optional pointer that receives the overwritten value when true is returned.</param>
   /// <returns>True when a prior value was overwritten; otherwise false.</returns>
   bool set(float value, float* removed = nullptr)
   {
      if (_size == 0)
      {
         return false;
      }

      const bool hasRemoved = _isFull;

      _index++;
      if (_index >= _size)
      {
         _index = 0;
         _isFull = true;
      }

      if (hasRemoved && removed != nullptr)
      {
         *removed = _values[_index];
      }

      _values[_index] = value;
      return hasRemoved;
   }

   /// <summary>
   /// Gets a value relative to the current index.
   /// </summary>
   /// <param name="index">0 for latest value, 1 for previous, and so on.</param>
   /// <returns>The requested value, or NaN when the buffer is empty.</returns>
   float get(size_t index) const
   {
      if (_size == 0)
      {
         return NAN;
      }

      int i = static_cast<int>(_index) - static_cast<int>(index);

      if (i < 0)
      {
         i += static_cast<int>(_size);
      }

      return _values[i];
   }

   /// <summary>
   /// Resets the buffer to its initial state.
   /// </summary>
   void reset()
   {
      _index = 0;
      _isFull = false;
      for (size_t i = 0; i < _size; i++)
      {
         _values[i] = _initialValue;
      }
   }

   /// <summary>
   /// Resizes the buffer and clears all existing values.
   /// </summary>
   /// <param name="size">The new number of values to retain.</param>
   void resize(size_t size)
   {
      if (size == _size)
      {
         reset();
         return;
      }

      delete[] _values;
      _size = size;
      _index = 0;
      _isFull = false;
      _values = new float[_size];
      for (size_t i = 0; i < _size; i++)
      {
         _values[i] = _initialValue;
      }
   }
};
