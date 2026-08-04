//
// Basic FieldEditor demonstration.
//
// Shows the minimal use of FieldEditor for an inline (non-table) field layout: five
// editable integer fields placed at the four corners and the center of the available
// display area (below the "FieldEditor" heading), each labeled by its location ("Upper
// Left", "Upper Right", "Center", "Lower Left", "Lower Right"). Each field is paired with
// its own already-positioned Field instance (see the FieldInfo constructor taking a
// Field*), so FieldEditor::draw() renders all five automatically, highlighting whichever
// one is currently selected. Selection/adjustment/persistence is driven entirely by
// FieldEditor::loop() (Encoder A selects a field, Encoder B adjusts it, Encoder B's
// integral button resets all fields to their defaults).
//
// Hardware: Requires a board with encoders and a display (e.g. the ESP32-S3 Playground).
//

#include <Wire.h>

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. the ESP32-S3 Playground)."
#endif

#ifndef ARDUINO_PLAYGROUND_SUPPORTED
#error "This sketch requires a board with encoders (e.g. the ESP32-S3 Playground)."
#endif

#include "ValueEditor.h"
#include "FieldEditor.h"
#include "Field.h"
#include "SerialX.h"

// ----------- The Board
ESP32_S3_Playground arduino;

// ----------- Text Sizes
constexpr uint8_t HEADING_SIZE = DEFAULT_HEADING_SIZE;
constexpr uint8_t FIELD_TEXT_SIZE = DEFAULT_CONTENT_SIZE;

// ----------- Preferences Namespace
constexpr const char* PREF_NAMESPACE = "basic_field_editor";

// ----------- Locations
// Order matches the FieldInfo/Field arrays below throughout the sketch.
enum Location : uint8_t { UPPER_LEFT, UPPER_RIGHT, CENTER, LOWER_LEFT, LOWER_RIGHT, NUM_LOCATIONS };
const char* const LOCATION_LABELS[NUM_LOCATIONS] = { "Upper Left", "Upper Right", "Center", "Lower Left", "Lower Right" };

// ----------- Fields
IntEditor fieldEditors[NUM_LOCATIONS] = {
   IntEditor(-1000, 1000, 1, 0, "##"),
   IntEditor(-1000, 1000, 1, 0, "##"),
   IntEditor(-1000, 1000, 1, 0, "##"),
   IntEditor(-1000, 1000, 1, 0, "##"),
   IntEditor(-1000, 1000, 1, 0, "##"),
};

// Field instances, one per location, created without a position (see setup(), where the
// actual corner/center positions are computed once the heading's height is known).
Field locationFields[NUM_LOCATIONS] = {
   Field(&arduino, LOCATION_LABELS[UPPER_LEFT], fieldEditors[UPPER_LEFT].format(), FIELD_TEXT_SIZE, Field::Alignment::LEFT),
   Field(&arduino, LOCATION_LABELS[UPPER_RIGHT], fieldEditors[UPPER_RIGHT].format(), FIELD_TEXT_SIZE, Field::Alignment::RIGHT),
   Field(&arduino, LOCATION_LABELS[CENTER], fieldEditors[CENTER].format(), FIELD_TEXT_SIZE, Field::Alignment::GAP),
   Field(&arduino, LOCATION_LABELS[LOWER_LEFT], fieldEditors[LOWER_LEFT].format(), FIELD_TEXT_SIZE, Field::Alignment::LEFT),
   Field(&arduino, LOCATION_LABELS[LOWER_RIGHT], fieldEditors[LOWER_RIGHT].format(), FIELD_TEXT_SIZE, Field::Alignment::RIGHT),
};

// Each entry is paired with its own Field above, so FieldEditor::draw() can render it
// automatically; the field's own label doubles as this entry's Preferences key, so it
// isn't repeated here.
FieldEditor::FieldInfo fieldInfos[NUM_LOCATIONS] = {
   { &fieldEditors[UPPER_LEFT], &locationFields[UPPER_LEFT] },
   { &fieldEditors[UPPER_RIGHT], &locationFields[UPPER_RIGHT] },
   { &fieldEditors[CENTER], &locationFields[CENTER] },
   { &fieldEditors[LOWER_LEFT], &locationFields[LOWER_LEFT] },
   { &fieldEditors[LOWER_RIGHT], &locationFields[LOWER_RIGHT] },
};
FieldEditor editor(&arduino, PREF_NAMESPACE, fieldInfos);

void setup()
{
   SerialX::begin();
   arduino.begin();

   arduino.setCursor(0, 0);
   arduino.setTextSize(HEADING_SIZE);
   arduino.println("FieldEditor", Color::HEADING);

   // Corner/center positions within the area below the heading. Field::setPosition()
   // (via ArduinoWithDisplay::normalizeCoords()) treats a negative y as an offset from
   // the bottom edge, so the lower row's y is passed directly as -charH() instead of
   // computing bottom - charH(); the right edge itself has no such shortcut since it's
   // the far edge with zero offset, not an offset from it.
   int16_t left = 0;
   int16_t right = (int16_t)arduino.width();
   int16_t top = arduino.getCursorY();
   int16_t lowerY = -arduino.charH(FIELD_TEXT_SIZE);
   int16_t centerX = (left + right) / 2;
   int16_t centerY = (top + (int16_t)arduino.height()) / 2 - arduino.charH(FIELD_TEXT_SIZE) / 2;

   locationFields[UPPER_LEFT].setPosition(left, top);
   locationFields[UPPER_RIGHT].setPosition(right, top);
   locationFields[CENTER].setPosition(centerX, centerY);
   locationFields[LOWER_LEFT].setPosition(left, lowerY);
   locationFields[LOWER_RIGHT].setPosition(right, lowerY);

   editor.load();
   editor.draw();
}

void loop()
{
   if (editor.loop())
   {
      editor.draw();
   }
}
