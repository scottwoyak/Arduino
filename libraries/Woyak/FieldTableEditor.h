#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <span>
#include "Color.h"
#include "ArduinoBoard.h"
#include "DisplayTableCellEditor.h"
#include "FieldTable.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "FieldTableEditor requires a board with a display."
#endif
#ifndef ARDUINO_PREFERENCES_SUPPORTED
#error "FieldTableEditor requires a board with Preferences support."
#endif

///
/// <summary>
/// Pairs a DisplayTableCell (or CellEditor) with the row-level display metadata
/// (label) needed to render it as a row in a FieldTableEditor. The first column of
/// a row is just a label; it isn't part of the cell's own value/editing behavior, so
/// that metadata lives here instead of on the cell itself. A row can also be a
/// label-only section header (no cell), which renders as its own section row (see
/// FieldTable::addSection()) above the rows that follow it, e.g. { "Measured" }.
/// </summary>
///
struct FieldTableEditorRow
{
   ///
   /// <summary>
   /// Initializes a new instance of the FieldTableEditorRow struct for a normal labeled field.
   /// </summary>
   /// <param name="label">Label text drawn in the row's first column, e.g. "Rate".</param>
   /// <param name="cell">The cell providing this row's value/editing behavior.</param>
   ///
   FieldTableEditorRow(const char* label, DisplayTableCell* cell)
      : label(label), cell(cell)
   {}

   ///
   /// <summary>
   /// Initializes a new instance of the FieldTableEditorRow struct as a section header row - a
   /// label-only entry with no cell that renders as its own section row above the rows that
   /// follow it.
   /// </summary>
   /// <param name="header">Section header text drawn above the following rows.</param>
   ///
   explicit FieldTableEditorRow(const char* header)
      : label(header), cell(nullptr)
   {}

   const char* label;
   DisplayTableCell* cell;

   // Row index within the internal FieldTable this field maps to, or -1 for section
   // header rows, which are added as their own row via addSection() rather than
   // stored on the FieldTableEditorRow that precedes them. Set by
   // FieldTableEditor::_relayout().
   int8_t rowIndex = -1;
};

///
/// <summary>
/// Reusable, embeddable single-column table of DisplayTableCell values (a mix of editable
/// CellEditor fields and read-only cells) that can be navigated and adjusted live with a
/// board's encoders: Encoder A cycles the selected field, Encoder B adjusts its value.
/// Also supports loading/saving/resetting all fields against a Preferences namespace. This
/// class owns no rendering logic of its own - it delegates all drawing to an internal
/// FieldTable, so drawing fixes (sprite creation, text-size restoration, flicker avoidance,
/// layout math, etc.) only need to live in one place. FieldTableEditor simply tracks the
/// selected field, persists values, and draws each field's current text/color/selection
/// state through the internal FieldTable.
/// </summary>
///
class FieldTableEditor
{
private:
   Arduino* _arduino;
   const char* _prefNamespace;
   std::span<FieldTableEditorRow> _rows;
   uint8_t _selectedIndex = 0;
   char _keyBuffer[10];
   FieldTable _table;

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
   /// <param name="row">Row to compute a persistence key for.</param>
   /// <returns>A short, stable Preferences key.</returns>
   ///
   const char* _keyFor(const FieldTableEditorRow& row)
   {
      // Simple FNV-1a hash of the label, truncated to fit Preferences' short key limit.
      uint32_t hash = 2166136261u;
      for (const char* p = row.label; *p != '\0'; p++)
      {
         hash ^= static_cast<uint8_t>(*p);
         hash *= 16777619u;
      }
      snprintf(_keyBuffer, sizeof(_keyBuffer), "f%08lx", static_cast<unsigned long>(hash));
      return _keyBuffer;
   }

   ///
   /// <summary>
   /// Rebuilds the internal FieldTable's rows from the current field array and resets the
   /// selection to the first editable field. Section header rows (no cell) are added as
   /// their own section row via addSection(), reusing FieldTable's existing section-header
   /// rendering. Shared by the constructor and setFields().
   /// </summary>
   ///
   void _relayout()
   {
      for (uint8_t i = 0; i < _rows.size(); i++)
      {
         if (_rows[i].cell == nullptr)
         {
            _rows[i].rowIndex = -1;
            _table.addSection(_rows[i].label);
            continue;
         }

         _rows[i].rowIndex = static_cast<int8_t>(_table.rowCount());
         _table.addRow(_rows[i].label, _rows[i].cell->format());
      }

      _selectedIndex = 0;
      for (uint8_t i = 0; i < _rows.size(); i++)
      {
         if (_rows[i].cell != nullptr && _rows[i].cell->isEditable())
         {
            _selectedIndex = i;
            break;
         }
      }
   }

public:
   ///
   /// <summary>
   /// Initializes a new instance of the FieldTableEditor class.
   /// </summary>
   /// <param name="arduino">Board providing the display, encoders, and preferences.</param>
   /// <param name="prefNamespace">Preferences namespace used to persist field values.</param>
   /// <param name="fields">Span of rows to display and edit.</param>
   /// <param name="x">Left X coordinate of the label column's left edge, shared by every row.</param>
   /// <param name="y">Top Y coordinate of the table.</param>
   /// <param name="textSize">The text size applied automatically before drawing labels and values.</param>
   ///
   FieldTableEditor(Arduino* arduino, const char* prefNamespace, std::span<FieldTableEditorRow> fields,
      int16_t x, int16_t y, uint8_t textSize = 2)
      : _arduino(arduino), _prefNamespace(prefNamespace), _rows(fields),
      _table(arduino, x, y, textSize)
   {
      _relayout();
   }

   ///
   /// <summary>
   /// Initializes a new instance of the FieldTableEditor class, deferring positioning until
   /// setPosition() is called, e.g. once a variable-height title's actual height is known.
   /// </summary>
   /// <param name="arduino">Board providing the display, encoders, and preferences.</param>
   /// <param name="prefNamespace">Preferences namespace used to persist field values.</param>
   /// <param name="fields">Span of rows to display and edit.</param>
   /// <param name="textSize">The text size applied automatically before drawing labels and values.</param>
   ///
   FieldTableEditor(Arduino* arduino, const char* prefNamespace, std::span<FieldTableEditorRow> fields,
      uint8_t textSize = 2)
      : FieldTableEditor(arduino, prefNamespace, fields, 0, 0, textSize)
   {}

   ///
   /// <summary>
   /// Replaces the set of fields this table displays/edits, e.g. to show different
   /// configuration rows depending on some other live selection (like an active test
   /// function). Resets the selection to the first editable field. Does not draw the
   /// remaining rows; call load() and draw() afterward to load the new fields' persisted
   /// values and refresh the display. Does not change the table's position; call
   /// setPosition() separately if needed.
   /// </summary>
   /// <param name="fields">Span of rows to display and edit.</param>
   ///
   void setFields(std::span<FieldTableEditorRow> fields)
   {
      int16_t oldWidth = width();
      int16_t oldHeight = height();

      _rows = fields;
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
   /// <param name="x">The X coordinate of the anchor point; negative values offset from the right edge.</param>
   /// <param name="y">The Y coordinate of the anchor point; negative values offset from the bottom edge.</param>
   /// <param name="anchor">Which point of the table's bounding rectangle (x, y) refers to (default: TOP_LEFT).</param>
   ///
   void setPosition(int16_t x, int16_t y, Anchor anchor = Anchor::TOP_LEFT)
   {
      _table.setPosition(x, y, anchor);
   }

   ///
   /// <summary>
   /// Updates the position rows are drawn at, e.g. when the table follows a variable-height
   /// title that is only known at runtime.
   /// </summary>
   /// <param name="pos">The coordinate of the anchor point.</param>
   /// <param name="anchor">Which point of the table's bounding rectangle pos refers to (default: TOP_LEFT).</param>
   ///
   void setPosition(Point16 pos, Anchor anchor = Anchor::TOP_LEFT)
   {
      _table.setPosition(pos, anchor);
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
      if (index < _rows.size() && _rows[index].cell != nullptr && _rows[index].cell->isEditable())
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

      int32_t fieldCount = static_cast<int32_t>(_rows.size());
      int32_t step = direction > 0 ? 1 : -1;
      int32_t newIndex = static_cast<int32_t>(_selectedIndex);
      for (uint8_t i = 0; i < _rows.size(); i++)
      {
         newIndex = (newIndex + step + fieldCount) % fieldCount;
         if (_rows[newIndex].cell != nullptr && _rows[newIndex].cell->isEditable() && _rows[newIndex].cell->isEnabled())
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

      DisplayTableCell* cell = _rows[_selectedIndex].cell;
      if (cell->isEditable() && cell->isEnabled())
      {
         static_cast<CellEditor*>(cell)->adjust(direction);
      }
   }

   ///
   /// <summary>
   /// Draws the label/value rows at the position given to the constructor, highlighting the
   /// currently selected field's value with a colored background. Reads each field's
   /// current value/colors directly from its cell and delegates all drawing to the
   /// internal FieldTable.
   /// </summary>
   ///
   void draw()
   {
      _table._relayout();

      for (uint8_t i = 0; i < _rows.size(); i++)
      {
         DisplayTableCell* cell = _rows[i].cell;
         if (cell == nullptr)
         {
            _table._drawSectionRow(_rows[i].rowIndex);
            continue;
         }

         bool isSelected = cell->isEditable() && cell->isEnabled() && (i == _selectedIndex);
         bool isDisabled = !cell->isEnabled();

         Color valueBackgroundColor = isSelected ? Color::BLUE : Color::BLACK;
         Color valueColor = isDisabled ? Color::GRAY : (isSelected ? Color::WHITE : (cell->hasColor() ? cell->color() : (cell->isEditable() ? Color::VALUE : Color::VALUE2)));

         _table._drawDataRow(_rows[i].rowIndex, cell->valueText(), valueColor, valueBackgroundColor);
      }
   }

#ifdef ARDUINO_PLAYGROUND_SUPPORTED
   ///
   /// <summary>
   /// Drives the editor from the board's own encoders in a single call: Encoder A moves the
   /// selection (selectNext()), Encoder B adjusts the selected field's value
   /// (adjustSelected()) and persists the change, Encoder B's integral button resets all
   /// fields to their defaults (reset(), which also persists), and the table is redrawn
   /// afterward. This is the simplest way to drive the editor each loop() iteration when no
   /// additional per-change logic is needed. Requires a board with Encoder A/B (see
   /// ARDUINO_PLAYGROUND_SUPPORTED); use selectNext()/adjustSelected()/reset()/draw()
   /// directly if finer control is needed (e.g. reacting to a change before it's applied).
   /// </summary>
   ///
   void loop()
   {
      selectNext(_arduino->encoderA.delta());

      int32_t adjustDelta = _arduino->encoderB.delta();
      if (adjustDelta != 0)
      {
         adjustSelected(adjustDelta);
         save();
      }

      if (_arduino->encoderB.button.wasPressed())
      {
         reset();
      }

      draw();
   }
#endif

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
      return _table.getHeight();
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
   /// Gets the underlying row span, e.g. for loading/saving/resetting from Preferences.
   /// </summary>
   /// <returns>The row span passed to the constructor.</returns>
   ///
   std::span<FieldTableEditorRow> fields() const
   {
      return _rows;
   }

   ///
   /// <summary>
   /// Gets the number of fields in this table.
   /// </summary>
   /// <returns>Field count.</returns>
   ///
   uint8_t fieldCount() const
   {
      return static_cast<uint8_t>(_rows.size());
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
      for (uint8_t i = 0; i < _rows.size(); i++)
      {
         if (_rows[i].cell == nullptr || !_rows[i].cell->isEditable())
         {
            continue;
         }
         CellEditor* field = static_cast<CellEditor*>(_rows[i].cell);
         double defaultValue = field->defaultNumericValue();
         double value = prefs->getDouble(_keyFor(_rows[i]), defaultValue);
         field->setNumericValue(value);
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
      for (uint8_t i = 0; i < _rows.size(); i++)
      {
         if (_rows[i].cell == nullptr || !_rows[i].cell->isEditable())
         {
            continue;
         }
         CellEditor* field = static_cast<CellEditor*>(_rows[i].cell);
         prefs->putDouble(_keyFor(_rows[i]), field->numericValue());
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
      for (uint8_t i = 0; i < _rows.size(); i++)
      {
         if (_rows[i].cell == nullptr || !_rows[i].cell->isEditable())
         {
            continue;
         }
         static_cast<CellEditor*>(_rows[i].cell)->reset();
      }
      save();
   }
};
