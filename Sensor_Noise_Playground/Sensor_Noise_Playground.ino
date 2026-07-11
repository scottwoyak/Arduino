//
// Combined sensor visualization sketch with three display modes: Scatter, Histogram, and Noise.
//
// Press Button A to cycle display modes. Sampling runs continuously and finite readings are stored
// in both TimedValues (for scatter/histogram) and TimedStats (for rolling noise metrics).
// - Scatter mode shows a rolling timed plot of recent values.
// - Histogram mode shows the rolling value distribution.
// - Noise mode shows Sample Time, Samples, Avg, Range, StdDev, and StdDev%.
//
// Serial output prints a Sensor_Noise metrics table every SERIAL_PRINT_INTERVAL_MS.
//

#include <Arduino.h>
#include <Wire.h>
#include <math.h>

#include "ESP32_S3_Playground.h"
#include "RollingRate.h"
#include "SerialTable.h"
#include "SerialX.h"
#include "TempSensor.h"
#include "TimedHistogram.h"
#include "TimedHistogramPlot.h"
#include "TimedScatterPlot.h"
#include "TimedStats.h"
#include "TimedValues.h"
#include "Timer.h"

// ----------- The Board
ESP32_S3_Playground arduino;
TempSensor sensor;

// ----------- History Windows
constexpr unsigned long SCATTER_HISTORY_PERIOD_S = 30;
constexpr unsigned long HISTOGRAM_HISTORY_PERIOD_S = 6;
constexpr unsigned long NOISE_HISTORY_S = 1;
TimedValues samples(SCATTER_HISTORY_PERIOD_S * 1000UL, 256);
TimedStats stats(NOISE_HISTORY_S * 1000UL);

// ----------- Sampling
constexpr uint16_t TARGET_SAMPLE_RATES[] = {
   1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100
};
constexpr uint16_t NUM_TARGET_SAMPLE_RATES = sizeof(TARGET_SAMPLE_RATES) / sizeof(TARGET_SAMPLE_RATES[0]);
constexpr uint16_t DEFAULT_SAMPLE_RATE_INDEX = NUM_TARGET_SAMPLE_RATES - 1; // 100/s
Timer sampleTimer(1000UL / TARGET_SAMPLE_RATES[DEFAULT_SAMPLE_RATE_INDEX]);
RollingRate sampleRate;

// ----------- Display Items
constexpr uint16_t DISPLAY_RATE_PER_SEC = 10;
constexpr uint16_t HISTOGRAM_BIN_COUNT = 40;
constexpr float SENSOR_VALUE_RESOLUTION_F = 0.0049f;
Timer displayTimer(1000UL / DISPLAY_RATE_PER_SEC);
Format sampleRateFormat("###/s", Format::Alignment::RIGHT);
Format targetSampleRateFormat("###/s", Format::Alignment::RIGHT);
Format valueFormat("####.##");
Format rangeFormat("###.##");
Format stdDevFormat("####.##");
Format stdDevPercentFormat("##.##%", 7);
Format countFormat("######");
Format sampleTimeFormat("#### ms");
TimedScatterPlot scatterPlot(&arduino, samples, SCATTER_HISTORY_PERIOD_S * 1000UL, 0.0f);
TimedHistogram histogram(HISTOGRAM_BIN_COUNT, HISTOGRAM_HISTORY_PERIOD_S * 1000UL, SENSOR_VALUE_RESOLUTION_F);
TimedHistogramPlot histogramPlot(&arduino, histogram, samples);

enum class DisplayMode : uint8_t
{
   Scatter,
   Histogram,
   Noise,
};

DisplayMode displayMode = DisplayMode::Scatter;

// ----------- Serial Output
constexpr uint16_t SERIAL_PRINT_INTERVAL_MS = 5000;
Timer serialPrintTimer(SERIAL_PRINT_INTERVAL_MS);
bool serialHeaderPrinted = false;
uint16_t serialRowsPrinted = 0;
constexpr SerialTable::Column SERIAL_COLUMNS[] = {
   { "Num Samples", 14 },
   { "Avg", 12 },
   { "Range", 12 },
   { "StdDev", 12 },
   { "StdDev%", 10 },
};
SerialTable serialTable(nullptr, SERIAL_COLUMNS, sizeof(SERIAL_COLUMNS) / sizeof(SERIAL_COLUMNS[0]));

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

   switch (displayMode)
   {
   case DisplayMode::Scatter:
      arduino.println("Sensor Noise", Color::HEADING);
      break;

   case DisplayMode::Histogram:
      arduino.println("Histogram", Color::HEADING);
      break;

   case DisplayMode::Noise:
   default:
      arduino.println("Sensor Noise", Color::HEADING);
      break;
   }
}

///
/// <summary>
/// Prints a summary of the serial output to explain what data is being collected and reported.
/// </summary>
///
void printSerialSummary()
{
   Serial.println();
   Serial.println("Serial Metrics Summary");
   Serial.println("- Sampling source: active sensor");
   Serial.println("- Data points are collected continuously");
   Serial.println(String("- Stats are averaged over: ") + (NOISE_HISTORY_S * 1000UL) + " ms (sliding period)");
   Serial.println("- Num Samples: finite samples currently retained in the active stats period");
   Serial.println("- Avg: mean of samples in the active stats period");
   Serial.println("- Range: max-min of samples in the active stats period");
   Serial.println("- StdDev: population standard deviation in active period");
}

///
/// <summary>
/// Prints a row of sensor statistics to the serial table.
/// </summary>
/// <param name="timedStats">The stats object to print values from.</param>
///
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

///
/// <summary>
/// Renders the noise view: a detailed statistical display showing sample count, average,
/// range, and standard deviation (both absolute and as a percentage of the mean).
/// </summary>
///
void renderNoiseView()
{
   float avg = stats.average();
   float minValue = stats.min();
   float maxValue = stats.max();
   float rng = (isfinite(minValue) && isfinite(maxValue)) ? (maxValue - minValue) : NAN;
   size_t count = stats.count();
   float sd = stats.stdDev();
   float sdPercent = (isfinite(avg) && (fabsf(avg) > 0.0f) && isfinite(sd)) ? ((sd / fabsf(avg)) * 100.0f) : NAN;

   arduino.setTextSize(2);
   int16_t sectionTop = arduino.charH() + 5;
   int16_t rowHeight = arduino.charH();

   arduino.setCursor(0, sectionTop);
   arduino.println("Sampling Time: ", NOISE_HISTORY_S * 1000UL, sampleTimeFormat, Color::VALUE);

   arduino.setCursor(0, sectionTop + rowHeight);
   arduino.println("      Samples: ", count, countFormat, Color::VALUE2);

   if (count == 0 || !isfinite(avg))
   {
      arduino.setCursor(0, sectionTop + (rowHeight * 2));
      arduino.println("  No valid data", Color::RED);
      arduino.setCursor(0, sectionTop + (rowHeight * 3));
      arduino.println("  Check sensor", Color::VALUE2);
      arduino.setCursor(0, sectionTop + (rowHeight * 4));
      arduino.println("  and I2C wiring", Color::VALUE2);
      return;
   }

   arduino.setCursor(0, sectionTop + (rowHeight * 2));
   arduino.println("          Avg: ", avg, valueFormat, Color::VALUE2);

   arduino.setCursor(0, sectionTop + (rowHeight * 3));
   arduino.println("        Range: ", rng, rangeFormat, Color::VALUE2);

   arduino.setCursor(0, sectionTop + (rowHeight * 4));
   arduino.print("       StdDev: ", Color::LABEL);

   String stdDevValue = String(stdDevFormat.toString(sd).c_str());
   stdDevValue.trim();

   if (isfinite(sdPercent))
   {
      String sdPercentValue = String(stdDevPercentFormat.toString(sdPercent).c_str());
      sdPercentValue.trim();
      arduino.println(stdDevValue + ", " + sdPercentValue, Color::VALUE2);
   }
   else
   {
      arduino.println(stdDevValue + ", n/a", Color::VALUE2);
   }
}

void setup()
{
   SerialX::begin();
   Wire.begin();
   arduino.begin();

   arduino.encoderA.setLimits(0, NUM_TARGET_SAMPLE_RATES - 1);
   arduino.encoderA.setPosition(DEFAULT_SAMPLE_RATE_INDEX);

   arduino.clearDisplay();
   drawHeader();

   bool sensorReady = sensor.begin();
   printSerialSummary();

   if (!sensorReady)
   {
      Serial.println("Temperature sensor initialization failed.");
   }

   float firstValue = sensor.readTemperatureF();
   if (!isfinite(firstValue))
   {
      Serial.println("Temperature sensor did not return a valid value.");
   }
}

void loop()
{
   if (arduino.encoderA.button.wasPressed())
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

      arduino.clearDisplay();
      drawHeader();
   }

   const uint16_t targetSampleRate = TARGET_SAMPLE_RATES[arduino.encoderA.getPosition()];
   const unsigned long targetSampleIntervalMs = 1000UL / targetSampleRate;
   if (sampleTimer.getDuration() != targetSampleIntervalMs)
   {
      sampleTimer.setDuration(targetSampleIntervalMs);
   }

   if (sampleTimer.ready())
   {
      const float value = sensor.readTemperatureF();
      if (isfinite(value))
      {
         addSample(value);
      }
   }

   if (serialPrintTimer.ready())
   {
      printSerialValues(stats);
   }

   if (!displayTimer.ready())
   {
      return;
   }

   arduino.setTextSize(2);
   arduino.setCursor(0, 0);
   arduino.printR("", static_cast<float>(targetSampleRate), targetSampleRateFormat, Color::GRAY);
   arduino.setCursor(0, arduino.charH());
   arduino.printR("", sampleRate.get(), sampleRateFormat, Color::GRAY);

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
