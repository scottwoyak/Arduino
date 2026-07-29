//
// Basic FieldTable demonstration.
//
// Shows the minimal use of FieldTable: a single-column, COLON-aligned "label: value"
// table redrawn as fast as possible, grouped into two sections ("Motion" and
// "Environment"). Sections are their own rows in the table (see FieldTable::Row's
// section-header constructor and addSection()), counting toward row indices the same
// as data rows. Each data row is backed directly by a live float variable (see
// FieldTable::Row's label/formatStr/value constructor), so a single call to draw()
// reads every row's variable and only repaints rows whose value has actually changed
// (row labels and section headers are only drawn once). A heading is drawn once at
// startup, and a live update-rate readout is kept in the lower right corner (drawn as a
// plain DisplayValue, same as the other Basic_* sketches).
//
// Hardware: Any board with a display (e.g. Feather ESP32-S3 or Feather M0).
//

#include <array>

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include "DisplayValue.h"
#include "FieldTable.h"
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
float speed = 0;
float acceleration = 0;
float temperature = 0;
float humidity = 0;

std::array rows = {
   FieldTable::Row("Motion"),
   FieldTable::Row("Speed", "###.#", &speed),
   FieldTable::Row("Acceleration", "#.##", &acceleration),
   FieldTable::Row("Environment"),
   FieldTable::Row("Temperature", "###.#", &temperature),
   FieldTable::Row("Humidity", "###.#", &humidity),
};
FieldTable table(&arduino, 0, 0, rows, CONTENT_TEXT_SIZE);

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
   arduino.println("FieldTable", Color::HEADING);

   int16_t top = arduino.getCursorY();
   int16_t bottom = (int16_t)arduino.height() - arduino.charH(FOOTER_TEXT_SIZE);
   table.setPosition((int16_t)arduino.width() / 2, (top + bottom) / 2, Anchor::CENTER);

   rateValue.setPosition((int16_t)arduino.width(), -arduino.charH(FOOTER_TEXT_SIZE));
}

void loop()
{
   speed = Util::randomValue("###.#");
   acceleration = Util::randomValue("#.##");
   temperature = Util::randomValue("###.#");
   humidity = Util::randomValue("###.#");

   table.draw();

   rate.tick();
   if (rateDisplayTimer.ready())
   {
      rateValue.draw(rate.get(), Color::LIGHTGRAY);
   }
}
