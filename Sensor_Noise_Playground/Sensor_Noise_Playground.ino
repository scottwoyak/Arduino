//
// Combined sensor visualization sketch with four display modes: Scatter, Histogram, Noise, and Rate Only.
//
// Turn Encoder A to cycle display modes. Turn Encoder B to change the target sample rate.
// Press Button A to reset the board for a fresh test run.
// Sampling runs continuously and finite readings are stored in both TimedValues (for
// scatter/histogram) and TimedStats (for rolling noise metrics).
// - Scatter mode shows a rolling timed plot of recent values.
// - Histogram mode shows the rolling value distribution.
// - Noise mode shows Sampling Time, Num Samples Collected, Avg, Range, StdDev, and StdDev%.
// - Rate Only mode draws nothing besides the target/actual rate readout, so it can be
//   used to measure the actual achievable sensor sample rate without any rendering overhead.
//

#include <Arduino.h>
#include <Wire.h>
#include <math.h>

#include "EnumSelector.h"
#include "ESP32_S3_Playground.h"
#include "DisplayField.h"
#include "DisplayTable.h"
#include "RollingRate.h"
#include "TestSensor.h"
#include "TimedHistogram.h"
#include "TimedHistogramPlot.h"
#include "TimedRate.h"
#include "TimedScatterPlot.h"
#include "TimedStats.h"
#include "TimedValues.h"
#include "Timer.h"
#include "Util.h"

// ----------- The Board
ESP32_S3_Playground arduino;
TestSensor sensor;

// ----------- History Windows
constexpr unsigned long SCATTER_HISTORY_PERIOD_S = 30;
constexpr unsigned long HISTOGRAM_HISTORY_PERIOD_S = 6;
constexpr unsigned long NOISE_HISTORY_S = 1;
TimedValues samples(SCATTER_HISTORY_PERIOD_S * 1000UL, 256);
TimedStats stats(NOISE_HISTORY_S * 1000UL);

// ----------- Display Geometry
// Matches the ESP32_S3_Playground board's display in landscape orientation.
constexpr uint16_t DISPLAY_WIDTH = 480;
constexpr uint16_t DISPLAY_HEIGHT = 320;
// The title (size 3) and the Target Rate/Actual Rate readout (2 lines, size 2)
// are drawn side by side, not stacked, so the header only needs to be as tall as the taller
// of the two: the 2-line rate readout.
constexpr uint16_t HEADER_HEIGHT = 2 * 2 * 8 + 4; // 2 info lines (size 2) plus padding
constexpr uint16_t SUB_HEADING_GAP = 10; // extra blank space below the sensor type sub heading
constexpr uint16_t HEADER_GAP = 4; // blank space left below the header before content starts
const Rect16 CONTENT_RECT{ 0, HEADER_HEIGHT + SUB_HEADING_GAP + HEADER_GAP, DISPLAY_WIDTH, static_cast<uint16_t>(DISPLAY_HEIGHT - HEADER_HEIGHT - SUB_HEADING_GAP - HEADER_GAP) };

// ----------- Sampling
constexpr uint16_t TARGET_SAMPLE_RATES[] = {
   1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100,
   200, 300, 400, 500, 600, 700, 800, 900, 1000, 1500, 2000
};
constexpr uint16_t NUM_TARGET_SAMPLE_RATES = sizeof(TARGET_SAMPLE_RATES) / sizeof(TARGET_SAMPLE_RATES[0]);
constexpr uint16_t DEFAULT_SAMPLE_RATE_INDEX = 18; // 100/s
constexpr unsigned long MIN_ELAPSED_MS_FOR_ACTUAL_RATE = 500; // actual rate reads "---" until this much time has elapsed
TimerMicros sampleTimer(1000000UL / TARGET_SAMPLE_RATES[DEFAULT_SAMPLE_RATE_INDEX]);
TimedRate sampleRate(1000UL);

uint16_t sampleRateIndex = DEFAULT_SAMPLE_RATE_INDEX;

// ----------- Display Items
constexpr uint16_t DISPLAY_RATE_PER_SEC = 10;
constexpr uint16_t HISTOGRAM_BIN_COUNT = 40;
constexpr float SENSOR_VALUE_RESOLUTION_F = 0.0049f;
Timer displayTimer(1000UL / DISPLAY_RATE_PER_SEC);
Format rateFormat("####/s", Format::Alignment::RIGHT);
Format countFormat("######");
Format sampleTimeFormat("#### ms");
Format stdDevPercentFormat("##.##%", 7);
DisplayTable noiseTable(&arduino, CONTENT_RECT.x, CONTENT_RECT.y, 2);
DisplayField* targetRateField = nullptr;
DisplayField* actualRateField = nullptr;
TimedScatterPlot scatterPlot(&arduino, CONTENT_RECT, SCATTER_HISTORY_PERIOD_S * 1000UL, 0.0f);
TimedScatterPlotSeries* scatterSeries = nullptr;
TimedHistogram histogram(HISTOGRAM_BIN_COUNT, HISTOGRAM_HISTORY_PERIOD_S * 1000UL, SENSOR_VALUE_RESOLUTION_F);
TimedHistogramPlot histogramPlot(&arduino, histogram, samples, CONTENT_RECT, "###");

enum class DisplayMode : uint8_t
{
   Scatter,
   Histogram,
   Table,
   RateOnly,
};

EnumSelector<DisplayMode> displayModeSelector(arduino.encoderA, DisplayMode::RateOnly, DisplayMode::Scatter);

///
/// <summary>
/// Adds a sample to both the rolling samples window and the rolling stats window.
/// </summary>
/// <param name="value">Temperature sample value in Fahrenheit.</param>
///
void addSample(float value)
{
   sampleRate.tick();
   samples.set(value);
   stats.set(value);
   ASSERT(scatterSeries != nullptr);
   scatterSeries->add(value);
}

///
/// <summary>
/// Draws the display header showing the current display mode.
/// </summary>
///
void drawHeader()
{
   arduino.setTextSize(3);
   arduino.setCursor(0, 0);
   arduino.println("Sensor Noise", Color::HEADING);

   arduino.setTextSize(2);
   arduino.print("Sensor Type: ", Color::GRAY);
   arduino.println(sensor.sensorType(), Color::GRAY);

   if (displayModeSelector.value() == DisplayMode::RateOnly)
   {
      arduino.setTextSize(2);
      arduino.setCursor(0, CONTENT_RECT.y);
      arduino.println("This view doesn't display anything so that we", Color::VALUE2);
      arduino.println("can more accurately measure the actual sensor", Color::VALUE2);
      arduino.println("sample rate.", Color::VALUE2);
   }
}

///
/// <summary>
/// Renders the noise view's per-frame values: sample count, average, range, and
/// standard deviation (both absolute and as a percentage of the mean) through the shared
/// noiseTable, which only redraws a row's value when it actually changes.
/// </summary>
///
void renderTableView()
{
   float avg = stats.average();
   float minValue = stats.min();
   float maxValue = stats.max();
   float rng = (isfinite(minValue) && isfinite(maxValue)) ? (maxValue - minValue) : NAN;
   size_t count = stats.count();
   float sd = stats.stdDev();
   float sdPercent = (isfinite(avg) && (fabsf(avg) > 0.0f) && isfinite(sd)) ? ((sd / fabsf(avg)) * 100.0f) : NAN;

   noiseTable.updateValue(0, NOISE_HISTORY_S * 1000UL);
   noiseTable.updateValue(1, count);

   if (count == 0 || !isfinite(avg))
   {
      noiseTable.updateValue(2, "No valid data", Color::RED);
      noiseTable.updateValue(3, "Check sensor");
      noiseTable.updateValue(4, "and I2C", Color::VALUE2);
      noiseTable.updateValue(5, "wiring", Color::VALUE2);
      noiseTable.draw();
      return;
   }

   noiseTable.updateValue(2, avg);
   noiseTable.updateValue(3, rng);
   noiseTable.updateValue(4, sd);

   if (isfinite(sdPercent))
   {
      noiseTable.updateValue(5, sdPercent);
   }
   else
   {
      noiseTable.updateValue(5, "n/a", Color::VALUE2);
   }

   noiseTable.draw();
}

///
/// <summary>
/// Applies the encoder limits/position for the target sample rate, so turning encoderB
/// resumes from its current value.
/// </summary>
///
void applySampleRateLimits()
{
   arduino.encoderB.reset();
   arduino.encoderB.setLimits(0, NUM_TARGET_SAMPLE_RATES - 1);
   arduino.encoderB.setPosition(sampleRateIndex);
}

void setup()
{
   Wire.begin();
   arduino.begin();

   applySampleRateLimits();

   scatterSeries = scatterPlot.createSeries(0);
   scatterSeries->showMovingAverage = true;
   scatterSeries->showStdDevBand = true;
   scatterPlot.setMinMaxFormat(*sensor.getFormat());
   histogramPlot.setMinMaxFormat(*sensor.getFormat());

   noiseTable.addRow("Sampling Time", sampleTimeFormat, Color::LABEL, Color::VALUE2);
   noiseTable.addRow("Num Samples Collected", countFormat, Color::LABEL, Color::VALUE2);
   noiseTable.addRow("Avg", *sensor.getHighResFormat(), Color::LABEL, Color::VALUE2);
   noiseTable.addRow("Range", *sensor.getHighResFormat(), Color::LABEL, Color::VALUE2);
   noiseTable.addRow("StdDev", *sensor.getHighResFormat(), Color::LABEL, Color::VALUE2);
   noiseTable.addRow("StdDev%", stdDevPercentFormat, Color::LABEL, Color::VALUE2);

   arduino.setTextSize(2);
   std::string label = "Target Sampling Rate";
   int x = arduino.display.width() - (label.length() + 2  + rateFormat.length()) * arduino.charW();
   targetRateField = new DisplayField(&arduino, x, 0, label.c_str(), rateFormat, 2);

   label = "Actual Rate";
   x = arduino.display.width() - (label.length() + 2  +  rateFormat.length()) * arduino.charW();
   actualRateField = new DisplayField(&arduino, x, arduino.charH(), label.c_str(), rateFormat, 2);

   sensor.begin();
   sensor.get();

   arduino.clearDisplay();
   drawHeader();
}

void loop()
{
   if (arduino.buttonA.wasPressed())
   {
      Util::reset();
   }

   if (displayModeSelector.hasChanged())
   {
      arduino.fillRect(CONTENT_RECT.x, CONTENT_RECT.y, CONTENT_RECT.width, CONTENT_RECT.height, Color::BLACK);
      noiseTable.invalidate();
      scatterPlot.invalidate();
      drawHeader();
      sampleRate.reset();
   }

   sampleRateIndex = static_cast<uint16_t>(arduino.encoderB.getPosition());

   const unsigned long effectiveSamplePeriodUs = 1000000UL / TARGET_SAMPLE_RATES[sampleRateIndex];

   if (sampleTimer.getDurationMs() != effectiveSamplePeriodUs)
   {
      sampleTimer.setDurationMs(effectiveSamplePeriodUs);
   }

   if (sampleTimer.ready() && sensor.hasNewValue())
   {
      const float value = sensor.get();
      if (isfinite(value))
      {
         addSample(value);
      }
   }

   if (!displayTimer.ready())
   {
      return;
   }

   const uint16_t targetSampleRate = TARGET_SAMPLE_RATES[sampleRateIndex];

   arduino.setTextSize(2);
   targetRateField->draw(targetSampleRate);

   if (sampleRate.getElapsedMs() >= MIN_ELAPSED_MS_FOR_ACTUAL_RATE)
   {
      actualRateField->draw(sampleRate.get());
   }
   else
   {
      actualRateField->draw("---");
   }

   switch (displayModeSelector.value())
   {
   case DisplayMode::Scatter:
      scatterPlot.render();
      break;

   case DisplayMode::Histogram:
      histogramPlot.render();
      break;

   case DisplayMode::Table:
      renderTableView();
      break;

   case DisplayMode::RateOnly:
   default:
      // Nothing to render each loop; the static explanation was drawn once in drawHeader()
      // so that only the Target/Actual Rate readout above updates per frame.
      break;
   }
}
