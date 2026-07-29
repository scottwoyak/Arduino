//
// Minimal repro of the Sensor_Warm_Up_Playground summary table column alignment issue:
// builds the same 4-column DisplayTable (Target/Actual/Start/Delta, all right-aligned)
// with a handful of static rows and just draws it centered on the screen, so the
// header/value alignment can be inspected in isolation.
//

// System/standard library headers
#include <Wire.h>
#include <array>

// Local library headers (from libraries/Woyak)
#include "ESP32_S3_Playground.h"
#include "DisplayTable.h"
#include "SerialX.h"

// ----------- The Board
ESP32_S3_Playground arduino;

// ----------- Display Geometry
constexpr uint16_t DISPLAY_WIDTH = 480;
constexpr uint16_t DISPLAY_HEIGHT = 320;
constexpr uint8_t BODY_TEXT_SIZE = 2;

// ----------- Table Columns (mirrors RESULT_TABLE_DISPLAY_COLUMNS in Sensor_Warm_Up_Playground)
std::array RESULT_TABLE_DISPLAY_COLUMNS = {
   DisplayTable::Column(""),
   DisplayTable::Column("Target", "###/s", DisplayTable::Alignment::RIGHT),
   DisplayTable::Column("Actual", "###/s", DisplayTable::Alignment::RIGHT),
   DisplayTable::Column("Start", "###.##", DisplayTable::Alignment::RIGHT),
   DisplayTable::Column("Delta", "+##.###", DisplayTable::Alignment::RIGHT),
};
DisplayTable resultDisplayTable(&arduino, 0, 0, RESULT_TABLE_DISPLAY_COLUMNS, BODY_TEXT_SIZE, Color::LABEL);

// ----------- Static Test Data
struct TestRow
{
   unsigned long targetRate;
   float actualRate;
   float start;
   float delta;
   Color color;
};

constexpr TestRow TEST_ROWS[] = {
   { 2UL, 2.0f, 76.71f, 0.034f, Color::GREEN },
   { 10UL, 10.0f, 76.73f, 0.018f, Color::YELLOW },
   { 20UL, 20.0f, 76.75f, 0.018f, Color::CYAN },
   { 30UL, 30.0f, 76.77f, 0.024f, Color::MAGENTA },
   { 50UL, 50.0f, 76.80f, 0.018f, Color::ORANGE },
   { 100UL, 79.0f, 76.84f, -0.014f, Color::PINK },
};
constexpr size_t NUM_TEST_ROWS = sizeof(TEST_ROWS) / sizeof(TEST_ROWS[0]);

///
/// <summary>
/// Fills the table with the static test rows and centers it on the display.
/// </summary>
///
void drawTable()
{
   arduino.clearDisplay();

   resultDisplayTable.clearRows();

   for (size_t i = 0; i < NUM_TEST_ROWS; i++)
   {
      const TestRow& row = TEST_ROWS[i];
      resultDisplayTable.addRow("", row.color);
      resultDisplayTable.setValue(i, 0, row.targetRate, row.color);
      resultDisplayTable.setValue(i, 1, row.actualRate, row.color);
      resultDisplayTable.setValue(i, 2, row.start, row.color);
      resultDisplayTable.setValue(i, 3, row.delta, row.color);
   }

//   Point16 center = arduino.center();
//   resultDisplayTable.setPosition(center, Anchor::CENTER);
   Point16 center(0,0);
   resultDisplayTable.setPosition(center, Anchor::TOP_LEFT);

   resultDisplayTable.draw();
}

void setup()
{
   SerialX::begin();
   Wire.begin();

   arduino.begin();

   drawTable();
}

void loop()
{
}
