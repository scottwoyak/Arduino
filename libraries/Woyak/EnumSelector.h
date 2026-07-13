#pragma once

#include <Arduino.h>
#include "RotaryEncoder.h"

///
/// <summary>
/// Cycles through a fixed set of enum values using a rotary encoder's rotation delta,
/// wrapping around at either end. Intended for enums such as
/// <c>enum class ResultView : uint8_t { Charts, Table };</c> where <c>Table</c> is the
/// last enumerator, but usable for any enum-driven encoder selection.
/// </summary>
///
template<typename Enum>
class EnumSelector
{
private:
   RotaryEncoder& _encoder;
   Enum _value;
   int32_t _numValues;

public:
   ///
   /// <summary>
   /// Constructs an EnumSelector bound to an existing rotary encoder.
   /// </summary>
   /// <param name="encoder">Rotary encoder whose delta() drives selection changes.</param>
   /// <param name="lastValue">The last (highest-valued) enumerator in the enum.</param>
   /// <param name="initialValue">The value to start on.</param>
   ///
   EnumSelector(RotaryEncoder& encoder, Enum lastValue, Enum initialValue)
      : _encoder(encoder),
        _value(initialValue),
        _numValues(static_cast<int32_t>(lastValue) + 1)
   {
   }

   ///
   /// <summary>
   /// Checks the encoder for rotation since the last call and advances/wraps the
   /// current value accordingly.
   /// </summary>
   /// <returns>True if the value changed; false otherwise.</returns>
   ///
   bool hasChanged()
   {
      int32_t delta = _encoder.delta();
      if (delta == 0)
      {
         return false;
      }

      int32_t index = (static_cast<int32_t>(_value) + (delta % _numValues) + _numValues) % _numValues;
      _value = static_cast<Enum>(index);
      return true;
   }

   ///
   /// <summary>
   /// Gets the currently selected value.
   /// </summary>
   /// <returns>Current value.</returns>
   ///
   Enum value() const
   {
      return _value;
   }

   ///
   /// <summary>
   /// Resets the selector to the given value and clears the encoder's delta() baseline.
   /// </summary>
   /// <param name="value">Value to reset to.</param>
   ///
   void reset(Enum value)
   {
      _value = value;
      _encoder.reset();
   }
};
