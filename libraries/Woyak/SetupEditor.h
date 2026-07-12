#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "Format.h"
#include "Color.h"
#include "ESP32_S3_Playground.h"

///
/// <summary>
/// Abstract base for one editable setup value shown by SetupEditor. Subclasses bind to a
/// caller-owned variable and implement type-specific persistence, adjustment, and display.
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
   /// <param name="key">Preferences key used to persist this field's value.</param>
   /// <param name="format">Format used to render the value for display.</param>
   ///
   SetupField(const char* label, const char* key, const Format& format)
      : _label(label), _key(key), _format(format)
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
   /// Loads this field's value from Preferences, falling back to its default when absent.
   /// </summary>
   /// <param name="prefs">Preferences instance opened for reading.</param>
   ///
   virtual void load(Preferences* prefs) = 0;

   ///
   /// <summary>
   /// Persists this field's current value to Preferences.
   /// </summary>
   /// <param name="prefs">Preferences instance opened for writing.</param>
   ///
   virtual void save(Preferences* prefs) = 0;

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
   virtual String valueText() = 0;

protected:
   const char* _label;
   const char* _key;
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
   /// <param name="key">Preferences key used to persist this field's value.</param>
   /// <param name="value">Caller-owned variable that holds the current value.</param>
   /// <param name="minValue">Minimum allowed value.</param>
   /// <param name="maxValue">Maximum allowed value.</param>
   /// <param name="step">Linear step size used by the default _stepValue implementation.</param>
   /// <param name="defaultValue">Default value used when no saved value exists or on reset.</param>
   /// <param name="format">Format used to render the value for display.</param>
   ///
   IntSetupField(const char* label, const char* key, long* value,
                 long minValue, long maxValue, long step, long defaultValue,
                 const Format& format)
      : SetupField(label, key, format),
        _value(value), _minValue(minValue), _maxValue(maxValue), _step(step), _default(defaultValue)
   {
   }

   void load(Preferences* prefs) override
   {
      *_value = prefs->getLong(_key, _default);
   }

   void save(Preferences* prefs) override
   {
      prefs->putLong(_key, *_value);
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

   String valueText() override
   {
      return String(_format.toString((double)*_value).c_str());
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
   /// <param name="key">Preferences key used to persist this field's value.</param>
   /// <param name="value">Caller-owned variable that holds the current value.</param>
   /// <param name="minValue">Minimum allowed value.</param>
   /// <param name="maxValue">Maximum allowed value.</param>
   /// <param name="step">Linear step size used by the default _stepValue implementation.</param>
   /// <param name="defaultValue">Default value used when no saved value exists or on reset.</param>
   /// <param name="format">Format used to render the value for display.</param>
   ///
   FloatSetupField(const char* label, const char* key, float* value,
                    float minValue, float maxValue, float step, float defaultValue,
                    const Format& format)
      : SetupField(label, key, format),
        _value(value), _minValue(minValue), _maxValue(maxValue), _step(step), _default(defaultValue)
   {
   }

   void load(Preferences* prefs) override
   {
      *_value = prefs->getFloat(_key, _default);
   }

   void save(Preferences* prefs) override
   {
      prefs->putFloat(_key, *_value);
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

   String valueText() override
   {
      return String(_format.toString((double)*_value).c_str());
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

///
/// <summary>
/// Interactive editor that drives a list of SetupField objects using a board's encoders and
/// buttons. Encoder A cycles the selected field, Encoder B adjusts its value, Button B resets
/// all fields to their defaults, and Button A confirms and saves.
/// </summary>
///
class SetupEditor
{
public:
   ///
   /// <summary>
   /// Initializes a new instance of the SetupEditor class.
   /// </summary>
   /// <param name="arduino">Board providing the display, encoders, buttons, and preferences.</param>
   /// <param name="prefNamespace">Preferences namespace used to persist field values.</param>
   /// <param name="title">Heading text drawn at the top of the setup screen.</param>
   /// <param name="fields">Array of field pointers to display and edit.</param>
   /// <param name="fieldCount">Number of entries in fields.</param>
   ///
   SetupEditor(ESP32_S3_Playground* arduino, const char* prefNamespace,
               const char* title, SetupField** fields, uint8_t fieldCount)
      : _arduino(arduino), _prefNamespace(prefNamespace), _title(title),
        _fields(fields), _fieldCount(fieldCount)
   {
   }

   ///
   /// <summary>
   /// Loads all field values from Preferences, falling back to defaults for missing entries.
   /// </summary>
   ///
   void load()
   {
      Preferences* prefs = _preferences();
      prefs->begin(_prefNamespace, true);
      for (uint8_t i = 0; i < _fieldCount; i++)
      {
         _fields[i]->load(prefs);
      }
      prefs->end();
   }

   ///
   /// <summary>
   /// Persists all field values to Preferences.
   /// </summary>
   ///
   void save()
   {
      Preferences* prefs = _preferences();
      prefs->begin(_prefNamespace, false);
      for (uint8_t i = 0; i < _fieldCount; i++)
      {
         _fields[i]->save(prefs);
      }
      prefs->end();
   }

   ///
   /// <summary>
   /// Restores all fields to their defaults and persists the result.
   /// </summary>
   ///
   void reset()
   {
      for (uint8_t i = 0; i < _fieldCount; i++)
      {
         _fields[i]->reset();
      }
      save();
   }

   ///
   /// <summary>
   /// Runs the blocking edit loop: Encoder A selects a field, Encoder B adjusts it, Button B
   /// resets to defaults, and Button A saves and returns.
   /// </summary>
   ///
   void run()
   {
      _viewInitialized = false;
      _draw();

      while (true)
      {
         int32_t deltaA = _arduino->encoderA.delta();
         if (deltaA != 0)
         {
            int32_t newIndex = static_cast<int32_t>(_selectedIndex) + (deltaA > 0 ? 1 : -1);
            newIndex = (newIndex + _fieldCount) % _fieldCount;
            _selectedIndex = static_cast<uint8_t>(newIndex);
            _draw();
         }

         int32_t deltaB = _arduino->encoderB.delta();
         if (deltaB != 0)
         {
            _fields[_selectedIndex]->adjust(deltaB);
            _draw();
         }

         if (_arduino->buttonB.wasPressed())
         {
            reset();
            _draw();
         }

         if (_arduino->buttonA.wasPressed())
         {
            save();
            return;
         }
      }
   }

private:
   ESP32_S3_Playground* _arduino;
   const char* _prefNamespace;
   const char* _title;
   SetupField** _fields;
   uint8_t _fieldCount;
   uint8_t _selectedIndex = 0;
   bool _viewInitialized = false;

   Preferences* _preferences()
   {
      return &_arduino->preferences;
   }

   ///
   /// <summary>
   /// Draws the setup screen, highlighting the currently selected field. Only clears the
   /// display on the first draw of a run() call to avoid flicker on subsequent redraws.
   /// </summary>
   ///
   void _draw()
   {
      if (!_viewInitialized)
      {
         _arduino->clearDisplay();
         _viewInitialized = true;
      }

      _arduino->setCursor(0, 0);
      _arduino->setTextSize(3);
      _arduino->println(_title, Color::HEADING);

      _arduino->setTextSize(2);

      size_t maxLabelLength = 0;
      for (uint8_t i = 0; i < _fieldCount; i++)
      {
         size_t labelLength = strlen(_fields[i]->label());
         if (labelLength > maxLabelLength)
         {
            maxLabelLength = labelLength;
         }
      }

      for (uint8_t i = 0; i < _fieldCount; i++)
      {
         String labelText = _fields[i]->label();
         while (labelText.length() < maxLabelLength)
         {
            labelText = " " + labelText;
         }
         labelText += ": ";

         Color valueBackgroundColor = (i == _selectedIndex) ? Color::BLUE : Color::BLACK;
         _arduino->print(labelText, Color::LABEL, Color::BLACK);
         _arduino->println(_fields[i]->valueText(), Color::VALUE, valueBackgroundColor);
      }

      _arduino->println();
      _arduino->println("Enc A: Select", Color::VALUE);
      _arduino->println("Enc B: Adjust", Color::VALUE);
      _arduino->println("Button A: Confirm", Color::VALUE);
      _arduino->println("Button B: Reset", Color::VALUE);
   }
};
