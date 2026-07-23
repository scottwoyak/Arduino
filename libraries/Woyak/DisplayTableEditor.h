#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "Color.h"
#include "ESP32_S3_Playground.h"
#include "DisplayTableCellEditor.h"
#include "DisplayTable.h"

///
/// <summary>
/// Reusable, embeddable table of DisplayTableCellEditor values that can be navigated and adjusted live
/// with a board's encoders: Encoder A cycles the selected field, Encoder B adjusts its value.
/// Also supports loading/saving/resetting all fields against a Preferences namespace. This class owns
/// no rendering logic of its own - it delegates all drawing to an internal DisplayTable, so drawing
/// fixes (sprite creation, text-size restoration, flicker avoidance, layout math, etc.) only need to
/// live in one place. DisplayTableEditor simply tracks the selected field, persists values, and pushes
/// each field's current text/color/section into the internal DisplayTable before drawing it.
/// </summary>
///
class DisplayTableEditor
{
private:
   ESP32_S3_Playground* _arduino;
   const char* _prefNamespace;
   DisplayTableCellEditor** _fields;
   uint8_t _fieldCount;
   uint8_t _selectedIndex = 0;
   char _keyBuffer[10];
   DisplayTable _table;

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
   const char* _keyFor(DisplayTableCellEditor* field)
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
   /// Rebuilds the internal DisplayTable's rows from the current field array and resets the
   /// selection to the first editable field. Shared by the constructor and setFields().
   /// </summary>
   ///
   void _relayout()
   {
      for (uint8_t i = 0; i < _fieldCount; i++)
      {
         _table.addRow(_fields[i]->label(), _fields[i]->format());
         _table.setSection(i, _fields[i]->section());
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
   }

   ///
   /// <summary>
   /// Pushes every field's current value/colors into the internal DisplayTable, reflecting
   /// selection highlighting and disabled/enabled state. Called by draw() before delegating
   /// to the table.
   /// </summary>
   ///
   void _syncTable()
   {
      for (uint8_t i = 0; i < _fieldCount; i++)
      {
         bool isSelected = _fields[i]->isEditable() && _fields[i]->isEnabled() && (i == _selectedIndex);
         bool isDisabled = !_fields[i]->isEnabled();

         Color labelColor = isDisabled ? Color::GRAY : Color::LABEL;
         Color valueBackgroundColor = isSelected ? Color::BLUE : Color::BLACK;
         Color valueColor = isDisabled ? Color::GRAY : (isSelected ? Color::WHITE : (_fields[i]->isEditable() ? Color::VALUE : Color::VALUE2));

         _table.setLabelColor(i, labelColor);
         _table.setValue(i, _fields[i]->valueText().c_str(), valueColor);
         _table.setValueBackgroundColor(i, valueBackgroundColor);
      }
   }

public:
   ///
   /// <summary>
   /// Initializes a new instance of the DisplayTableEditor class.
   /// </summary>
   /// <param name="arduino">Board providing the display, encoders, and preferences.</param>
   /// <param name="prefNamespace">Preferences namespace used to persist field values.</param>
   /// <param name="fields">Array of field pointers to display and edit.</param>
   /// <param name="fieldCount">Number of entries in fields.</param>
   /// <param name="x">Left X coordinate of the table.</param>
   /// <param name="y">Top Y coordinate of the table.</param>
   /// <param name="textSize">The text size applied automatically before drawing labels and values.</param>
   /// <param name="mono">If true, uses a monospaced font; if false, uses a proportional font.</param>
   ///
   DisplayTableEditor(ESP32_S3_Playground* arduino, const char* prefNamespace, DisplayTableCellEditor** fields,
      uint8_t fieldCount, int16_t x, int16_t y, uint8_t textSize = 2, bool mono = true)
      : _arduino(arduino), _prefNamespace(prefNamespace), _fields(fields), _fieldCount(fieldCount),
      _table(arduino, x, y, textSize, mono)
   {
      _relayout();
   }

   ///
   /// <summary>
   /// Replaces the set of fields this table displays/edits, e.g. to show different
   /// configuration rows depending on some other live selection (like an active test
   /// function). Recomputes label alignment and resets the selection to the first editable
   /// field. Does not draw the remaining rows; call load() and draw() afterward to load the
   /// new fields' persisted values and refresh the display. Does not change the table's
   /// position; call setPosition() separately if needed.
   /// </summary>
   /// <param name="fields">Array of field pointers to display and edit.</param>
   /// <param name="fieldCount">Number of entries in fields.</param>
   ///
   void setFields(DisplayTableCellEditor** fields, uint8_t fieldCount)
   {
      int16_t oldWidth = width();
      int16_t oldHeight = height();

      _fields = fields;
      _fieldCount = fieldCount;
      _table.clearRows();
      _relayout();

      int16_t clearWidth = max(oldWidth, width());
      int16_t clearHeight = max(oldHeight, height());
      Rect16 rect = _table.getRect();
      _arduino->fillRect(rect.x, rect.y, clearWidth, clearHeight, Color::BLACK);
   }

   ///
   /// <summary>
   /// Updates the position rows are drawn at, e.g. when the table follows a variable-height
   /// title that is only known at runtime.
   /// </summary>
   /// <param name="x">Left X coordinate of the table.</param>
   /// <param name="y">Top Y coordinate of the table.</param>
   ///
   void setPosition(int16_t x, int16_t y)
   {
      _table.setPosition(x, y);
   }

   ///
   /// <summary>
   /// Updates the position rows are drawn at, e.g. when the table follows a variable-height
   /// title that is only known at runtime.
   /// </summary>
   /// <param name="pos">The new top-left coordinate of the table.</param>
   ///
   void setPosition(Point16 pos)
   {
      _table.setPosition(pos);
   }

   ///
   /// <summary>
   /// Computes the table's bounding rectangle, based on its top-left position, its content
   /// width, and its total height. Useful for laying out other content (e.g. a value field)
   /// relative to the table's actual bounds.
   /// </summary>
   /// <returns>The table's bounding rectangle, in pixels.</returns>
   ///
   Rect16 getRect()
   {
      return _table.getRect();
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
      _table.invalidate();
   }

   ///
   /// <summary>
   /// Sets whether section header rows (see DisplayTableCellEditor::section()) are drawn and
   /// reserved space for. Defaults to true. Useful when a table reuses field objects whose
   /// section was set for a different table (e.g. a scatter-plot view reusing main-screen
   /// fields) and shouldn't repeat that section header.
   /// </summary>
   /// <param name="showSections">True to draw section headers, false to suppress them.</param>
   ///
   void setShowSections(bool showSections)
   {
      _table.setShowSections(showSections);
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
   /// currently selected field's value with a colored background. Syncs each field's current
   /// value/colors into the internal DisplayTable, then delegates all drawing to it.
   /// </summary>
   ///
   void draw()
   {
      _syncTable();
      _table.draw();
   }

   ///
   /// <summary>
   /// Gets the total pixel height of all rows (including section headers and their
   /// separating half-height blank rows) at the current text size, useful for laying out
   /// content below the table.
   /// </summary>
   /// <returns>Total height in pixels.</returns>
   ///
   int16_t height()
   {
      return _table.getRect().height;
   }

   ///
   /// <summary>
   /// Gets the total pixel width the table needs to display its widest label/value row.
   /// Useful for laying out other content (e.g. a plot) relative to the table's actual
   /// content width rather than a guessed fixed width.
   /// </summary>
   /// <returns>The required width, in pixels.</returns>
   ///
   int16_t width()
   {
      return _table.getWidth();
   }

   ///
   /// <summary>
   /// Gets the underlying field array, e.g. for loading/saving/resetting from Preferences.
   /// </summary>
   /// <returns>The field pointer array passed to the constructor.</returns>
   ///
   DisplayTableCellEditor** fields() const
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
