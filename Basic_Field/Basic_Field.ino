//
// Basic Field demonstration.
//
// Shows the minimal use of Field: a "label: value" pair redrawn as fast as
// possible, where only the value's off-screen sprite is repainted after the initial
// draw (the label is only drawn once). A heading is drawn once at startup, and a live
// update-rate readout is kept in the lower right corner (drawn as a plain DisplayValue,
// same as Basic_DisplayValue).
//
// Hardware: Any board with a display (e.g. Feather ESP32-S3 or Feather M0).
//

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include "Field.h"
#include "DisplayValue.h"
#include "RollingRate.h"
#include "SerialX.h"
#include "Timer.h"
#include "Util.h"

// ----------- The Board
Arduino arduino;

// ----------- Content
constexpr uint8_t FIELD_TEXT_SIZE = 3;
constexpr uint8_t NUM_FIELDS = 3;
Format fieldFormats[NUM_FIELDS] = { Format("###.#"), Format("###.##"), Format("#.##") };
const char* fieldLabels[NUM_FIELDS] = { "Speed", "Acceleration", "Position" };
Field fields[NUM_FIELDS] = {
   Field(&arduino, fieldLabels[0], fieldFormats[0], FIELD_TEXT_SIZE, Field::Alignment::COLON),
   Field(&arduino, fieldLabels[1], fieldFormats[1], FIELD_TEXT_SIZE, Field::Alignment::COLON),
   Field(&arduino, fieldLabels[2], fieldFormats[2], FIELD_TEXT_SIZE, Field::Alignment::COLON),
};

// ----------- Rate readout (lower right)
constexpr uint8_t FOOTER_TEXT_SIZE = 2;
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
   arduino.setTextSize(3);
   arduino.println("Field", Color::HEADING);

   int16_t groupHeight = NUM_FIELDS * arduino.charH(FIELD_TEXT_SIZE);
   int16_t top = arduino.getCursorY();
   int16_t bottom = arduino.height() - arduino.charH(FOOTER_TEXT_SIZE);
   int16_t availableHeight = bottom - top;
   int16_t groupTop = (int16_t)((availableHeight - groupHeight) / 2) + top;
   for (uint8_t i = 0; i < NUM_FIELDS; i++)
   {
      fields[i].setPosition(Point16(arduino.center().x, groupTop + i * arduino.charH(FIELD_TEXT_SIZE)));
   }

   rateValue.setPosition((int16_t)arduino.width(), -arduino.charH(FOOTER_TEXT_SIZE));
}

void loop()
{
   for (uint8_t i = 0; i < NUM_FIELDS; i++)
   {
      fields[i].draw(Util::randomValue(fieldFormats[i]));
   }

   rate.tick();
   if (rateDisplayTimer.ready())
   {
      rateValue.draw(rate.get(), Color::LIGHTGRAY);
   }
}
