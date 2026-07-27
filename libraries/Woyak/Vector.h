#pragma once

#include <new>
#include <span>

#include "Util.h"

///
/// <summary>
/// A minimal, reusable growable array, similar in spirit to std::vector but using
/// new (std::nothrow) for growth and reporting allocation failure through
/// Util::setHaltReason()/Util::reset() instead of throwing, since this codebase avoids
/// C++ exceptions (common on embedded Arduino cores built with -fno-exceptions). Grows in
/// fixed-size blocks (see GROWTH_INCREMENT) rather than doubling, since these arrays are
/// typically small (e.g. one entry per chart series) and predictable growth keeps
/// reallocations infrequent without over-allocating.
/// </summary>
///
template <typename T>
class Vector
{
private:
   static constexpr size_t GROWTH_INCREMENT = 5;

   T* _items = nullptr;
   size_t _count = 0;
   size_t _capacity = 0;

   ///
   /// <summary>
   /// Grows the backing array to at least minCapacity, copying existing elements over.
   /// Safe to call repeatedly; a no-op if capacity is already sufficient.
   /// </summary>
   /// <param name="minCapacity">The minimum capacity required.</param>
   /// <returns>True if the array has at least minCapacity capacity (either already, or after growing).</returns>
   ///
   bool _ensureCapacity(size_t minCapacity)
   {
      if (minCapacity <= _capacity)
      {
         return true;
      }

      size_t newCapacity = _capacity + GROWTH_INCREMENT;
      if (newCapacity < minCapacity)
      {
         newCapacity = minCapacity;
      }

      T* newItems = new (std::nothrow) T[newCapacity];
      if (newItems == nullptr)
      {
         Util::setHaltReason("OOM growing Vector");
         Util::reset();
         return false;
      }

      for (size_t i = 0; i < _count; i++)
      {
         newItems[i] = _items[i];
      }

      delete[] _items;
      _items = newItems;
      _capacity = newCapacity;
      return true;
   }

public:
   Vector() = default;

   Vector(const Vector&) = delete;
   Vector& operator=(const Vector&) = delete;

   ~Vector()
   {
      delete[] _items;
   }

   ///
   /// <summary>
   /// Appends a new default-constructed element, growing the backing array if necessary.
   /// </summary>
   /// <returns>Reference to the newly appended element.</returns>
   ///
   T& append()
   {
      if (!_ensureCapacity(_count + 1))
      {
         // Util::reset() above should not return, but guard against
         // indexing into a null/undersized backing array just in case.
         static T fallback;
         fallback = T();
         return fallback;
      }
      _items[_count] = T();
      return _items[_count++];
   }

   ///
   /// <summary>
   /// Removes the element at the given index, shifting later elements down by one. A
   /// no-op if index is out of range.
   /// </summary>
   /// <param name="index">Index of the element to remove.</param>
   ///
   void removeAt(size_t index)
   {
      if (index >= _count)
      {
         return;
      }

      for (size_t i = index; i < _count - 1; i++)
      {
         _items[i] = _items[i + 1];
      }
      _count--;
   }

   ///
   /// <summary>
   /// Removes all elements without shrinking the backing array's capacity.
   /// </summary>
   ///
   void clear()
   {
      _count = 0;
   }

   ///
   /// <summary>
   /// Gets the number of elements currently in the array.
   /// </summary>
   /// <returns>The element count.</returns>
   ///
   size_t size() const
   {
      return _count;
   }

   T& operator[](size_t index)
   {
      return _items[index];
   }

   const T& operator[](size_t index) const
   {
      return _items[index];
   }

   ///
   /// <summary>
   /// Gets a pointer to the backing array, e.g. for constructing a std::span over the
   /// current elements. Only the first size() entries are valid.
   /// </summary>
   /// <returns>Pointer to the backing array, or nullptr if empty.</returns>
   ///
   T* data()
   {
      return _items;
   }

   ///
   /// <summary>
   /// Gets a pointer to the backing array, e.g. for constructing a std::span over the
   /// current elements. Only the first size() entries are valid.
   /// </summary>
   /// <returns>Pointer to the backing array, or nullptr if empty.</returns>
   ///
   const T* data() const
   {
      return _items;
   }

   ///
   /// <summary>
   /// Implicitly converts to a std::span over the current elements, so a Vector can be
   /// passed directly wherever a span is expected (e.g. SerialTable's constructor).
   /// </summary>
   ///
   operator std::span<T>()
   {
      return std::span<T>(_items, _count);
   }

   ///
   /// <summary>
   /// Implicitly converts to a std::span over the current elements, so a Vector can be
   /// passed directly wherever a span is expected (e.g. SerialTable's constructor).
   /// </summary>
   ///
   operator std::span<const T>() const
   {
      return std::span<const T>(_items, _count);
   }
};
