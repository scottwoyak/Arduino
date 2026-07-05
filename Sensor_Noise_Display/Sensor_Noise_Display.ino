//
// Combined sensor visualization sketch with three display modes: Scatter, Histogram, and Noise.
//
// Detailed behavior:
// - Press button A to cycle display modes.
// - Sampling runs continuously and finite readings are stored in both TimedValues (for scatter/histogram)
//   and TimedStats (for rolling noise metrics).
// - Scatter mode shows a rolling timed plot.
// - Histogram mode shows rolling value distribution.
// - Noise mode shows Sample Time, Samples, Avg, Range, StdDev, and StdDev%.
// - Serial output uses the Sensor_Noise metrics table at SERIAL_PRINT_INTERVAL_MS.
// 

#include <Arduino.h>
#include <Wire.h>
#include <math.h>

#include "Feather.h"
#include "SerialX.h"
#include "SerialTable.h"
#include "TimedStats.h"
#include "Timer.h"
#include "TestSensor.h"
#include "I2C.h"
#include "RollingRate.h"
#include "TimedScatterPlot.h"
#include "TimedHistogram.h"
#include "TimedHistogramPlot.h"
#include "TimedValues.h"

// ----------- Rates
constexpr uint16_t SENSOR_SAMPLE_RATE_PER_SEC = 100;
constexpr uint16_t DISPLAY_RATE_PER_SEC = 10;

// ----------- Intervals
constexpr uint16_t SERIAL_PRINT_INTERVAL_MS = 5000;

// ----------- History Windows
constexpr unsigned long SCATTER_HISTORY_PERIOD_S = 30;
constexpr unsigned long HISTOGRAM_HISTORY_PERIOD_S = 6;
constexpr unsigned long NOISE_HISTORY_S = 1;

// ----------- Histogram Settings
constexpr uint16_t HISTOGRAM_BIN_COUNT = 40;
constexpr float SENSOR_VALUE_RESOLUTION_F = 0.0049f;

Feather feather;
TestSensor sensor;
Timer sampleTimer(
   (SENSOR_SAMPLE_RATE_PER_SEC == 0) ? 0 :
   ((1000U / SENSOR_SAMPLE_RATE_PER_SEC) == 0 ? 1 : (1000U / SENSOR_SAMPLE_RATE_PER_SEC)));
Timer displayTimer(
   (DISPLAY_RATE_PER_SEC == 0) ? 0 :
   ((1000U / DISPLAY_RATE_PER_SEC) == 0 ? 1 : (1000U / DISPLAY_RATE_PER_SEC)));
Timer serialPrintTimer(SERIAL_PRINT_INTERVAL_MS);
RollingRate sampleRate;
TimedValues samples(SCATTER_HISTORY_PERIOD_S * 1000UL, 256);
TimedStats stats(NOISE_HISTORY_S * 1000UL);

bool serialHeaderPrinted = false;
uint16_t serialRowsPrinted = 0;

enum class DisplayMode : uint8_t
{
   Scatter,
   Histogram,
   Noise,
};

DisplayMode displayMode = DisplayMode::Scatter;

Format sampleRateFormat("###/s", Format::Alignment::RIGHT);
Format valueFormat("####.##");
Format rangeFormat("###.##");
Format stdDevFormat("####.##");
Format stdDevPercentFormat("##.##%", 7);
Format countFormat("######");
Format sampleTimeFormat("#### ms");

constexpr SerialTable::Column SERIAL_COLUMNS[] = {
   { "Num Samples", 14 },
   { "Avg", 12 },
   { "Range", 12 },
   { "StdDev", 12 },
   { "StdDev%", 10 },
};
SerialTable serialTable(nullptr, SERIAL_COLUMNS, sizeof(SERIAL_COLUMNS) / sizeof(SERIAL_COLUMNS[0]));

TimedScatterPlot scatterPlot(feather, samples, SCATTER_HISTORY_PERIOD_S * 1000UL, 0.0f);
TimedHistogram histogram(HISTOGRAM_BIN_COUNT, HISTOGRAM_HISTORY_PERIOD_S * 1000UL, SENSOR_VALUE_RESOLUTION_F);
TimedHistogramPlot histogramPlot(feather, histogram, samples);

void addSample(float value)
{
   sampleRate.tick();
   samples.set(value);
   stats.set(value);
}

void drawHeader()
{
   feather.setTextSize(2);
   feather.setCursor(0, 0);

   switch (displayMode)
   {
   case DisplayMode::Scatter:
      feather.println("Sensor Scatter", Color::HEADING);
      break;

   case DisplayMode::Histogram:
      feather.println("Histogram", Color::HEADING);
      break;

   case DisplayMode::Noise:
   default:
      feather.println("Sensor Noise", Color::HEADING);
      break;
   }
}

void printSerialSummary()
{
   Serial.println();
   Serial.println("Serial Metrics Summary");
   SerialX::println("- Sampling source: active sensor", 0);
   SerialX::println("- Data points are collected continuously", 0);
   SerialX::println(String("- Stats are averaged over: ") + (NOISE_HISTORY_S * 1000UL) + " ms (sliding period)", 0);
   Serial.println("- Num Samples: finite samples currently retained in the active stats period");
   Serial.println("- Avg: mean of samples in the active stats period");
   Serial.println("- Range: max-min of samples in the active stats period");
   Serial.println("- StdDev: population standard deviation in active period");
}

void printSerialValues(TimedStats& timedStats)
{
   float avg = timedStats.average();
   float minValue = timedStats.min();
   float maxValue = timedStats.max();
   float rng = (isfinite(minValue) && isfinite(maxValue)) ? (maxValue - minValue) : NAN;
   size_t count = timedStats.count();
   float sd = timedStats.stdDev();
   float sdPercent = (isfinite(avg) && (fabsf(avg) > 0.0f) && isfinite(sd)) ? ((sd / fabsf(avg)) * 100.0f) : NAN;

   if (!serialHeaderPrinted || (serialRowsPrinted % 10 == 0))
   {
      Serial.println();
      serialTable.printHeader();
      serialHeaderPrinted = true;
   }

   serialTable.printRow(
      count,
      SerialTable::fixed(avg, 3),
      SerialTable::fixed(rng, 3),
      SerialTable::fixed(sd, 3),
      isfinite(sdPercent) ? String(sdPercent, 2) + "%" : "n/a");

   serialRowsPrinted++;
}

void renderNoiseView()
{
   float avg = stats.average();
   float minValue = stats.min();
   float maxValue = stats.max();
   float rng = (isfinite(minValue) && isfinite(maxValue)) ? (maxValue - minValue) : NAN;
   size_t count = stats.count();
   float sd = stats.stdDev();
   float sdPercent = (isfinite(avg) && (fabsf(avg) > 0.0f) && isfinite(sd)) ? ((sd / fabsf(avg)) * 100.0f) : NAN;

   feather.setTextSize(2);
   int16_t sectionTop = feather.charH() + 5;
   int16_t rowHeight = feather.charH();

   feather.setCursor(0, sectionTop);
   feather.println("Sampling Time: ", NOISE_HISTORY_S * 1000UL, sampleTimeFormat, Color::VALUE);

   feather.setCursor(0, sectionTop + rowHeight);
   feather.println("      Samples: ", count, countFormat, Color::VALUE2);

   if (count == 0 || !isfinite(avg))
   {
      feather.setCursor(0, sectionTop + (rowHeight * 2));
      feather.println("  No valid data", Color::RED);
      feather.setCursor(0, sectionTop + (rowHeight * 3));
      feather.println("  Check sensor", Color::VALUE2);
      feather.setCursor(0, sectionTop + (rowHeight * 4));
      feather.println("  and I2C wiring", Color::VALUE2);
      return;
   }

   feather.setCursor(0, sectionTop + (rowHeight * 2));
   feather.println("          Avg: ", avg, valueFormat, Color::VALUE2);

   feather.setCursor(0, sectionTop + (rowHeight * 3));
   feather.println("        Range: ", rng, rangeFormat, Color::VALUE2);

   feather.setCursor(0, sectionTop + (rowHeight * 4));
   feather.print("       StdDev: ", Color::LABEL);

   String stdDevValue = String(stdDevFormat.toString(sd).c_str());
   stdDevValue.trim();

   if (isfinite(sdPercent))
   {
      String sdPercentValue = String(stdDevPercentFormat.toString(sdPercent).c_str());
      sdPercentValue.trim();
      feather.println(stdDevValue + ", " + sdPercentValue, Color::VALUE2);
   }
   else
   {
      feather.println(stdDevValue + ", n/a", Color::VALUE2);
   }
}

void setup()
{
   SerialX::begin();
   Wire.begin();
   feather.begin();

   feather.clearDisplay();
   drawHeader();

   bool sensorReady = sensor.begin();
   printSerialSummary();

   if (!sensorReady)
   {
      Serial.println("Temperature sensor initialization failed.");
      Serial.println("I2C scan:");
      I2C::scan();
   }

   float firstValue = sensor.get();
   if (!isfinite(firstValue))
   {
      Serial.println("Temperature sensor did not return a valid value.");
      Serial.println("I2C scan:");
      I2C::scan();
   }
}

void loop()
{
   if (feather.buttonA.wasPressed())
   {
      switch (displayMode)
      {
      case DisplayMode::Scatter:
         displayMode = DisplayMode::Histogram;
         break;

      case DisplayMode::Histogram:
         displayMode = DisplayMode::Noise;
         break;

      case DisplayMode::Noise:
      default:
         displayMode = DisplayMode::Scatter;
         break;
      }

      feather.clearDisplay();
      drawHeader();
   }

   if (sampleTimer.ready())
   {
      const float value = sensor.get();
      if (isfinite(value))
      {
         addSample(value);
      }
   }

   if (serialPrintTimer.ready())
   {
      printSerialValues(stats);
   }

   const bool shouldRender = (displayMode == DisplayMode::Scatter) || displayTimer.ready();
   if (!shouldRender)
   {
      return;
   }

   feather.setTextSize(2);
   feather.setCursor(0, 0);
   feather.printR("", sampleRate.get(), sampleRateFormat, Color::GRAY);

   switch (displayMode)
   {
   case DisplayMode::Scatter:
      scatterPlot.render();
      break;

   case DisplayMode::Histogram:
      histogramPlot.render();
      break;

   case DisplayMode::Noise:
   default:
      renderNoiseView();
      break;
   }
}

