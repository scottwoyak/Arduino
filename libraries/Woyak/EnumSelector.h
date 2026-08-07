#pragma once

#include <Arduino.h>
#include "Button.h"
#include "IEncoder.h"

///
/// <summary>
/// Cycles through a fixed set of enum values using either a rotary encoder's rotation
/// delta or a button's press events, wrapping around at either end. Intended for enums
/// such as <c>enum class ResultView : uint8_t { Charts, Table };</c> where <c>Table</c>
/// is the last enumerator, but usable for any enum-driven selection.
/// </summary>
///
template<typename Enum>
class EnumSelector
{
private:
   IEncoder* _encoder = nullptr;
   Button* _button = nullptr;
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
   EnumSelector(IEncoder& encoder, Enum lastValue, Enum initialValue)
      : _encoder(&encoder),
        _value(initialValue),
        _numValues(static_cast<int32_t>(lastValue) + 1)
   {
   }

   ///
   /// <summary>
   /// Constructs an EnumSelector bound to an existing button. Each debounced press
   /// advances the value by one, wrapping around at either end.
   /// </summary>
   /// <param name="button">Button whose wasPressed() drives selection changes.</param>
   /// <param name="lastValue">The last (highest-valued) enumerator in the enum.</param>
   /// <param name="initialValue">The value to start on.</param>
   ///
   EnumSelector(Button& button, Enum lastValue, Enum initialValue)
      : _button(&button),
        _value(initialValue),
        _numValues(static_cast<int32_t>(lastValue) + 1)
   {
   }

   ///
   /// <summary>
   /// Checks the bound encoder/button for input since the last call and advances/wraps
   /// the current value accordingly.
   /// </summary>
   /// <returns>True if the value changed; false otherwise.</returns>
   ///
   bool hasChanged()
   {
      int32_t delta = 0;
      if (_encoder != nullptr)
      {
         delta = _encoder->delta();
      }
      else if (_button != nullptr && _button->wasPressed())
      {
         delta = 1;
      }

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
   /// Resets the selector to the given value and clears the bound encoder's/button's
   /// input baseline.
   /// </summary>
   /// <param name="value">Value to reset to.</param>
   ///
   void reset(Enum value)
   {
      _value = value;
      if (_encoder != nullptr)
      {
         _encoder->reset();
      }
      else if (_button != nullptr)
      {
         _button->reset();
      }
   }
};
