//
// Demonstrates DisplayValue layout options: left-aligned, right-aligned, and centered
// values positioned at the corners and center of the display.
//
// Shows a "DisplayValue" heading and five "##.#" values (no label), one at the top-left,
// one at the bottom-left, one centered, and two right-aligned at the top-right and
// bottom-right. Each value has its own independent value, shown with a blue background
// when selected. Rotate Encoder A to change the selected value; rotate Encoder B to
// increment the selected value by 0.1.
//

#include <Wire.h>

#include "ESP32_S3_Playground.h"
#include "DisplayValue.h"
#include "SerialX.h"

// ----------- The Board
ESP32_S3_Playground arduino;

// ----------- Layout
constexpr uint8_t HEADING_TEXT_SIZE = 4;
constexpr uint8_t FIELD_TEXT_SIZE = 3;

// ----------- Field Value
Format valueFormat("##.#");
constexpr float VALUE_STEP = 0.1f;
constexpr uint8_t NUM_VALUES = 5;
float values[NUM_VALUES] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
uint8_t selectedIndex = 0;

DisplayValue topLeftValue(&arduino, valueFormat, FIELD_TEXT_SIZE, DisplayValue::Alignment::LEFT);
DisplayValue topRightValue(&arduino, valueFormat, FIELD_TEXT_SIZE, DisplayValue::Alignment::RIGHT);
DisplayValue bottomRightValue(&arduino, valueFormat, FIELD_TEXT_SIZE, DisplayValue::Alignment::RIGHT);
DisplayValue bottomLeftValue(&arduino, valueFormat, FIELD_TEXT_SIZE, DisplayValue::Alignment::LEFT);
DisplayValue centerValue(&arduino, valueFormat, FIELD_TEXT_SIZE, DisplayValue::Alignment::DECIMAL);

// Selection rotation order: top-left, top-right, bottom-right, bottom-left, center
DisplayValue* displayValues[NUM_VALUES] =
{
   &topLeftValue, &topRightValue, &bottomRightValue, &bottomLeftValue, &centerValue
};

///
/// <summary>
/// Clears the display and draws the "DisplayValue" heading.
/// </summary>
///
void drawHeader()
{
   arduino.clearDisplay();

   arduino.setTextSize(HEADING_TEXT_SIZE);
   arduino.println("DisplayValue", Color::HEADING);
}

///
/// <summary>
/// Positions the five value fields at the top-left, top-right, bottom-right, bottom-left,
/// and center of the display.
/// </summary>
///
void positionValues()
{
   arduino.setTextSize(HEADING_TEXT_SIZE);
   int16_t headerHeight = arduino.charH();

   arduino.setTextSize(FIELD_TEXT_SIZE);

   int16_t bottomY = arduino.height() - arduino.charH();

   topLeftValue.setPosition(0, headerHeight);
   bottomLeftValue.setPosition(0, bottomY);

   int16_t rightX = arduino.width();
   topRightValue.setPosition(rightX, headerHeight);
   bottomRightValue.setPosition(rightX, bottomY);

   int16_t centerX = arduino.center().x;
   int16_t centerY = (int16_t)(arduino.center().y - arduino.charH() / 2);
   centerValue.setPosition(centerX, centerY);
}

///
/// <summary>
/// Redraws all five values with their own current values, highlighting the selected
/// value's background in blue.
/// </summary>
///
void updateValues()
{
   for (uint8_t i = 0; i < NUM_VALUES; i++)
   {
      Color backgroundColor = (i == selectedIndex) ? Color::BLUE : Color::BLACK;
      displayValues[i]->draw(values[i], Color::VALUE, backgroundColor);
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
   positionValues();
}

void loop()
{
   if (arduino.encoderA.hasChanged())
   {
      int32_t selectDelta = arduino.encoderA.delta();
      selectedIndex = (selectedIndex + NUM_VALUES + selectDelta) % NUM_VALUES;
   }

   if (arduino.encoderB.hasChanged())
   {
      int32_t valueDelta = arduino.encoderB.delta();
      values[selectedIndex] += (float)valueDelta * VALUE_STEP;
   }

   updateValues();
}
