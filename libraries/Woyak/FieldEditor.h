#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <span>
#include "Color.h"
#include "ArduinoBoard.h"
#include "ValueEditor.h"

#ifndef ARDUINO_PREFERENCES_SUPPORTED
#error "FieldEditor requires a board with Preferences support."
#endif

///
/// <summary>
/// Reusable, render-agnostic collection of ValueBase values (a mix of editable Editor
/// fields and read-only values) that can be navigated and adjusted live with a board's
/// encoders: Encoder A cycles the selected field, Encoder B adjusts its value. Also
/// supports loading/saving/resetting all fields against a Preferences namespace.
/// FieldEditor has no rendering/layout logic and no notion of rows, tables, or sections -
/// it just manages a flat set of named fields and makes them selectable/editable, so it
/// can back any presentation (a table, a label drawn next to some other content, etc.).
/// FieldTableEditor is built on top of this class, adding FieldTable-based table rendering
/// (including its own row-level concepts like section headers).
/// </summary>
///
class FieldEditor
{
public:
   ///
   /// <summary>
   /// Pairs a ValueBase (or Editor) with the name used to identify it, e.g. for
   /// Preferences persistence. The name isn't part of the value's own value/editing
   /// behavior, so it lives here instead of on the value itself.
   /// </summary>
   ///
   struct FieldInfo
   {
      // Default-constructible so Vector<FieldInfo> can grow its backing array (see
      // Vector::_ensureCapacity()); a default-constructed FieldInfo has no name/value and is
      // never actually used as-is.
      FieldInfo() = default;

      ///
      /// <summary>
      /// Initializes a new instance of the FieldInfo struct.
      /// </summary>
      /// <param name="name">Name identifying this field, e.g. "Rate", used to derive its Preferences key.</param>
      /// <param name="value">The value providing this field's value/editing behavior.</param>
      ///
      FieldInfo(const char* name, ValueBase* value)
         : name(name), value(value)
      {}

      const char* name = nullptr;
      ValueBase* value = nullptr;
   };

private:
   Arduino* _arduino;
   const char* _prefNamespace;
   std::span<FieldInfo> _fields;
   uint8_t _selectedIndex = 0;
   char _keyBuffer[10];

   Preferences* _preferences()
   {
      return &_arduino->preferences;
   }

   ///
   /// <summary>
   /// Generates the Preferences key used to persist a field, derived from a hash of its name
   /// rather than its position in the collection. This lets setFields() swap which fields are
   /// currently tracked (e.g. different config fields per selected test function) without
   /// different fields sharing - and clobbering - the same positional key.
   /// </summary>
   /// <param name="field">Field to compute a persistence key for.</param>
   /// <returns>A short, stable Preferences key.</returns>
   ///
   const char* _keyFor(const FieldInfo& field)
   {
      // Simple FNV-1a hash of the name, truncated to fit Preferences' short key limit.
      uint32_t hash = 2166136261u;
      for (const char* p = field.name; *p != '\0'; p++)
      {
         hash ^= static_cast<uint8_t>(*p);
         hash *= 16777619u;
      }
      snprintf(_keyBuffer, sizeof(_keyBuffer), "f%08lx", static_cast<unsigned long>(hash));
      return _keyBuffer;
   }

   ///
   /// <summary>
   /// Resets the selection to the first editable field. Shared by the constructor and
   /// setFields().
   /// </summary>
   ///
   void _selectFirstEditable()
   {
      _selectedIndex = 0;
      for (uint8_t i = 0; i < _fields.size(); i++)
      {
         if (_fields[i].value != nullptr && _fields[i].value->isEditable())
         {
            _selectedIndex = i;
            break;
         }
      }
   }

public:
   ///
   /// <summary>
   /// Initializes a new instance of the FieldEditor class.
   /// </summary>
   /// <param name="arduino">Board providing the encoders and preferences.</param>
   /// <param name="prefNamespace">Preferences namespace used to persist field values.</param>
   /// <param name="fields">Span of fields to edit.</param>
   ///
   FieldEditor(Arduino* arduino, const char* prefNamespace, std::span<FieldInfo> fields)
      : _arduino(arduino), _prefNamespace(prefNamespace), _fields(fields)
   {
      _selectFirstEditable();
   }

   ///
   /// <summary>
   /// Replaces the set of fields this editor tracks, e.g. to switch between different
   /// configuration fields depending on some other live selection (like an active test
   /// function). Resets the selection to the first editable field. Does not load the new
   /// fields' persisted values; call load() afterward if needed.
   /// </summary>
   /// <param name="fields">Span of fields to edit.</param>
   ///
   void setFields(std::span<FieldInfo> fields)
   {
      _fields = fields;
      _selectFirstEditable();
   }

   ///
   /// <summary>
   /// Gets the index of the currently selected field.
   /// </summary>
   /// <returns>Zero-based index of the selected field.</returns>
   ///
   uint8_t selectedIndex() const
   {
      return _selectedIndex;
   }

   ///
   /// <summary>
   /// Sets the currently selected field index, clamped to a valid range.
   /// </summary>
   /// <param name="index">Zero-based index of the field to select.</param>
   ///
   void setSelectedIndex(uint8_t index)
   {
      if (index < _fields.size() && _fields[index].value != nullptr && _fields[index].value->isEditable())
      {
         _selectedIndex = index;
      }
   }

   ///
   /// <summary>
   /// Moves the selection by one or more steps, skipping fields that aren't editable or are
   /// currently disabled, and wrapping around at either end. Does nothing if direction is
   /// zero. The caller is responsible for reading its own encoder and redrawing afterward if
   /// needed.
   /// </summary>
   /// <param name="direction">Signed number of steps to move the selection (e.g. from Encoder A). Zero means no change.</param>
   ///
   void selectNext(int32_t direction)
   {
      if (direction == 0)
      {
         return;
      }

      int32_t fieldCount = static_cast<int32_t>(_fields.size());
      int32_t step = direction > 0 ? 1 : -1;
      int32_t newIndex = static_cast<int32_t>(_selectedIndex);
      for (uint8_t i = 0; i < _fields.size(); i++)
      {
         newIndex = (newIndex + step + fieldCount) % fieldCount;
         if (_fields[newIndex].value != nullptr && _fields[newIndex].value->isEditable() && _fields[newIndex].value->isEnabled())
         {
            break;
         }
      }
      _selectedIndex = static_cast<uint8_t>(newIndex);
   }

   ///
   /// <summary>
   /// Adjusts the currently selected field's value by one or more steps, if it is editable
   /// and enabled. Does nothing if direction is zero or the selected field can't be adjusted.
   /// The caller is responsible for reading its own encoder and redrawing afterward if
   /// needed.
   /// </summary>
   /// <param name="direction">Signed number of steps to adjust the selected field's value (e.g. from Encoder B). Zero means no change.</param>
   ///
   void adjustSelected(int32_t direction)
   {
      if (direction == 0)
      {
         return;
      }

      ValueBase* value = _fields[_selectedIndex].value;
      if (value->isEditable() && value->isEnabled())
      {
         static_cast<Editor*>(value)->adjust(direction);
      }
   }

   ///
   /// <summary>
   /// Computes the text/background colors a caller's own rendering should use for the field
   /// at the given index, based on its current selection/enabled state (selected fields are
   /// highlighted, disabled fields are grayed out, read-only fields use a dimmer value
   /// color). The same logic FieldTableEditor uses to highlight a selected row, extracted
   /// here so any caller with its own rendering (e.g. an inline field layout) can reuse it
   /// instead of duplicating it.
   /// </summary>
   /// <param name="index">Index of the field to compute display colors for.</param>
   /// <param name="valueColor">Receives the color the value's text should be drawn in.</param>
   /// <param name="backgroundColor">Receives the color to draw behind the value's text.</param>
   ///
   void colorsFor(uint8_t index, Color& valueColor, Color& backgroundColor) const
   {
      ValueBase* value = _fields[index].value;
      bool isSelected = value->isEditable() && value->isEnabled() && (index == _selectedIndex);
      bool isDisabled = !value->isEnabled();

      backgroundColor = isSelected ? Color::BLUE : Color::BLACK;
      valueColor = isDisabled ? Color::GRAY : (isSelected ? Color::WHITE : (value->hasColor() ? value->color() : (value->isEditable() ? Color::VALUE : Color::VALUE2)));
   }

   ///
   /// <summary>
   /// Gets the underlying field span, e.g. for a caller's own rendering.
   /// </summary>
   /// <returns>The field span currently in use.</returns>
   ///
   std::span<FieldInfo> fields() const
   {
      return _fields;
   }

   ///
   /// <summary>
   /// Gets the number of fields tracked by this editor.
   /// </summary>
   /// <returns>Field count.</returns>
   ///
   uint8_t fieldCount() const
   {
      return static_cast<uint8_t>(_fields.size());
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
      for (uint8_t i = 0; i < _fields.size(); i++)
      {
         if (_fields[i].value == nullptr || !_fields[i].value->isEditable())
         {
            continue;
         }
         Editor* field = static_cast<Editor*>(_fields[i].value);
         double defaultValue = field->defaultNumericValue();
         double value = prefs->getDouble(_keyFor(_fields[i]), defaultValue);
         field->setNumericValue(value, /* markAsChanged */ false);
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
      for (uint8_t i = 0; i < _fields.size(); i++)
      {
         if (_fields[i].value == nullptr || !_fields[i].value->isEditable())
         {
            continue;
         }
         Editor* field = static_cast<Editor*>(_fields[i].value);
         prefs->putDouble(_keyFor(_fields[i]), field->numericValue());
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
      for (uint8_t i = 0; i < _fields.size(); i++)
      {
         if (_fields[i].value == nullptr || !_fields[i].value->isEditable())
         {
            continue;
         }
         static_cast<Editor*>(_fields[i].value)->reset();
      }
      save();
   }

#ifdef ARDUINO_PLAYGROUND_SUPPORTED
   ///
   /// <summary>
   /// Drives the editor from the board's own encoders in a single call: Encoder A moves the
   /// selection (selectNext()), Encoder B adjusts the selected field's value
   /// (adjustSelected()) and persists the change, and Encoder B's integral button resets all
   /// fields to their defaults (reset(), which also persists). Unlike
   /// FieldTableEditor::loop(), this does not draw anything - FieldEditor has no
   /// rendering/layout logic - so the caller is responsible for redrawing its own
   /// presentation of the fields when this returns true. Requires a board with Encoder A/B
   /// (see ARDUINO_PLAYGROUND_SUPPORTED); use selectNext()/adjustSelected()/reset() directly
   /// if finer control is needed (e.g. reacting to a change before it's applied).
   /// </summary>
   /// <returns>True if the selection changed, the selected value was adjusted, or the fields were reset; false otherwise.</returns>
   ///
   bool loop()
   {
      uint8_t previousIndex = _selectedIndex;
      selectNext(_arduino->encoderA.delta());
      bool changed = _selectedIndex != previousIndex;

      int32_t adjustDelta = _arduino->encoderB.delta();
      if (adjustDelta != 0)
      {
         adjustSelected(adjustDelta);
         save();
         changed = true;
      }

      if (_arduino->encoderB.button.wasPressed())
      {
         reset();
         changed = true;
      }

      return changed;
   }
#endif
};
