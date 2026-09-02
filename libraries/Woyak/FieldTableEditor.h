#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <span>
#include "ColorX.h"
#include "ArduinoBoard.h"
#include "ValueEditor.h"
#include "FieldEditor.h"
#include "FieldTable.h"
#include "Vector.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "FieldTableEditor requires a board with a display."
#endif
#ifndef ARDUINO_PREFERENCES_SUPPORTED
#error "FieldTableEditor requires a board with Preferences support."
#endif

///
/// <summary>
/// Reusable, embeddable single-column table of ValueBase values (a mix of editable
/// Editor fields and read-only values) that can be navigated and adjusted live with a
/// board's encoders: Encoder A cycles the selected field, Encoder B adjusts its value.
/// Also supports loading/saving/resetting all fields against a Preferences namespace.
/// Selection/adjustment/persistence is delegated to an internal FieldEditor (the
/// render-agnostic part), while this class owns only the FieldTable-based rendering
/// (sprite creation, text-size restoration, flicker avoidance, layout math, etc.) -
/// FieldTableEditor simply draws each field's current text/color/selection state, reading
/// selection state from the internal FieldEditor.
/// </summary>
///
class FieldTableEditor
{
public:
   ///
   /// <summary>
   /// Pairs a ValueBase (or Editor) with the row-level display metadata (label) needed to
   /// identify it in the table. The label doubles as the key used to persist this row's
   /// value to Preferences, so it isn't asked for separately. The first column of a row is
   /// just a label; it isn't part of the value's own value/editing behavior, so that
   /// metadata lives here instead of on the value itself. A row can also be a label-only
   /// section header (no value), or a blank spacer row with neither label nor value.
   /// </summary>
   ///
   struct Row
   {
      ///
      /// <summary>
      /// Initializes a new instance of the Row struct for a normal labeled field.
      /// </summary>
      /// <param name="label">Label text identifying this row, e.g. "Rate". Also used as its
      /// Preferences key.</param>
      /// <param name="value">The value providing this row's value/editing behavior.</param>
      ///
      Row(const char* label, ValueBase* value)
         : label(label), value(value)
      {}

      ///
      /// <summary>
      /// Initializes a new instance of the Row struct as a section header row - a
      /// label-only entry with no value, for sectioned tables.
      /// </summary>
      /// <param name="header">Section header text.</param>
      ///
      Row(const char* header)
         : label(header), value(nullptr)
      {}

      const char* label;
      ValueBase* value;

      // Row index within the internal FieldTable this row maps to, or -1 for section header
      // rows. Populated by _relayout().
      int8_t rowIndex = -1;

      ///
      /// <summary>
      /// A blank spacer data row (no label, no editable value), backed by a shared
      /// BlankValue instance, e.g. { FieldTableEditor::Row::BlankRow } to push a following
      /// field down to a different row than a field in an adjacent table it might
      /// otherwise visually overlap.
      /// </summary>
      ///
      static const Row BlankRow;
   };

private:
   Arduino* _arduino;
   std::span<Row> _rows;
   Vector<FieldEditor::FieldInfo> _editorFields;
   // Maps each entry in _editorFields back to its index in _rows, since section header rows
   // are excluded from _editorFields (FieldEditor has no notion of sections).
   Vector<uint8_t> _editorFieldRowIndices;
   FieldEditor _editor;
   FieldTable _table;

   ///
   /// <summary>
   /// Rebuilds the internal FieldEditor's flat field list and the internal FieldTable's rows
   /// from the current row array. Section header rows (no value) are added as their own
   /// section row via addSection(), reusing FieldTable's existing section-header rendering,
   /// and are skipped when building the field list since FieldEditor has no notion of
   /// sections. Shared by the constructor and setFields().
   /// </summary>
   ///
   void _relayout()
   {
      _editorFields.clear();
      _editorFieldRowIndices.clear();
      for (uint8_t i = 0; i < _rows.size(); i++)
      {
         if (_rows[i].value == nullptr)
         {
            _rows[i].rowIndex = static_cast<int8_t>(_table.rowCount());
            _table.addSection(_rows[i].label);
            continue;
         }

         _rows[i].rowIndex = static_cast<int8_t>(_table.rowCount());
         _table.addRow(_rows[i].label, _rows[i].value->format());
         _editorFields.append() = FieldEditor::FieldInfo(_rows[i].label, _rows[i].value);
         _editorFieldRowIndices.append() = i;
      }
      _editor.setFields(_editorFields);
   }

   ///
   /// <summary>
   /// Converts a row index into the corresponding index within the internal FieldEditor's
   /// flat field list, or -1 if that row has no value (e.g. a section header).
   /// </summary>
   /// <param name="rowIndex">Index into _rows.</param>
   /// <returns>The corresponding index into _editorFields, or -1 if none.</returns>
   ///
   int16_t _fieldIndexForRow(uint8_t rowIndex) const
   {
      for (uint8_t i = 0; i < _editorFieldRowIndices.size(); i++)
      {
         if (_editorFieldRowIndices[i] == rowIndex)
         {
            return static_cast<int16_t>(i);
         }
      }
      return -1;
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
   FieldTableEditor(Arduino* arduino, const char* prefNamespace, std::span<Row> fields,
      int16_t x, int16_t y, uint8_t textSize = 2)
      : _arduino(arduino), _rows(fields), _editor(arduino, prefNamespace, std::span<FieldEditor::FieldInfo>()),
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
   FieldTableEditor(Arduino* arduino, const char* prefNamespace, std::span<Row> fields,
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
   void setFields(std::span<Row> fields)
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
      uint8_t fieldIndex = _editor.selectedIndex();
      return fieldIndex < _editorFieldRowIndices.size() ? _editorFieldRowIndices[fieldIndex] : 0;
   }

   ///
   /// <summary>
   /// Sets the currently selected field index, clamped to a valid range.
   /// </summary>
   /// <param name="index">Zero-based index of the row to select.</param>
   ///
   void setSelectedIndex(uint8_t index)
   {
      int16_t fieldIndex = _fieldIndexForRow(index);
      if (fieldIndex >= 0)
      {
         _editor.setSelectedIndex(static_cast<uint8_t>(fieldIndex));
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
      _editor.selectNext(direction);
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
      _editor.adjustSelected(direction);
   }

   ///
   /// <summary>
   /// Draws the label/value rows at the position given to the constructor, highlighting the
   /// currently selected field's value with a colored background. Reads each field's
   /// current value/colors directly from its value and delegates all drawing to the
   /// internal FieldTable.
   /// </summary>
   ///
   void draw()
   {
      _table._relayout();

      for (uint8_t i = 0; i < _rows.size(); i++)
      {
         ValueBase* value = _rows[i].value;
         if (value == nullptr)
         {
            _table._drawSectionRow(_rows[i].rowIndex);
            continue;
         }

         Color valueColor;
         Color valueBackgroundColor;
         int16_t fieldIndex = _fieldIndexForRow(i);
         _editor.colorsFor(static_cast<uint8_t>(fieldIndex), valueColor, valueBackgroundColor);

         _table._drawDataRow(_rows[i].rowIndex, value->valueText(), valueColor, valueBackgroundColor);
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
      _editor.loop();
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
   std::span<Row> fields() const
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
      return _editor.fieldCount();
   }

   ///
   /// <summary>
   /// Loads all field values from Preferences, falling back to defaults for missing entries.
   /// </summary>
   ///
   void load()
   {
      _editor.load();
   }

   ///
   /// <summary>
   /// Persists all field values to Preferences.
   /// </summary>
   ///
   void save()
   {
      _editor.save();
   }

   ///
   /// <summary>
   /// Restores all fields to their defaults and persists the result.
   /// </summary>
   ///
   void reset()
   {
      _editor.reset();
   }
};

inline const FieldTableEditor::Row FieldTableEditor::Row::BlankRow = FieldTableEditor::Row("", &BlankValue::instance());
