#pragma once

#include <Arduino.h>
#include "Color.h"
#include "ESP32_S3_Playground.h"
#include "DisplayEditableField.h"
#include "DisplayEditableTable.h"

///
/// <summary>
/// Interactive, blocking, full-screen editor that drives a list of DisplayEditableField objects using a
/// board's encoders and buttons. Encoder A cycles the selected field, Encoder B adjusts its
/// value, Button B resets all fields to their defaults, and Button A confirms and saves.
/// Internally uses a DisplayEditableTable for field navigation/adjustment/drawing/persistence,
/// so the same DisplayEditableField list can also be embedded directly into another screen via
/// DisplayEditableTable.
/// </summary>
///
class DisplayEditor
{
public:
   ///
   /// <summary>
   /// Initializes a new instance of the DisplayEditor class.
   /// </summary>
   /// <param name="arduino">Board providing the display, encoders, buttons, and preferences.</param>
   /// <param name="prefNamespace">Preferences namespace used to persist field values.</param>
   /// <param name="title">Heading text drawn at the top of the setup screen.</param>
   /// <param name="fields">Array of field pointers to display and edit.</param>
   /// <param name="fieldCount">Number of entries in fields.</param>
   ///
   DisplayEditor(ESP32_S3_Playground* arduino, const char* prefNamespace,
               const char* title, DisplayEditableField** fields, uint8_t fieldCount)
      : _arduino(arduino), _title(title),
        _table(arduino, prefNamespace, fields, fieldCount)
   {
   }

   ///
   /// <summary>
   /// Loads all field values from Preferences, falling back to defaults for missing entries.
   /// </summary>
   ///
   void load()
   {
      _table.load();
   }

   ///
   /// <summary>
   /// Persists all field values to Preferences.
   /// </summary>
   ///
   void save()
   {
      _table.save();
   }

   ///
   /// <summary>
   /// Restores all fields to their defaults and persists the result.
   /// </summary>
   ///
   void reset()
   {
      _table.reset();
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
         if (_table.poll())
         {
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
   const char* _title;
   DisplayEditableTable _table;
   bool _viewInitialized = false;

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

      int16_t tableTop = _arduino->getCursorY();
      _table.draw(0, tableTop);

      _arduino->setCursor(0, tableTop + _table.height());
      _arduino->println();

      static constexpr const char* INSTRUCTIONS[] = {
         "Encoder A: Select",
         "Encoder B: Adjust",
         "Button A: Confirm",
         "Button B: Reset",
      };
      constexpr size_t NUM_INSTRUCTIONS = sizeof(INSTRUCTIONS) / sizeof(INSTRUCTIONS[0]);

      int16_t rowHeight = _arduino->charH();
      int16_t y = (int16_t)_arduino->height() - rowHeight * (int16_t)NUM_INSTRUCTIONS;
      for (size_t i = 0; i < NUM_INSTRUCTIONS; i++)
      {
         int16_t x = (int16_t)_arduino->width() - (int16_t)_arduino->textWidth(INSTRUCTIONS[i]);
         _arduino->setCursor(x, y);
         _arduino->println(INSTRUCTIONS[i], Color::GRAY);
         y += rowHeight;
      }
   }
};
