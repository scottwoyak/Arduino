//
// Demonstrates DisplayField layout options: left-aligned, right-aligned, and centered
// fields positioned at the corners and center of the display.
//
// Shows a "DisplayField" heading and five "Label: ##.#" fields, one at the top-left,
// one at the bottom-left, one centered, and two right-aligned at the top-right and
// bottom-right. Each field has its own independent value, shown with a blue background
// when selected. Rotate Encoder A to change the selected field; rotate Encoder B to
// increment the selected field's value by 0.1.
//

#include <Wire.h>

#include "ESP32_S3_Playground.h"
#include "DisplayField.h"
#include "SerialX.h"

// ----------- The Board
ESP32_S3_Playground arduino;

// ----------- Layout
constexpr uint8_t HEADING_TEXT_SIZE = 4;
constexpr uint8_t FIELD_TEXT_SIZE = 3;

// ----------- Field Value
Format valueFormat("##.#");
constexpr float VALUE_STEP = 0.1f;
constexpr uint8_t NUM_FIELDS = 5;
float values[NUM_FIELDS] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
Color backgroundColors[NUM_FIELDS] = { Color::BLACK, Color::BLACK, Color::BLACK, Color::BLACK, Color::BLACK };
uint8_t selectedIndex = 0;

DisplayField topLeftField(&arduino, "Label", valueFormat, FIELD_TEXT_SIZE, DisplayField::Alignment::LEFT);
DisplayField topRightField(&arduino, "Label", valueFormat, FIELD_TEXT_SIZE, DisplayField::Alignment::RIGHT);
DisplayField bottomRightField(&arduino, "Label", valueFormat, FIELD_TEXT_SIZE, DisplayField::Alignment::RIGHT);
DisplayField bottomLeftField(&arduino, "Label", valueFormat, FIELD_TEXT_SIZE, DisplayField::Alignment::LEFT);
DisplayField centerField(&arduino, "Label", valueFormat, FIELD_TEXT_SIZE, DisplayField::Alignment::COLON);

// Selection rotation order: top-left, top-right, bottom-right, bottom-left, center
DisplayField* fields[NUM_FIELDS] =
{
   &topLeftField, &topRightField, &bottomRightField, &bottomLeftField, &centerField
};

///
/// <summary>
/// Clears the display and draws the "DisplayField" heading.
/// </summary>
///
void drawHeader()
{
   arduino.clearDisplay();

   arduino.setTextSize(HEADING_TEXT_SIZE);
   arduino.println("DisplayField", Color::HEADING);
}

///
/// <summary>
/// Positions the five fields at the top-left, bottom-left, center, top-right, and
/// bottom-right of the display.
/// </summary>
///
void positionFields()
{
   arduino.setTextSize(HEADING_TEXT_SIZE);
   int16_t headerHeight = arduino.charH();

   arduino.setTextSize(FIELD_TEXT_SIZE);

   topLeftField.setPosition(Point16(0, headerHeight));
   bottomLeftField.setPosition(Point16(0, -arduino.charH()));
   topRightField.setPosition(Point16(arduino.width(), headerHeight));
   bottomRightField.setPosition(Point16(arduino.width(), -arduino.charH()));

   int16_t centerY = (int16_t)(arduino.center().y - arduino.charH() / 2);
   centerField.setPosition(Point16(arduino.center().x, centerY));
}

///
/// <summary>
/// Sets the selected field's background to blue and all others back to black.
/// </summary>
///
void updateSelection()
{
   for (uint8_t i = 0; i < NUM_FIELDS; i++)
   {
      backgroundColors[i] = (i == selectedIndex) ? Color::BLUE : Color::BLACK;
   }
}

///
/// <summary>
/// Redraws all five fields with their own current values.
/// </summary>
///
void updateFields()
{
   for (uint8_t i = 0; i < NUM_FIELDS; i++)
   {
      fields[i]->draw(values[i], Color::LABEL, Color::VALUE, backgroundColors[i]);
   }
}

void setup()
{
   SerialX::begin();
   Wire.begin();

   arduino.begin();
   arduino.encoderA.begin();
   arduino.encoderB.begin();

   drawHeader();
   positionFields();
   updateSelection();
}

void loop()
{
   if (arduino.encoderA.hasChanged())
   {
      int32_t selectDelta = arduino.encoderA.delta();
      selectedIndex = (selectedIndex + NUM_FIELDS + selectDelta) % NUM_FIELDS;
      updateSelection();
   }

   if (arduino.encoderB.hasChanged())
   {
      int32_t valueDelta = arduino.encoderB.delta();
      values[selectedIndex] += (float)valueDelta * VALUE_STEP;
   }

   updateFields();
}
