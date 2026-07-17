#pragma once

#include <Arduino.h>
#include "Format.h"

///
/// <summary>
/// Abstract base for one editable setup value shown by DisplayEditor or DisplayEditableTable.
/// Subclasses bind to a caller-owned variable and implement type-specific adjustment and
/// display. Persistence (loading/saving to Preferences) is handled entirely by
/// DisplayEditableTable, using the generic numericValue()/setNumericValue()/defaultNumericValue()
/// accessors below, so DisplayEditableField itself has no Preferences dependency.
/// </summary>
///
class DisplayEditableField
{
public:
   ///
   /// <summary>
   /// Initializes a new instance of the DisplayEditableField class.
   /// </summary>
   /// <param name="label">Label text drawn before the value, e.g. "Rate: ".</param>
   /// <param name="format">Format used to render the value for display.</param>
   ///
   DisplayEditableField(const char* label, const Format& format)
      : _label(label), _format(format)
   {
   }

   virtual ~DisplayEditableField()
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

       ///
       /// <summary>
       /// Gets whether this field can be selected and adjusted live via the encoders. Read-only
       /// fields (e.g. measured values) return false so DisplayEditableTable skips them when
       /// cycling the selection and never highlights or persists them.
       /// </summary>
       /// <returns>True if the field is selectable/adjustable; false if it is display-only.</returns>
       ///
           virtual bool isEditable() const
           {
              return true;
           }

           ///
           /// <summary>
           /// Gets whether this field is currently enabled for interaction/display, e.g. so a
           /// field can be temporarily grayed out and skipped by encoder selection when it is
           /// not relevant to the current configuration (like a noise StdDev row while noise is
           /// off) without being permanently read-only like a measured value. The default
           /// implementation always returns true.
           /// </summary>
           /// <returns>True if the field is currently enabled; false if it should appear dimmed and be skipped.</returns>
           ///
           virtual bool isEnabled() const
           {
              return true;
           }

           ///
           /// <summary>
           /// Sets the section header text drawn above this field when it appears in a
           /// DisplayEditableTable, e.g. "Plot" or "Measured". A field with no section set (the
           /// default) is drawn as part of the previous field's section.
           /// </summary>
           /// <param name="section">Section header text, or nullptr for no header.</param>
           ///
           void setSection(const char* section)
           {
              _section = section;
           }

           ///
           /// <summary>
           /// Gets the section header text drawn above this field, or nullptr if this field
           /// continues the previous field's section.
           /// </summary>
           /// <returns>The section header text, or nullptr.</returns>
           ///
           const char* section() const
           {
              return _section;
           }

       protected:
           const char* _label;
           Format _format;
           const char* _section = nullptr;
       };

   ///
   /// <summary>
   /// Display-only field backed by a caller-owned float, for measured values (e.g. a live rate)
   /// that need to share a table/alignment with editable fields but cannot be selected or adjusted.
   /// </summary>
   ///
   class ReadOnlyDisplayEditableField : public DisplayEditableField
   {
   public:
       ///
       /// <summary>
       /// Initializes a new instance of the ReadOnlyDisplayEditableField class.
       /// </summary>
       /// <param name="label">Label text drawn before the value.</param>
       /// <param name="value">Caller-owned variable that holds the current value.</param>
       /// <param name="format">Format used to render the value for display.</param>
       ///
       ReadOnlyDisplayEditableField(const char* label, float* value, const Format& format)
          : DisplayEditableField(label, format), _value(value)
       {
       }

       void reset() override
       {
       }

       void adjust(int32_t direction) override
       {
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
       }

       double defaultNumericValue() const override
       {
          return 0.0;
       }

       bool isEditable() const override
       {
          return false;
       }

           private:
               float* _value;
           };

       ///
       /// <summary>
       /// Integer setup field backed by a caller-owned long. Override _stepValue in a subclass to
       /// implement non-linear stepping.
       /// </summary>
       ///
       class IntDisplayEditableField : public DisplayEditableField
{
public:
   ///
   /// <summary>
   /// Initializes a new instance of the IntDisplayEditableField class.
   /// </summary>
   /// <param name="label">Label text drawn before the value, e.g. "Max Samples: ".</param>
   /// <param name="value">Caller-owned variable that holds the current value.</param>
   /// <param name="minValue">Minimum allowed value.</param>
   /// <param name="maxValue">Maximum allowed value.</param>
   /// <param name="step">Linear step size used by the default _stepValue implementation.</param>
   /// <param name="defaultValue">Default value used when no saved value exists or on reset.</param>
   /// <param name="format">Format used to render the value for display.</param>
   ///
   IntDisplayEditableField(const char* label, long* value,
                 long minValue, long maxValue, long step, long defaultValue,
                 const Format& format)
      : DisplayEditableField(label, format),
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
   /// Enumerated-selection field backed by a caller-owned long index into a fixed array of
   /// string labels. Adjusting the field steps through the labels by index, wrapping around at
   /// either end, and valueText() displays the selected label instead of a raw index number.
   /// Replaces the common pattern of writing a one-off IntDisplayEditableField subclass just to
   /// step through and display a small fixed set of string options (e.g. "Points"/"Lines",
   /// "True"/"False", "Fixed"/"Timed").
   /// </summary>
   ///
   class EnumDisplayEditableField : public IntDisplayEditableField
   {
   public:
      ///
      /// <summary>
      /// Initializes a new instance of the EnumDisplayEditableField class.
      /// </summary>
      /// <param name="label">Label text drawn before the value.</param>
      /// <param name="value">Caller-owned variable that holds the current selected index.</param>
      /// <param name="labels">Array of label strings to step through and display.</param>
      /// <param name="labelCount">Number of entries in labels.</param>
      /// <param name="defaultValue">Default index used when no saved value exists or on reset.</param>
      /// <param name="format">Format used to render the selected label for display.</param>
      ///
      EnumDisplayEditableField(const char* label, long* value, const char* const* labels,
         size_t labelCount, long defaultValue, const Format& format)
         : IntDisplayEditableField(label, value, 0, (long)labelCount - 1, 1, defaultValue, format),
           _labels(labels), _labelCount(labelCount)
      {
      }

      void adjust(int32_t direction) override
      {
         long count = (long)_labelCount;
         long newValue = (*_value + (direction > 0 ? 1 : -1) + count) % count;
         *_value = newValue;
      }

      std::string valueText() override
      {
         long index = constrain(*_value, 0L, (long)(_labelCount - 1));
         return _format.toString(_labels[index]);
      }

   private:
      const char* const* _labels;
      size_t _labelCount;
   };

///
/// <summary>
/// Floating-point setup field backed by a caller-owned float. Override _stepValue in a subclass
/// to implement non-linear stepping.
/// </summary>
///
class FloatDisplayEditableField : public DisplayEditableField
{
public:
   ///
   /// <summary>
   /// Initializes a new instance of the FloatDisplayEditableField class.
   /// </summary>
   /// <param name="label">Label text drawn before the value.</param>
   /// <param name="value">Caller-owned variable that holds the current value.</param>
   /// <param name="minValue">Minimum allowed value.</param>
   /// <param name="maxValue">Maximum allowed value.</param>
   /// <param name="step">Linear step size used by the default _stepValue implementation.</param>
   /// <param name="defaultValue">Default value used when no saved value exists or on reset.</param>
   /// <param name="format">Format used to render the value for display.</param>
   ///
   FloatDisplayEditableField(const char* label, float* value,
                    float minValue, float maxValue, float step, float defaultValue,
                    const Format& format)
      : DisplayEditableField(label, format),
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

