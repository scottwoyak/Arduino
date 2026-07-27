//
// Basic DisplayValue demonstration.
//
// Shows the minimal use of DisplayValue: a single formatted number redrawn as fast as
// possible using its own off-screen sprite, so only the value's small region repaints
// each frame instead of the whole display. A heading is drawn once at startup, and a
// live update-rate readout is kept in the lower right corner.
//
// Hardware: Any board with a display (e.g. Feather ESP32-S3 or Feather M0).
//

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include "DisplayValue.h"
#include "RollingRate.h"
#include "SerialX.h"
#include "Timer.h"
#include "Util.h"

// ----------- The Board
Arduino arduino;

// ----------- Content
constexpr uint8_t VALUES_TEXT_SIZE = 3;
constexpr uint8_t NUM_VALUES = 3;
Format valueFormats[NUM_VALUES] = { Format("###.#"), Format("###.##"), Format("##.#")};
DisplayValue* values[NUM_VALUES];

// ----------- Rate readout (lower right)
constexpr uint8_t RATE_TEXT_SIZE = 2;
constexpr uint16_t RATE_NUM_SAMPLES = 100;
RollingRate rate(RATE_NUM_SAMPLES);
Format rateFormat("####/s");
DisplayValue rateValue(&arduino, rateFormat, RATE_TEXT_SIZE, DisplayValue::Alignment::RIGHT);
Timer rateDisplayTimer(250);

void setup()
{
   SerialX::begin();
   Serial.println("Hello from Basic_DisplayValue_Display!");

   arduino.begin();

   arduino.setCursor(0, 0);
   arduino.setTextSize(DEFAULT_HEADING_SIZE);
   arduino.println("DisplayValue", Color::HEADING);

   int16_t groupHeight = NUM_VALUES* arduino.charH(VALUES_TEXT_SIZE);
   int16_t top = arduino.charH(DEFAULT_HEADING_SIZE);
   int16_t bottom = arduino.height() - arduino.charH(RATE_TEXT_SIZE);
   int16_t availableHeight = bottom - top;
   int16_t groupTop = (int16_t)((availableHeight - groupHeight) / 2) + top;
   for (uint8_t i = 0; i < NUM_VALUES; i++)
   {
      values[i] = new DisplayValue(&arduino, valueFormats[i], VALUES_TEXT_SIZE, DisplayValue::Alignment::DECIMAL);
      values[i]->setPosition(arduino.center().x, groupTop + i * arduino.charH(VALUES_TEXT_SIZE));
   }

   rateValue.setPosition(arduino.width(), -arduino.charH(RATE_TEXT_SIZE));
}

void loop()
{
   for (uint8_t i = 0; i < NUM_VALUES; i++)
   {
      values[i]->draw(Util::randomValue(valueFormats[i]));
   }

   rate.tick();
   if (rateDisplayTimer.ready())
   {
      rateValue.draw(rate.get(), Color::LIGHTGRAY);
   }
}
