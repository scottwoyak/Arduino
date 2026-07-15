#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "Format.h"
#include "Color.h"
#include "ESP32_S3_Playground.h"
#include "SetupField.h"

///
/// <summary>
/// Reusable, embeddable table of SetupField values that can be navigated and adjusted live
/// with a board's encoders: Encoder A cycles the selected field, Encoder B adjusts its value.
/// Also supports loading/saving/resetting all fields against a Preferences namespace. Unlike
/// SetupDisplay, this class does not block and does not own a title/instructions area - it
/// simply tracks the selected field, persists values, and draws the label/value rows at a
/// caller-specified position, so it can be embedded into any display layout (a live monitoring
/// screen, a dedicated setup screen, etc.).
/// </summary>
///
class SetupTable
{
public:
   ///
   /// <summary>
   /// Initializes a new instance of the SetupTable class.
   /// </summary>
   /// <param name="arduino">Board providing the display, encoders, and preferences.</param>
   /// <param name="prefNamespace">Preferences namespace used to persist field values.</param>
   /// <param name="fields">Array of field pointers to display and edit.</param>
   /// <param name="fieldCount">Number of entries in fields.</param>
   ///
   SetupTable(ESP32_S3_Playground* arduino, const char* prefNamespace, SetupField** fields, uint8_t fieldCount)
      : _arduino(arduino), _prefNamespace(prefNamespace), _fields(fields), _fieldCount(fieldCount)
   {
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
      _selectedIndex = (index < _fieldCount) ? index : 0;
   }

   ///
   /// <summary>
   /// Checks the board's encoders for changes since the last call, advancing the selected
   /// field (Encoder A) or adjusting its value (Encoder B) as needed.
   /// </summary>
   /// <returns>True if the selection or a field's value changed; false otherwise.</returns>
   ///
   bool poll()
   {
      bool changed = false;

      int32_t deltaA = _arduino->encoderA.delta();
      if (deltaA != 0)
      {
         int32_t newIndex = static_cast<int32_t>(_selectedIndex) + (deltaA > 0 ? 1 : -1);
         newIndex = (newIndex + _fieldCount) % _fieldCount;
         _selectedIndex = static_cast<uint8_t>(newIndex);
         changed = true;
      }

      int32_t deltaB = _arduino->encoderB.delta();
      if (deltaB != 0)
      {
         _fields[_selectedIndex]->adjust(deltaB);
         changed = true;
      }

      return changed;
   }

   ///
   /// <summary>
   /// Draws the label/value rows at the given position, highlighting the currently selected
   /// field's value with a colored background. The caller is responsible for clearing/managing
   /// the surrounding display area and for setting the desired text size beforehand.
   /// </summary>
   /// <param name="x">Left X coordinate to start each row at.</param>
   /// <param name="y">Top Y coordinate of the first row.</param>
   /// <param name="minLabelWidth">
   /// Minimum label column width in characters, used to align the ":" with other content drawn
   /// outside this table (e.g. readout rows below it). Defaults to 0, which sizes the column to
   /// the longest field label.
   /// </param>
   ///
   void draw(int16_t x, int16_t y, size_t minLabelWidth = 0)
   {
      size_t maxLabelLength = minLabelWidth;
      for (uint8_t i = 0; i < _fieldCount; i++)
      {
         size_t labelLength = strlen(_fields[i]->label());
         if (labelLength > maxLabelLength)
         {
            maxLabelLength = labelLength;
         }
      }

      int16_t rowHeight = _arduino->charH();
      for (uint8_t i = 0; i < _fieldCount; i++)
      {
         String labelText = _fields[i]->label();
         while (labelText.length() < maxLabelLength)
         {
            labelText = " " + labelText;
         }
         labelText += ": ";

         bool isSelected = (i == _selectedIndex);
         Color valueBackgroundColor = isSelected ? Color::BLUE : Color::BLACK;
         Color valueTextColor = isSelected ? Color::WHITE : Color::VALUE;
         _arduino->setCursor(x, y + rowHeight * i);
         _arduino->print(labelText, Color::LABEL, Color::BLACK);
         _arduino->println(_fields[i]->valueText(), valueTextColor, valueBackgroundColor);
      }
   }

   ///
   /// <summary>
   /// Gets the total pixel height of all rows at the current text size, useful for laying out
   /// content below the table.
   /// </summary>
   /// <returns>Total height in pixels.</returns>
   ///
   int16_t height() const
   {
      return _arduino->charH() * (int16_t)_fieldCount;
   }

   ///
   /// <summary>
   /// Gets the underlying field array, e.g. for loading/saving/resetting from Preferences.
   /// </summary>
   /// <returns>The field pointer array passed to the constructor.</returns>
   ///
   SetupField** fields() const
   {
      return _fields;
   }

   ///
   /// <summary>
   /// Gets the number of fields in this table.
   /// </summary>
   /// <returns>Field count.</returns>
   ///
   uint8_t fieldCount() const
   {
      return _fieldCount;
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
         double defaultValue = _fields[i]->defaultNumericValue();
         double value = prefs->getDouble(_keyFor(i), defaultValue);
         _fields[i]->setNumericValue(value);
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
         prefs->putDouble(_keyFor(i), _fields[i]->numericValue());
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

private:
   ESP32_S3_Playground* _arduino;
   const char* _prefNamespace;
   SetupField** _fields;
   uint8_t _fieldCount;
   uint8_t _selectedIndex = 0;

   Preferences* _preferences()
   {
      return &_arduino->preferences;
   }

   ///
   /// <summary>
   /// Generates the Preferences key used to persist the field at the given index, based solely
   /// on its position in the table (fields no longer own their own keys).
   /// </summary>
   /// <param name="index">Zero-based field index.</param>
   /// <returns>A short, stable Preferences key.</returns>
   ///
   const char* _keyFor(uint8_t index)
   {
      snprintf(_keyBuffer, sizeof(_keyBuffer), "f%u", index);
      return _keyBuffer;
   }

   char _keyBuffer[8];
};
