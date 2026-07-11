//
// Measures TimedScatterPlot rendering performance under varying sample input rates.
//
// A RandomTestSensor supplies values that are added to a TimedValues buffer at a target
// rate controlled live via the rotary encoder. TimedScatterPlot::render() is called as
// fast as possible every loop iteration, and its lastComputeMicros()/lastDisplayMicros()
// timings, along with the achieved render rate, show how render performance holds up as
// the input sample rate changes.
//
// Turn the rotary encoder to change the target sample rate (Hz), in steps of HZ_STEP.
// Press the encoder button to reset the rolling rate measurements and sample history,
// giving a clean baseline for the current rate.
//
// Serial output logs target Hz, achieved sample rate, achieved render rate, and the
// render timing breakdown (compute/display microseconds) once per second. The display
// shows the same live readout above the scatter plot.
//

// System/standard library headers
#include <Wire.h>

// Local library headers (from libraries/Woyak)
#include "ESP32_S3_Playground.h"
#include "RollingRate.h"
#include "SerialTable.h"
#include "SerialX.h"
#include "TestSensor.h"
#include "Timer.h"
#include "TimedScatterPlot.h"
#include "TimedValues.h"

// ----------- Test Parameters
constexpr unsigned long HISTORY_MS = 10000;  // 10 second visible history window
constexpr unsigned long MOVING_AVERAGE_MS = 1000;  // moving average window width
constexpr uint16_t HZ_STEP = 10;             // Hz change per encoder click
constexpr uint16_t MIN_HZ = 10;
constexpr uint16_t MAX_HZ = 2000;
constexpr uint16_t DEFAULT_HZ = 100;

// ----------- Display Geometry
// Matches the ESP32_S3_Playground board's display in landscape orientation.
constexpr uint16_t DISPLAY_WIDTH = 480;
constexpr uint16_t DISPLAY_HEIGHT = 320;
constexpr uint16_t HEADER_HEIGHT = 3 * 8 + 3 * 2 * 8 + 6; // title (size 3) + 3 info lines (size 2) plus padding

// ----------- Serial Output
constexpr uint8_t SERIAL_PRINT_RATE_PER_SEC = 1;
constexpr SerialTable::Column RESULT_COLUMNS[] = {
   { "Target(Hz)", 12 },
   { "Sample(/s)", 12 },
   { "Render(/s)", 12 },
   { "Compute(us)", 13 },
   { "Display(us)", 13 },
};
constexpr size_t NUM_RESULT_COLUMNS = sizeof(RESULT_COLUMNS) / sizeof(RESULT_COLUMNS[0]);

// ----------- The Board
ESP32_S3_Playground arduino;
RandomTestSensor sensor;

// ----------- Display Formats
Format hzFormat("####", Format::Alignment::RIGHT);
Format rateFormat("#####/s", Format::Alignment::RIGHT);
Format microsFormat("######us", Format::Alignment::RIGHT);

// ----------- Test State
TimedScatterPlot plot(&arduino, Rect16{ 0, HEADER_HEIGHT, DISPLAY_WIDTH, static_cast<uint16_t>(DISPLAY_HEIGHT - HEADER_HEIGHT) }, HISTORY_MS);
TimedScatterPlotSeries* series = nullptr;
RollingRate sampleRate;
RollingRate renderRate;
RateTimer readoutTimer(10);
RateTimer serialPrintTimer(SERIAL_PRINT_RATE_PER_SEC);
SerialTable resultTable("Timed Scatter Plot Render Performance", RESULT_COLUMNS, NUM_RESULT_COLUMNS);

uint16_t targetHz = DEFAULT_HZ;
unsigned long lastSampleMs = 0;
int16_t headerY = 0;
bool sensorReady = false;

///
/// <summary>
/// Clears the display and draws the sketch title, recording the Y position of the
/// info lines drawn below it.
/// </summary>
///
void drawTitle()
{
   arduino.setTextSize(3);
   arduino.clearDisplay();
   arduino.println("Timed ScatterPlot", Color::HEADING);

   headerY = arduino.getCursorY();
}

///
/// <summary>
/// Redraws the live target rate, achieved sample/render rates, and render timing
/// breakdown. The fixed-width formats overwrite their own background in place, so the
/// rows do not need to be cleared first and do not flicker.
/// </summary>
///
void updateReadout()
{
   arduino.setTextSize(2);
   arduino.setCursor(0, headerY);

   arduino.print("Sensor Rate  Target: ", targetHz, hzFormat, Color::VALUE);
   arduino.println(" Hz  Actual: ", sampleRate.get(), rateFormat, Color::VALUE2);

   arduino.print("Render Rate: ", renderRate.get(), rateFormat, Color::VALUE);
   arduino.println("   ", Color::LABEL);

   arduino.print("Compute: ", plot.lastComputeMicros(), microsFormat, Color::VALUE);
   arduino.println("  Display: ", plot.lastDisplayMicros(), microsFormat, Color::VALUE2);
}

///
/// <summary>
/// Prints one row of target Hz, achieved rates, and render timing to serial.
/// </summary>
///
void printSerialRow()
{
   resultTable.printRow(
      targetHz,
      SerialTable::fixed(sampleRate.get(), 1),
      SerialTable::fixed(renderRate.get(), 1),
      plot.lastComputeMicros(),
      plot.lastDisplayMicros());
}

void setup()
{
   SerialX::begin();
   Wire.begin();

   arduino.begin();

   arduino.encoderA.setLimits(MIN_HZ / HZ_STEP, MAX_HZ / HZ_STEP);
   arduino.encoderA.setPosition(DEFAULT_HZ / HZ_STEP);

   drawTitle();

   series = plot.createSeries();
   series->color = Color::GREEN;
   series->showMovingAverage = true;
   series->movingAverageWindowMs = MOVING_AVERAGE_MS;

   sensorReady = sensor.begin();
   if (!sensorReady)
   {
      arduino.setTextSize(2);
      arduino.setCursor(0, headerY);
      arduino.println("Sensor init failed", Color::RED);
      Serial.println("Error: sensor initialization failed");
      return;
   }

   resultTable.printHeader();
}

void loop()
{
   if (!sensorReady)
   {
      return;
   }

   // Reset the measurements for a clean baseline at the current rate
   if (arduino.encoderA.button.wasPressed())
   {
      series->clear();
      sampleRate.reset();
      renderRate.reset();
   }

   // The rotary encoder directly sets the target sampling rate in Hz
   targetHz = static_cast<uint16_t>(arduino.encoderA.getPosition()) * HZ_STEP;

   // Manually pace sample acquisition at the encoder-selected rate. A fixed-duration
   // Timer doesn't fit here since the interval changes continuously as the encoder turns.
   unsigned long intervalMs = 1000UL / targetHz;
   unsigned long nowMs = millis();
   if ((nowMs - lastSampleMs) >= intervalMs)
   {
      lastSampleMs = nowMs;

      float value = sensor.get();
      if (isfinite(value))
      {
         series->add(value);
         sampleRate.tick();
      }
   }

   // Render as fast as possible to measure the plot's sustainable refresh rate
   renderRate.tick();
   plot.render();

   if (readoutTimer.ready())
   {
      updateReadout();
   }

   if (serialPrintTimer.ready())
   {
      printSerialRow();
   }
}
