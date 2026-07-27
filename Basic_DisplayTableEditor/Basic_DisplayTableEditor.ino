//
// Basic DisplayTableEditor demonstration.
//
// Shows the minimal use of DisplayTableEditor: a small table of editable fields that can be
// selected and adjusted live with the board's encoders (Encoder A selects a field, Encoder B
// adjusts it, Encoder B's integral button resets all fields to their defaults). All of this
// is driven by a single call to DisplayTableEditor::update() in loop(). Values are not
// persisted in this sketch since there is no setup screen - they just live in RAM.
//
// Fields:
// - Bool:  toggles between False and True.
// - Value: an integer that increments/decrements by 1.
// - Enum:  cycles through Left, Right, Top, Bottom.
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

#include "DisplayTableCellEditor.h"
#include "DisplayTableEditor.h"
#include "SerialX.h"

// ----------- The Board
ESP32_S3_Playground arduino;

// ----------- Text Sizes
constexpr uint8_t HEADING_SIZE = DEFAULT_HEADING_SIZE;
constexpr uint8_t CONTENT_TEXT_SIZE = DEFAULT_CONTENT_SIZE;

// ----------- Preferences Namespace
constexpr const char* PREF_NAMESPACE = "basic_table_editor";

// ----------- Fields
long intValue = 0;
long enumValue = 0;
bool boolValue = false;

const char* const enumLabels[] = { "Left", "Right", "Top", "Bottom" };

BoolCellEditor boolCell(&boolValue, false, "#####");
IntCellEditor valueCell(&intValue, -1000, 1000, 1, 0, "#####");
EnumCellEditor enumCell(&enumValue, enumLabels, 0, "######");

TableEditorRow rows[] = {
   { "Bool", &boolCell },
   { "Value", &valueCell },
   { "Enum", &enumCell }
};
DisplayTableEditor editor(&arduino, PREF_NAMESPACE, rows, CONTENT_TEXT_SIZE);

void setup()
{
   SerialX::begin();
   arduino.begin();

   arduino.setCursor(0, 0);
   arduino.setTextSize(HEADING_SIZE);
   arduino.println("DisplayTableEditor", Color::HEADING);

   int16_t contentY = arduino.getCursorY();
   editor.setPosition(0, contentY);
   editor.load();
   editor.draw();
}

void loop()
{
   editor.update();
}
