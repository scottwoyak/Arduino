#pragma once

#include <Arduino.h>
#include "Format.h"

///
/// <summary>
/// Abstract base for one editable setup value shown by SetupDisplay or SetupTable.
/// Subclasses bind to a caller-owned variable and implement type-specific adjustment and
/// display. Persistence (loading/saving to Preferences) is handled entirely by
/// SetupTable, using the generic numericValue()/setNumericValue()/defaultNumericValue()
/// accessors below, so SetupField itself has no Preferences dependency.
/// </summary>
///
class SetupField
{
public:
   ///
   /// <summary>
   /// Initializes a new instance of the SetupField class.
   /// </summary>
   /// <param name="label">Label text drawn before the value, e.g. "Rate: ".</param>
   /// <param name="format">Format used to render the value for display.</param>
   ///
   SetupField(const char* label, const Format& format)
      : _label(label), _format(format)
   {
   }

   virtual ~SetupField()
   {
   }

   ///
   /// <summary>
   /// Gets the label text drawn before the value.
   /// </summary>
   /// <returns>The field's label text.</returns>
   ///
   const char* label() const
   {
      return _label;
   }

   ///
   /// <summary>
   /// Restores this field's in-memory value to its default (does not persist).
   /// </summary>
   ///
   virtual void reset() = 0;

   ///
   /// <summary>
   /// Adjusts this field's value by one or more encoder detents, clamped to its valid range.
   /// </summary>
   /// <param name="direction">Signed number of encoder steps to apply.</param>
   ///
   virtual void adjust(int32_t direction) = 0;

   ///
   /// <summary>
   /// Formats this field's current value for display.
   /// </summary>
   /// <returns>The formatted value text.</returns>
   ///
   virtual std::string valueText() = 0;

   ///
   /// <summary>
   /// Gets this field's current value as a double, for generic Preferences persistence.
   /// </summary>
   /// <returns>The current value.</returns>
   ///
   virtual double numericValue() const = 0;

   ///
   /// <summary>
   /// Sets this field's current value from a double (clamped to its valid range), for
   /// generic Preferences persistence.
   /// </summary>
   /// <param name="value">The value to set.</param>
   ///
   virtual void setNumericValue(double value) = 0;

   ///
   /// <summary>
   /// Gets this field's default value as a double, for generic Preferences persistence.
   /// </summary>
   /// <returns>The default value.</returns>
   ///
   virtual double defaultNumericValue() const = 0;

protected:
   const char* _label;
   Format _format;
};

///
/// <summary>
/// Integer setup field backed by a caller-owned long. Override _stepValue in a subclass to
/// implement non-linear stepping.
/// </summary>
///
class IntSetupField : public SetupField
{
public:
   ///
   /// <summary>
   /// Initializes a new instance of the IntSetupField class.
   /// </summary>
   /// <param name="label">Label text drawn before the value, e.g. "Max Samples: ".</param>
   /// <param name="value">Caller-owned variable that holds the current value.</param>
   /// <param name="minValue">Minimum allowed value.</param>
   /// <param name="maxValue">Maximum allowed value.</param>
   /// <param name="step">Linear step size used by the default _stepValue implementation.</param>
   /// <param name="defaultValue">Default value used when no saved value exists or on reset.</param>
   /// <param name="format">Format used to render the value for display.</param>
   ///
   IntSetupField(const char* label, long* value,
                 long minValue, long maxValue, long step, long defaultValue,
                 const Format& format)
      : SetupField(label, format),
        _value(value), _minValue(minValue), _maxValue(maxValue), _step(step), _default(defaultValue)
   {
   }

   void reset() override
   {
      *_value = _default;
   }

   void adjust(int32_t direction) override
   {
      long newValue = _stepValue(*_value, direction);
      *_value = constrain(newValue, _minValue, _maxValue);
   }

   std::string valueText() override
   {
      return _format.toString((double)*_value);
   }

   double numericValue() const override
   {
      return (double)*_value;
   }

   void setNumericValue(double value) override
   {
      *_value = constrain((long)value, _minValue, _maxValue);
   }

   double defaultNumericValue() const override
   {
      return (double)_default;
   }

protected:
   ///
   /// <summary>
   /// Computes the new (pre-clamp) value after one encoder step. The default implementation
   /// applies a linear step; override to implement non-linear stepping.
   /// </summary>
   /// <param name="current">Current field value.</param>
   /// <param name="direction">Signed number of encoder steps to apply.</param>
   /// <returns>The new value before clamping to [min, max].</returns>
   ///
   virtual long _stepValue(long current, int32_t direction)
   {
      return current + (direction * _step);
   }

   long* _value;
   long _minValue;
   long _maxValue;
   long _step;
   long _default;
};

///
/// <summary>
/// Floating-point setup field backed by a caller-owned float. Override _stepValue in a subclass
/// to implement non-linear stepping.
/// </summary>
///
class FloatSetupField : public SetupField
{
public:
   ///
   /// <summary>
   /// Initializes a new instance of the FloatSetupField class.
   /// </summary>
   /// <param name="label">Label text drawn before the value.</param>
   /// <param name="value">Caller-owned variable that holds the current value.</param>
   /// <param name="minValue">Minimum allowed value.</param>
   /// <param name="maxValue">Maximum allowed value.</param>
   /// <param name="step">Linear step size used by the default _stepValue implementation.</param>
   /// <param name="defaultValue">Default value used when no saved value exists or on reset.</param>
   /// <param name="format">Format used to render the value for display.</param>
   ///
   FloatSetupField(const char* label, float* value,
                    float minValue, float maxValue, float step, float defaultValue,
                    const Format& format)
      : SetupField(label, format),
        _value(value), _minValue(minValue), _maxValue(maxValue), _step(step), _default(defaultValue)
   {
   }

   void reset() override
   {
      *_value = _default;
   }

   void adjust(int32_t direction) override
   {
      float newValue = _stepValue(*_value, direction);
      *_value = constrain(newValue, _minValue, _maxValue);
   }

   std::string valueText() override
   {
      return _format.toString((double)*_value);
   }

   double numericValue() const override
   {
      return (double)*_value;
   }

   void setNumericValue(double value) override
   {
      *_value = constrain((float)value, _minValue, _maxValue);
   }

   double defaultNumericValue() const override
   {
      return (double)_default;
   }

protected:
   ///
   /// <summary>
   /// Computes the new (pre-clamp) value after one encoder step. The default implementation
   /// applies a linear step; override to implement non-linear stepping.
   /// </summary>
   /// <param name="current">Current field value.</param>
   /// <param name="direction">Signed number of encoder steps to apply.</param>
   /// <returns>The new value before clamping to [min, max].</returns>
   ///
   virtual float _stepValue(float current, int32_t direction)
   {
      return current + (direction * _step);
   }

   float* _value;
   float _minValue;
   float _maxValue;
   float _step;
   float _default;
};

