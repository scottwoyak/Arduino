//
// Basic Table demonstration.
//
// Shows the minimal use of Table: a multi-column table ("Item", "Value", "StdDev")
// redrawn as fast as possible, where only the value sprites are repainted after the
// initial draw (row labels and column headers are only drawn once). A heading is drawn
// once at startup, and a live update-rate readout is kept in the lower right corner
// (drawn as a plain DisplayValue, same as the other Basic_* sketches).
//
// Hardware: Any board with a display (e.g. Feather ESP32-S3 or Feather M0).
//

#include <array>

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include "Field.h"
#include "Table.h"
#include "DisplayValue.h"
#include "RollingRate.h"
#include "SerialX.h"
#include "Timer.h"
#include "Util.h"

// ----------- The Board
Arduino arduino;

// ----------- Text Sizes
constexpr uint8_t HEADER_SIZE = DEFAULT_HEADING_SIZE;
constexpr uint8_t CONTENT_TEXT_SIZE = DEFAULT_CONTENT_SIZE;
constexpr uint8_t FOOTER_TEXT_SIZE = 2;

// ----------- Content
std::array rows = {
   Table::Row("Speed"),
   Table::Row("Acceleration"),
   Table::Row("Position"),
};
std::array columns = {
   Table::Column("Item"),
   Table::Column("Value", "###.#"),
   Table::Column("StdDev", "#.##"),
};
Table table(&arduino, 0, 0, columns, rows, CONTENT_TEXT_SIZE);

// ----------- Rate readout (lower right)
constexpr uint16_t RATE_NUM_SAMPLES = 200;
RollingRate rate(RATE_NUM_SAMPLES);
Format rateFormat("####/s");
DisplayValue rateValue(&arduino, rateFormat, FOOTER_TEXT_SIZE, DisplayValue::Alignment::RIGHT);
Timer rateDisplayTimer(250);

void setup()
{
   SerialX::begin();
   arduino.begin();

   arduino.setCursor(0, 0);
   arduino.setTextSize(HEADER_SIZE);
   arduino.println("Table", Color::HEADING);

   int16_t top = arduino.getCursorY();
   int16_t bottom = arduino.height() - arduino.charH(FOOTER_TEXT_SIZE);
   int16_t availableHeight = bottom - top;

   int16_t tableWidth = table.getWidth();
   int16_t tableHeight = table.getRect().height;
   int16_t tableX = (int16_t)((arduino.width() - tableWidth) / 2);
   int16_t tableY = top + (int16_t)((availableHeight - tableHeight) / 2);
   table.setPosition(tableX, tableY);

   rateValue.setPosition((int16_t)arduino.width(), -arduino.charH(FOOTER_TEXT_SIZE));
}

void loop()
{
   for (uint8_t i = 0; i < table.rowCount(); i++)
   {
      table.setValue(i, 0, Util::randomValue(columns[1].format));
      table.setValue(i, 1, Util::randomValue(columns[2].format));
   }
   table.draw();

   rate.tick();
   if (rateDisplayTimer.ready())
   {
      rateValue.draw(rate.get(), Color::LIGHTGRAY);
   }
}
