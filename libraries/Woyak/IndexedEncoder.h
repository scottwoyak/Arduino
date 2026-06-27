#pragma once

#include <Arduino.h>
#include "RotaryEncoder.h"

/// <summary>
/// Rotary encoder that wraps position around a specified item count.
/// </summary>
/// <remarks>
/// Extends RotaryEncoder to provide circular list navigation. Automatically wraps
/// position to keep it within the range [0, numItems), useful for menu selection
/// and cyclic index traversal.
/// </remarks>
class IndexedEncoder : public RotaryEncoder
{
private:
   int _numItems;

public:
   /// <summary>
   /// Constructs an IndexedEncoder with the specified pins and item count.
   /// </summary>
   /// <param name="pinA">GPIO pin for encoder phase A</param>
   /// <param name="pinB">GPIO pin for encoder phase B</param>
   /// <param name="buttonPin">GPIO pin for encoder pushbutton</param>
   /// <param name="numItems">Number of items in the circular list</param>
   IndexedEncoder(uint8_t pinA, uint8_t pinB, uint8_t buttonPin, uint16_t numItems) : RotaryEncoder(pinA, pinB, buttonPin)
   {
      _numItems = numItems;
   }

   /// <summary>
   /// Sets the current index and updates the item count.
   /// </summary>
   /// <param name="index">The current index (will be wrapped to [0, numItems))</param>
   /// <param name="numItems">The number of items in the list</param>
   void setIndex(uint16_t index, uint16_t numItems)
   {
      setPosition(index);
      _numItems = numItems;
   }

   /// <summary>
   /// Gets the current index, automatically wrapped to the item count.
   /// </summary>
   /// <returns>Index in range [0, numItems)</returns>
   uint16_t getIndex() const
   {
      long value = getPosition();

      while (value < 0)
      {
         value += _numItems;
      }

      return value % _numItems;
   }
};
