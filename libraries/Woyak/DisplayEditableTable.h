#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "Color.h"
#include "ESP32_S3_Playground.h"
#include "DisplayEditableField.h"

///
/// <summary>
/// Reusable, embeddable table of DisplayEditableField values that can be navigated and adjusted live
/// with a board's encoders: Encoder A cycles the selected field, Encoder B adjusts its value.
/// Also supports loading/saving/resetting all fields against a Preferences namespace. Unlike
/// DisplayEditor, this class does not block and does not own a title/instructions area - it
/// simply tracks the selected field, persists values, and draws the label/value rows at the
/// position given to its constructor, so it can be embedded into any display layout (a live
/// monitoring screen, a dedicated setup screen, etc.).
/// </summary>
///
class DisplayEditableTable
{
private:
   ESP32_S3_Playground* _arduino;
   const char* _prefNamespace;
   DisplayEditableField** _fields;
   uint8_t _fieldCount;
   int16_t _x;
   int16_t _y;
   size_t _maxLabelLength = 0;
   uint8_t _selectedIndex = 0;
   char _keyBuffer[10];
   bool _showSections = true;

   ///
   /// <summary>
   /// Per-row cache of what was last drawn, so draw() can skip re-printing rows whose
   /// content hasn't changed since the previous call (e.g. labels, which never change while
   /// a test is running - only the measured values do).
   /// </summary>
   ///
   struct _RowCache
   {
      std::string valueText;
      bool selected = false;
      bool enabled = true;
   };
   _RowCache* _cache = nullptr;
   bool _forceRedraw = true;

   Preferences* _preferences()
   {
      return &_arduino->preferences;
   }

   ///
   /// <summary>
   /// Generates the Preferences key used to persist a field, derived from a hash of its label
   /// rather than its position in the table. This lets setFields() swap which fields are
   /// currently displayed (e.g. different config rows per selected test function) without
   /// different fields sharing - and clobbering - the same positional key.
   /// </summary>
   /// <param name="field">Field to compute a persistence key for.</param>
   /// <returns>A short, stable Preferences key.</returns>
   ///
   const char* _keyFor(DisplayEditableField* field)
   {
      // Simple FNV-1a hash of the label, truncated to fit Preferences' short key limit.
      uint32_t hash = 2166136261u;
      for (const char* p = field->label(); *p != '\0'; p++)
      {
         hash ^= static_cast<uint8_t>(*p);
         hash *= 16777619u;
      }
      snprintf(_keyBuffer, sizeof(_keyBuffer), "f%08lx", static_cast<unsigned long>(hash));
      return _keyBuffer;
   }

   ///
   /// <summary>
   /// Recomputes the fixed label column width and resets the selection to the first editable
   /// field. Shared by the constructor and setFields(). The label column width only ever
   /// grows, never shrinks, across setFields() calls, so the ":" column stays in the same
   /// position even when swapping in a field set with shorter labels.
   /// </summary>
   ///
   void _relayout()
   {
      for (uint8_t i = 0; i < _fieldCount; i++)
      {
         size_t labelLength = strlen(_fields[i]->label());
         if (labelLength > _maxLabelLength)
         {
            _maxLabelLength = labelLength;
         }
      }

         _selectedIndex = 0;
         for (uint8_t i = 0; i < _fieldCount; i++)
         {
            if (_fields[i]->isEditable())
            {
               _selectedIndex = i;
               break;
            }
         }

         delete[] _cache;
         _cache = new _RowCache[_fieldCount];
         _forceRedraw = true;
      }

   ///
   /// <summary>
   /// Computes the pixel Y offset (relative to _y) of a given row index, accounting for
   /// section header rows and the half-height blank row separating sections. A field starts
   /// a new section - and gets a header row (plus a separating half-row gap, unless it's the
   /// first field) drawn above it - whenever DisplayEditableField::section() is non-null.
   /// Passing _fieldCount computes the table's total pixel height.
   /// </summary>
   /// <param name="index">Zero-based field index, or _fieldCount for the total height.</param>
   /// <returns>Pixel offset from the table's Y position.</returns>
   ///
   int16_t _fieldY(uint8_t index) const
   {
      int16_t rowHeight = _arduino->charH();
      int16_t halfRow = rowHeight / 2;
      int16_t y = 0;
      for (uint8_t i = 0; i < index; i++)
      {
         if (_showSections && _fields[i]->section() != nullptr)
         {
            if (i > 0)
            {
               y += halfRow;
            }
            y += rowHeight;
         }
         y += rowHeight;
      }

      if (_showSections && index < _fieldCount && _fields[index]->section() != nullptr)
      {
         if (index > 0)
         {
            y += halfRow;
         }
         y += rowHeight;
      }
      return y;
   }

public:
   ///
   /// <summary>
   /// Initializes a new instance of the DisplayEditableTable class.
   /// </summary>
   /// <param name="arduino">Board providing the display, encoders, and preferences.</param>
   /// <param name="prefNamespace">Preferences namespace used to persist field values.</param>
   /// <param name="fields">Array of field pointers to display and edit.</param>
   /// <param name="fieldCount">Number of entries in fields.</param>
   /// <param name="x">Left X coordinate of the table.</param>
   /// <param name="y">Top Y coordinate of the table.</param>
   ///
   DisplayEditableTable(ESP32_S3_Playground* arduino, const char* prefNamespace, DisplayEditableField** fields,
      uint8_t fieldCount, int16_t x, int16_t y)
   {
      _arduino = arduino;
      _prefNamespace = prefNamespace;
      _fields = fields;
      _fieldCount = fieldCount;
         _x = x;
         _y = y;
         _relayout();
      }

      ///
      /// <summary>
      /// Releases the per-row draw cache.
      /// </summary>
      ///
      ~DisplayEditableTable()
      {
         delete[] _cache;
      }

   ///
   /// <summary>
   /// Replaces the set of fields this table displays/edits, e.g. to show different
   /// configuration rows depending on some other live selection (like an active test
   /// function). Recomputes label alignment and resets the selection to the first editable
   /// field. Erases the table's entire previous footprint so stale content - including
   /// section headers and their separating blank rows that may land on different rows than
   /// before - doesn't linger when the new field set has a different section layout, even if
   /// the row count is unchanged. Does not draw the remaining rows; call load() and draw()
   /// afterward to load the new fields' persisted values and refresh the display. Does not
   /// change the table's position; call setPosition() separately if needed.
   /// </summary>
   /// <param name="fields">Array of field pointers to display and edit.</param>
   /// <param name="fieldCount">Number of entries in fields.</param>
   ///
   void setFields(DisplayEditableField** fields, uint8_t fieldCount)
   {
      int16_t oldWidth = width();
      int16_t oldHeight = height();

      _fields = fields;
      _fieldCount = fieldCount;
      _relayout();

      int16_t clearWidth = max(oldWidth, width());
      int16_t clearHeight = max(oldHeight, height());
      _arduino->fillRect(_x, _y, clearWidth, clearHeight, Color::BLACK);
   }

   ///
   /// <summary>
   /// Updates the position rows are drawn at, e.g. when the table follows a variable-height
   /// title that is only known at runtime. Does nothing if the position is unchanged from the
   /// last call, so calling this every draw cycle with a recomputed-but-identical position
   /// doesn't force every row to be redrawn (see draw()'s per-row change tracking).
   /// </summary>
   /// <param name="x">Left X coordinate of the table.</param>
   /// <param name="y">Top Y coordinate of the table.</param>
   ///
   void setPosition(int16_t x, int16_t y)
   {
      if (x == _x && y == _y)
      {
         return;
      }

      _x = x;
      _y = y;
      _forceRedraw = true;
   }

   ///
   /// <summary>
   /// Forces every row (including section headers) to be fully redrawn on the next draw()
   /// call, even if their content hasn't changed, e.g. after the display was cleared by
   /// another view (like a scatter plot) so this table's cached content no longer matches
   /// what's actually on screen.
   /// </summary>
   ///
   void forceRedraw()
   {
      _forceRedraw = true;
   }

   ///
   /// <summary>
   /// Sets whether section header rows (see DisplayEditableField::section()) are drawn and
   /// reserved space for. Defaults to true. Useful when a table reuses field objects whose
   /// section was set for a different table (e.g. a scatter-plot view reusing main-screen
   /// fields) and shouldn't repeat that section header.
   /// </summary>
   /// <param name="showSections">True to draw section headers, false to suppress them.</param>
   ///
   void setShowSections(bool showSections)
   {
      _showSections = showSections;
      _forceRedraw = true;
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
      if (index < _fieldCount && _fields[index]->isEditable())
      {
         _selectedIndex = index;
      }
   }

   ///
   /// <summary>
   /// Moves the selection by one or more steps, skipping fields that aren't editable or are
   /// currently disabled, and wrapping around at either end. Does nothing if direction is
   /// zero. The caller is responsible for reading its own encoder and calling draw()
   /// afterward if a redraw is needed.
   /// </summary>
   /// <param name="direction">Signed number of steps to move the selection (e.g. from Encoder A). Zero means no change.</param>
   ///
   void selectNext(int32_t direction)
   {
      if (direction == 0)
      {
         return;
      }

      int32_t step = direction > 0 ? 1 : -1;
      int32_t newIndex = static_cast<int32_t>(_selectedIndex);
      for (uint8_t i = 0; i < _fieldCount; i++)
      {
         newIndex = (newIndex + step + _fieldCount) % _fieldCount;
         if (_fields[newIndex]->isEditable() && _fields[newIndex]->isEnabled())
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
   /// The caller is responsible for reading its own encoder and calling draw() afterward if
   /// a redraw is needed.
   /// </summary>
   /// <param name="direction">Signed number of steps to adjust the selected field's value (e.g. from Encoder B). Zero means no change.</param>
   ///
   void adjustSelected(int32_t direction)
   {
      if (direction == 0)
      {
         return;
      }

         if (_fields[_selectedIndex]->isEditable() && _fields[_selectedIndex]->isEnabled())
         {
            _fields[_selectedIndex]->adjust(direction);
         }
      }

      ///
   /// <summary>
   /// Draws the label/value rows at the position given to the constructor, highlighting the
   /// currently selected field's value with a colored background. Labels are right-aligned and
   /// values are left-aligned, both anchored to the ":" column so every row lines up regardless
   /// of individual label/value lengths. The label column width is fixed, sized to the longest
   /// field label. The caller is responsible for clearing/managing the surrounding display area
   /// and for setting the desired text size beforehand. Section headers and labels are only
   /// (re)drawn once, or after a layout change; each row's value is only redrawn when its text,
   /// selection, or enabled state actually changed since the previous call, so repeatedly
   /// calling draw() while a test is running - where labels never change and only measured
   /// values do - only repaints the handful of rows that actually changed.
   /// </summary>
   ///
   void draw()
   {
      int16_t rowHeight = _arduino->charH();
      int16_t charWidth = _arduino->charW();
      int16_t colonX = _x + (int16_t)_maxLabelLength * charWidth;
      for (uint8_t i = 0; i < _fieldCount; i++)
      {
         int16_t rowY = _y + _fieldY(i);

         bool isSelected = _fields[i]->isEditable() && _fields[i]->isEnabled() && (i == _selectedIndex);
         bool isDisabled = !_fields[i]->isEnabled();
         std::string valueText = _fields[i]->valueText();

         bool labelNeedsDraw = _forceRedraw || (isDisabled != !_cache[i].enabled);
         bool valueNeedsDraw = _forceRedraw || (valueText != _cache[i].valueText) ||
            (isSelected != _cache[i].selected) || (isDisabled != !_cache[i].enabled);

         if (_showSections && _forceRedraw && _fields[i]->section() != nullptr)
         {
            int16_t headerY = rowY - rowHeight;
            _arduino->setCursor(_x, headerY);
            _arduino->println(_fields[i]->section(), Color::SUB_HEADING, Color::BLACK);
         }

         bool isBlankRow = _fields[i]->label()[0] == '\0';

         Color labelColor = isDisabled ? Color::GRAY : Color::LABEL;
         if (labelNeedsDraw && !isBlankRow)
         {
            String labelText = _fields[i]->label();
            while (labelText.length() < _maxLabelLength)
            {
               labelText = " " + labelText;
            }
            labelText += ": ";

            _arduino->setCursor(_x, rowY);
            _arduino->print(labelText, labelColor, Color::BLACK);
         }

         if (valueNeedsDraw && !isBlankRow)
         {
            Color valueBackgroundColor = isSelected ? Color::BLUE : Color::BLACK;
            Color valueTextColor = isDisabled ? Color::GRAY : (isSelected ? Color::WHITE : (_fields[i]->isEditable() ? Color::VALUE : Color::VALUE2));
            _arduino->setCursor(colonX + 2 * charWidth, rowY);
            _arduino->println(valueText, valueTextColor, valueBackgroundColor);
         }

         _cache[i].valueText = valueText;
         _cache[i].selected = isSelected;
         _cache[i].enabled = !isDisabled;
      }

      _forceRedraw = false;
   }

   ///
   /// <summary>
   /// Gets the total pixel height of all rows (including section headers and their
   /// separating half-height blank rows) at the current text size, useful for laying out
   /// content below the table.
   /// </summary>
   /// <returns>Total height in pixels.</returns>
   ///
   int16_t height() const
   {
      return _fieldY(_fieldCount);
   }

   ///
   /// <summary>
   /// Gets the total pixel width the table needs to display its widest label/value row,
   /// based on the fixed label column width and the widest current value across all
   /// fields. Useful for laying out other content (e.g. a plot) relative to the table's
   /// actual content width rather than a guessed fixed width.
   /// </summary>
   /// <returns>The required width, in pixels.</returns>
   ///
   int16_t width()
   {
      size_t maxValueLength = 0;
      for (uint8_t i = 0; i < _fieldCount; i++)
      {
         size_t valueLength = _fields[i]->valueText().length();
         if (valueLength > maxValueLength)
         {
            maxValueLength = valueLength;
         }
      }

      int16_t charWidth = _arduino->charW();
      return (int16_t)(_maxLabelLength + 2) * charWidth + (int16_t)maxValueLength * charWidth;
   }

   ///
   /// <summary>
   /// Gets the underlying field array, e.g. for loading/saving/resetting from Preferences.
   /// </summary>
   /// <returns>The field pointer array passed to the constructor.</returns>
   ///
   DisplayEditableField** fields() const
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
         if (!_fields[i]->isEditable())
         {
            continue;
         }
         double defaultValue = _fields[i]->defaultNumericValue();
         double value = prefs->getDouble(_keyFor(_fields[i]), defaultValue);
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
         if (!_fields[i]->isEditable())
         {
            continue;
         }
         prefs->putDouble(_keyFor(_fields[i]), _fields[i]->numericValue());
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
         if (!_fields[i]->isEditable())
         {
            continue;
         }
         _fields[i]->reset();
      }
      save();
   }
};
