//
// Basic ScatterPlot demonstration.
//
// Shows the minimal use of ScatterPlot: a single rolling-count series (see
// ScatterPlotSeries::setRollingCount()) is populated with random samples forever - once it
// reaches 1000 points, each new sample overwrites the oldest one in place (like an
// oscilloscope trace) instead of clearing/restarting the chart, so the display never goes
// blank and the X axis never changes (avoiding flicker from a shifting axis range). A
// "ScatterPlot" heading is drawn once at startup, and a live update-rate readout is kept in
// the upper right corner, with the plot filling the remaining space below it.
//
// Hardware: Any board with a display (e.g. Feather ESP32-S3 or Feather M0).
//

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include "DisplayValue.h"
#include "RollingRate.h"
#include "ScatterPlot.h"
#include "SerialX.h"
#include "Timer.h"
#include "Util.h"

// ----------- The Board
Arduino arduino;

// ----------- Content
constexpr size_t NUM_SAMPLES = 1000;
constexpr const char* X_AXIS_FORMAT = "####";
constexpr const char* VALUE_FORMAT = "###.#";
ScatterPlot scatterPlot(&arduino, Rect16{}, X_AXIS_FORMAT, VALUE_FORMAT);
ScatterPlotSeries* series = scatterPlot.createRollingSeries(NUM_SAMPLES);

// ----------- Rate readout (upper right)
constexpr uint8_t RATE_TEXT_SIZE = 2;
constexpr uint16_t RATE_NUM_SAMPLES = 100;
RollingRate rate(RATE_NUM_SAMPLES);
Format rateFormat("####/s");
DisplayValue rateValue(&arduino, rateFormat, RATE_TEXT_SIZE, DisplayValue::Alignment::RIGHT);
Timer rateDisplayTimer(250);

void setup()
{
   SerialX::begin();
   Serial.println("Hello from Basic_ScatterPlot_Display!");

   arduino.begin();

   arduino.setCursor(0, 0);
   arduino.setTextSize(DEFAULT_HEADING_SIZE);
   arduino.println("ScatterPlot", Color::HEADING);

   int16_t top = arduino.charH(DEFAULT_HEADING_SIZE);
   scatterPlot.setRect(0, top, arduino.width(), arduino.height() - top);

   rateValue.setPosition(arduino.width(), 0);
}

void loop()
{
   series->add(Util::randomValue(VALUE_FORMAT));

   scatterPlot.draw();

   rate.tick();
   if (rateDisplayTimer.ready())
   {
      rateValue.draw(rate.get(), Color::LIGHTGRAY);
   }
}
