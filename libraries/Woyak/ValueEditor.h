#pragma once

#include <Arduino.h>
#include <span>
#include "Color.h"
#include "Format.h"

///
/// <summary>
/// Abstract base for one value shown as a row in a Table-based view (FieldTableEditor),
/// whether or not it can be edited.
/// (format, value rendering). Row-level metadata (label, section header) is owned by the
/// FieldTableEditor::Row that wraps this value, not the value itself. See Editor for the
/// additional contract implemented by editable fields.
/// </summary>
///
class ValueBase
{
public:
   ///
   /// <summary>
   /// Initializes a new instance of the ValueBase class.
   /// </summary>
   /// <param name="format">Format used to render the value for display.</param>
   ///
   ValueBase(const Format& format)
      : _format(format)
   {}

   virtual ~ValueBase()
   {}

   ///
   /// <summary>
   /// Formats this field's current value for display.
   /// </summary>
   /// <returns>The formatted value text.</returns>
   ///
   virtual std::string valueText() = 0;

   ///
   /// <summary>
   /// Gets whether this field can be selected and adjusted live via the encoders. Read-only
   /// fields (e.g. measured values) return false so FieldTableEditor skips them when
   /// cycling the selection and never highlights or persists them.
   /// </summary>
   /// <returns>True if the field is selectable/adjustable; false if it is display-only.</returns>
   ///
   virtual bool isEditable() const
   {
      return false;
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
   /// Gets the format used to render this field's value.
   /// </summary>
   /// <returns>The field's format.</returns>
   ///
   const Format& format() const
   {
      return _format;
   }

   ///
   /// <summary>
   /// Gets whether this field overrides its normal value color (e.g. to reflect a status
   /// like connecting/connected/error). Default is false, so FieldTableEditor falls back
   /// to its usual editable/read-only color.
   /// </summary>
   /// <returns>True if color() should be used instead of the default value color.</returns>
   ///
   virtual bool hasColor() const
   {
      return false;
   }

   ///
   /// <summary>
   /// Gets the overridden value color, when hasColor() is true.
   /// </summary>
   /// <returns>The color to draw the value text.</returns>
   ///
   virtual Color color() const
   {
      return Color::VALUE2;
   }

protected:
   Format _format;
};

///
/// <summary>
/// Abstract base for one editable setup value shown by FieldTableEditor.
/// Subclasses bind to a caller-owned variable and implement type-specific adjustment and
/// display. Persistence (loading/saving to Preferences) is handled entirely by
/// FieldTableEditor, using the generic numericValue()/setNumericValue()/defaultNumericValue()
/// accessors below, so Editor itself has no Preferences dependency.
/// </summary>
///
class Editor : public ValueBase
{
public:
   ///
   /// <summary>
   /// Initializes a new instance of the Editor class.
   /// </summary>
   /// <param name="format">Format used to render the value for display.</param>
   ///
   Editor(const Format& format)
      : ValueBase(format)
   {}

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

   bool isEditable() const override
   {
      return true;
   }
};

///
/// <summary>
/// Display-only field backed by a caller-owned float, for measured values (e.g. a live rate)
/// that need to share a table/alignment with editable fields but cannot be selected or adjusted.
/// </summary>
///
class FloatValue : public ValueBase
{
public:
   ///
   /// <summary>
   /// Initializes a new instance of the FloatValue class.
   /// </summary>
   /// <param name="value">Caller-owned variable that holds the current value.</param>
   /// <param name="format">Format used to render the value for display.</param>
   ///
   FloatValue(float* value, const Format& format)
      : ValueBase(format), _value(value)
   {}

   std::string valueText() override
   {
      return _format.toString((double)*_value);
   }

private:
   float* _value;
};

///
/// <summary>
/// Display-only field backed by a caller-owned std::string, for status/informational text
/// (e.g. connection state, topic, host) that needs to share a table/alignment with editable
/// fields but cannot be selected or adjusted.
/// </summary>
///
class StringValue : public ValueBase
{
public:
   ///
   /// <summary>
   /// Initializes a new instance of the StringValue class.
   /// </summary>
   /// <param name="value">Caller-owned variable that holds the current value.</param>
   /// <param name="format">Format used to render the value for display.</param>
   /// <param name="color">Optional caller-owned variable that overrides the value color (e.g. to
   /// reflect a status like connecting/connected/error). When omitted, the default value color
   /// is used.</param>
   ///
   StringValue(const std::string* value, const Format& format, const Color* color = nullptr)
      : ValueBase(format), _value(value), _color(color)
   {}

   std::string valueText() override
   {
      return _format.toString(*_value);
   }

   bool hasColor() const override
   {
      return _color != nullptr;
   }

   Color color() const override
   {
      return *_color;
   }

private:
   const std::string* _value;
   const Color* _color;
};

///
/// <summary>
/// Blank spacer row with no label or value, e.g. to push a following field down to a
/// different row than a field in an adjacent table it might otherwise visually overlap.
/// </summary>
///
class BlankValue : public ValueBase
{
public:
   BlankValue()
      : ValueBase(Format(size_t(0)))
   {}

   std::string valueText() override
   {
      return "";
   }

   ///
   /// <summary>
   /// Gets a shared BlankValue instance, since a spacer row carries no per-instance state.
   /// </summary>
   /// <returns>A shared BlankValue instance.</returns>
   ///
   static BlankValue& instance()
   {
      static BlankValue blank;
      return blank;
   }
};

///
/// <summary>
/// Integer setup field backed by a caller-owned long. Override _stepValue in a subclass to
/// implement non-linear stepping.
/// </summary>
///
class IntEditor : public Editor
{
public:
   ///
   /// <summary>
   /// Initializes a new instance of the IntEditor class.
   /// </summary>
   /// <param name="value">Caller-owned variable that holds the current value.</param>
   /// <param name="minValue">Minimum allowed value.</param>
   /// <param name="maxValue">Maximum allowed value.</param>
   /// <param name="step">Linear step size used by the default _stepValue implementation.</param>
   /// <param name="defaultValue">Default value used when no saved value exists or on reset.</param>
   /// <param name="format">Format used to render the value for display.</param>
   ///
   IntEditor(long* value,
      long minValue,
      long maxValue,
      long step,
      long defaultValue,
      const Format& format)
      : Editor(format),
      _value(value),
      _minValue(minValue),
      _maxValue(maxValue),
      _step(step),
      _default(defaultValue)
   {}

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
/// Boolean setup field backed by a caller-owned bool. Adjusting the field (in either
/// direction) toggles it, and valueText() displays "False"/"True" instead of a raw
/// number. Replaces the common pattern of writing a two-entry EnumEditor just to
/// toggle a boolean value.
/// </summary>
///
class BoolEditor : public Editor
{
public:
   ///
   /// <summary>
   /// Initializes a new instance of the BoolEditor class.
   /// </summary>
   /// <param name="value">Caller-owned variable that holds the current value.</param>
   /// <param name="defaultValue">Default value used when no saved value exists or on reset.</param>
   /// <param name="format">Format used to render the value for display.</param>
   ///
   BoolEditor(bool* value, bool defaultValue, const Format& format)
      : Editor(format), _value(value), _default(defaultValue)
   {}

   void reset() override
   {
      *_value = _default;
   }

   void adjust(int32_t direction) override
   {
      *_value = !*_value;
   }

   std::string valueText() override
   {
      return _format.toString(*_value ? "True" : "False");
   }

   double numericValue() const override
   {
      return *_value ? 1.0 : 0.0;
   }

   void setNumericValue(double value) override
   {
      *_value = (value != 0.0);
   }

   double defaultNumericValue() const override
   {
      return _default ? 1.0 : 0.0;
   }

private:
   bool* _value;
   bool _default;
};

///
/// <summary>
/// Enumerated-selection field backed by a caller-owned long index into a fixed array of
/// string labels. Adjusting the field steps through the labels by index, wrapping around at
/// either end, and valueText() displays the selected label instead of a raw index number.
/// Replaces the common pattern of writing a one-off IntEditor subclass just to
/// step through and display a small fixed set of string options (e.g. "Points"/"Lines",
/// "True"/"False", "Fixed"/"Timed").
/// </summary>
///
class EnumEditor : public IntEditor
{
public:
   ///
   /// <summary>
   /// Initializes a new instance of the EnumEditor class.
   /// </summary>
   /// <param name="value">Caller-owned variable that holds the current selected index.</param>
   /// <param name="labels">Span of label strings to step through and display.</param>
   /// <param name="defaultValue">Default index used when no saved value exists or on reset.</param>
   /// <param name="format">Format used to render the selected label for display.</param>
   ///
   EnumEditor(long* value,
      std::span<const char* const> labels,
      long defaultValue,
      const Format& format)
      : IntEditor(value, 0, (long)labels.size() - 1, 1, defaultValue, format),
      _labels(labels)
   {}

   void adjust(int32_t direction) override
   {
      long count = (long)_labels.size();
      long newValue = (*_value + (direction > 0 ? 1 : -1) + count) % count;
      *_value = newValue;
   }

   std::string valueText() override
   {
      long index = constrain(*_value, 0L, (long)(_labels.size() - 1));
      return _format.toString(_labels[index]);
   }

private:
   std::span<const char* const> _labels;
};

///
/// <summary>
/// Floating-point setup field backed by a caller-owned float. Override _stepValue in a subclass
/// to implement non-linear stepping.
/// </summary>
///
class FloatEditor : public Editor
{
public:
   ///
   /// <summary>
   /// Initializes a new instance of the FloatEditor class.
   /// </summary>
   /// <param name="value">Caller-owned variable that holds the current value.</param>
   /// <param name="minValue">Minimum allowed value.</param>
   /// <param name="maxValue">Maximum allowed value.</param>
   /// <param name="step">Linear step size used by the default _stepValue implementation.</param>
   /// <param name="defaultValue">Default value used when no saved value exists or on reset.</param>
   /// <param name="format">Format used to render the value for display.</param>
   ///
   FloatEditor(float* value,
      float minValue,
      float maxValue,
      float step,
      float defaultValue,
      const Format& format)
      : Editor(format),
      _value(value),
      _minValue(minValue),
      _maxValue(maxValue),
      _step(step),
      _default(defaultValue)
   {}

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

