//
// Captures finite sensor values for a fixed run window (or until the sample cap is reached), stores
// them in RAM, and reports serial/display summaries plus an optional serial dump.
//
// Detailed behavior:
// - The sketch is designed for repeatable calibration runs where sampling duration, interval, and
//   maximum sample count are controlled by constants near the top of the file.
// - A warm-up phase runs first so startup transients do not skew the captured dataset.
// - Sampling is driven by SensorCapture timing (default 1 ms cadence), and only finite values are
//   retained in RAM.
// - Capture ends automatically when either SAMPLING_DURATION_S elapses or MAX_SAMPLES is reached.
//
// Capture flow:
// 1) Initialize display, serial, and sensor, then allow a brief warm-up period.
// 2) Use a 1 ms timer to collect at most one sample per interval and store finite values in RAM.
// 3) Stop when MAX_SAMPLES are stored or SAMPLING_DURATION_S expires.
//
// Output flow:
// - Serial summary includes run metrics, value stats, and a value histogram.
// - Feather display renders a value histogram with min/max labels plus Sigma%/effective-rate rows
//   for multiple averaging window sizes.
// - Button A triggers a paced serial dump of stored points (index, value).
//
// Sensor mode:
// - Reads from TempSensor in Fahrenheit.
//
// Typical usage:
// - Flash the sketch and allow warm-up/capture to complete.
// - Review serial + display outputs to compare stability (StdDev%, range) across averaging sizes.
//
#include "Feather.h"
#include "Histogram.h"
#include "SensorCapture.h"
#include "SensorCaptureStats.h"
#include "SerialX.h"
#include "SerialHistogram.h"
#include "HistogramPlot.h"
#include <Wire.h>
#include "TestSensor.h"

constexpr float WARM_UP_S = 2.0f;
constexpr unsigned long SAMPLING_DURATION_S = 10;
constexpr unsigned long MAX_SAMPLE_RATE = 1000;
constexpr unsigned long SAMPLE_INTERVAL_MS =
   (MAX_SAMPLE_RATE == 0) ? 1UL :
   ((1000UL / MAX_SAMPLE_RATE) == 0 ? 1UL : (1000UL / MAX_SAMPLE_RATE));
constexpr size_t MAX_SAMPLES = 1000;
constexpr size_t HISTOGRAM_BINS = 20;
constexpr uint8_t CHART_MIN_MAX_SIGNIFICANT_DIGITS = 3;

constexpr size_t BUFFER_SIZES[] = { 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 };
constexpr size_t NUM_BUFFER_SIZES = sizeof(BUFFER_SIZES) / sizeof(BUFFER_SIZES[0]);

Feather feather;
TestSensor sensor;

SensorCapture sensorCapture(
   MAX_SAMPLES,
   SAMPLING_DURATION_S * 1000UL,
   SAMPLE_INTERVAL_MS);

bool captureFinalized = false;

//
// Draws a histogram panel on the Feather display.
//
void drawHistogramOnFeather(const char* title, const Histogram& histogram, int16_t sectionLeft, int16_t sectionWidth, int16_t sectionTop, int16_t sectionHeight, Color barColor)
{
   feather.setTextSize(2);
   feather.setCursor(sectionLeft, sectionTop);
   feather.println(title, Color::LABEL);

   int16_t chartTop = feather.getCursorY() + 1;
   int16_t adjustedHeight = sectionHeight - (chartTop - sectionTop);
   HistogramPlot plot(feather, histogram, sectionLeft, sectionWidth, chartTop, adjustedHeight, barColor, CHART_MIN_MAX_SIGNIFICANT_DIGITS);
   plot.render();
}

//
// Renders histogram and N/range analysis table on the Feather display.
//
void renderHistogramsOnFeather()
{
   feather.clearDisplay();

   Histogram valueHistogram(sensorCapture.values(), sensorCapture.count(), HISTOGRAM_BINS);
   SensorCaptureStats analysis(sensorCapture);

   feather.setTextSize(2);
   feather.setCursor(0, 0);
   String headerText = String(sensorCapture.count()) + " Samples";
   feather.println(headerText.c_str(), Color::HEADING);

   int16_t top = feather.getCursorY();
   int16_t messageHeight = feather.charH() + 2;
   int16_t availableHeight = (int16_t)feather.height() - top - messageHeight;
   int16_t totalWidth = (int16_t)feather.width();
   constexpr int16_t sectionGap = 5;
   constexpr int16_t tableWidth = 140;
   int16_t leftWidth = totalWidth - sectionGap - tableWidth;
   int16_t x = leftWidth + sectionGap;

   drawHistogramOnFeather("", valueHistogram, 0, leftWidth, top, availableHeight, Color::VALUE2);

   feather.setTextSize(2);
   Format numSamplesFormat("####", Format::Alignment::RIGHT);
   Format sigmaPercentFormat("##.##%", 6, Format::Alignment::RIGHT);
   Format hzFormat(" ####", Format::Alignment::RIGHT);

   auto computeEffectiveRateHz = [](size_t sampleSize) -> float
   {
      return (sampleSize > 0) ? (static_cast<float>(MAX_SAMPLE_RATE) / static_cast<float>(sampleSize)) : NAN;
   };

   Stats rawStats = analysis.computeBasicStats();
   float rawAvg = rawStats.get();
   float rawStdDev = rawStats.stdDev();
   size_t rawCount = rawStats.count();

   feather.setCursor(x, 0);
   feather.println("   N Sigma%   Hz", Color::VALUE3);

   feather.setCursorX(x);
   float rawSigmaPercent = NAN;
   if (isfinite(rawAvg) && (fabsf(rawAvg) > 0.0f) && isfinite(rawStdDev))
   {
      rawSigmaPercent = (rawStdDev / fabsf(rawAvg)) * 100.0f;
   }

   if ((rawCount > 0) && isfinite(rawSigmaPercent))
   {
      feather.print(" Raw", Color::LABEL);
      feather.print(rawSigmaPercent, sigmaPercentFormat, Color::VALUE2);
      feather.println(computeEffectiveRateHz(1), hzFormat, Color::VALUE2);
   }
   else
   {
      feather.println(" Raw   n/a   n/a", Color::GRAY);
   }

   constexpr size_t BUFFER_SIZE_FOR_DISPLAY[] = { 10, 20, 50, 100 };
   for (size_t i = 0; i < (sizeof(BUFFER_SIZE_FOR_DISPLAY) / sizeof(BUFFER_SIZE_FOR_DISPLAY[0])); i++)
   {
      feather.moveCursorY(-1);
      size_t sampleSize = BUFFER_SIZE_FOR_DISPLAY[i];
      float effectiveRateHz = computeEffectiveRateHz(sampleSize);

      Stats avgSeriesStats = analysis.computeAverageSeriesStats(sampleSize);
      float avgMean = avgSeriesStats.get();
      float avgStdDev = avgSeriesStats.stdDev();
      size_t averageCount = avgSeriesStats.count();

      float avgSigmaPercent = NAN;
      if (isfinite(avgMean) && (fabsf(avgMean) > 0.0f) && isfinite(avgStdDev))
      {
         avgSigmaPercent = (avgStdDev / fabsf(avgMean)) * 100.0f;
      }

      feather.setCursorX(x);
      if ((averageCount > 0) && isfinite(avgSigmaPercent))
      {
         feather.print(sampleSize, numSamplesFormat, Color::LABEL);
         feather.print(avgSigmaPercent, sigmaPercentFormat, Color::VALUE);
         feather.println(effectiveRateHz, hzFormat, Color::VALUE);
      }
      else
      {
         std::string rowText = numSamplesFormat.toString((double)sampleSize) + "   n/a   n/a";
         feather.println(rowText, Color::GRAY);
      }
   }

   feather.setTextSize(2);
   feather.setCursor(0, -feather.charH());
   feather.println("Button A to dump to Serial", Color::GRAY);
}

//
// Computes and prints capture statistics and histogram data to Serial.
//
void printCaptureSummary()
{
   SensorCaptureStats analysis(sensorCapture);
   Stats basicStats = analysis.computeBasicStats();

   float valueAvg = basicStats.get();
   float valueStdDev = basicStats.stdDev();
   float valueMin = basicStats.min();
   float valueMax = basicStats.max();
   float valueRange = analysis.computeRange(valueMin, valueMax);

   Serial.println();
   Serial.println("Capture Summary");
   SerialX::print("Capture ms", 20);
   SerialX::println(SAMPLING_DURATION_S * 1000UL, 20);
   SerialX::print("Samples", 20);
   SerialX::println(sensorCapture.count(), 20);
   SerialX::print("Sensor Avg", 20);
   SerialX::println(valueAvg, 3, 20);
   SerialX::print("Sensor StdDev", 20);
   SerialX::println(valueStdDev, 3, 20);
   SerialX::print("Sensor Min", 20);
   SerialX::println(valueMin, 3, 20);
   SerialX::print("Sensor Max", 20);
   SerialX::println(valueMax, 3, 20);
   SerialX::print("Sensor Range", 20);
   SerialX::println(valueRange, 3, 20);
   Serial.println();

   Histogram histogram(sensorCapture.values(), sensorCapture.count(), HISTOGRAM_BINS);
   SerialHistogram::print("Sensor Value Histogram", histogram, 3);

   sensorCapture.printFirstAndLastToSerial(10, 3);
}

//
// Prints post-capture block-average analysis for configured sample sizes.
//
void printAveragingAnalysis()
{
   Serial.println("Averaging Analysis");

   SensorCaptureStats analysis(sensorCapture);

   SerialX::print("Num Samples", 14);
   SerialX::print("Range", 12);
   SerialX::print("StdDev", 12);
   SerialX::print("StdDev%", 12);
   SerialX::println("Eff Rate", 10);

   SerialX::print("-----------", 14);
   SerialX::print("-----", 12);
   SerialX::print("------", 12);
   SerialX::print("-------", 12);
   SerialX::println("--------", 10);

   for (size_t analysisIndex = 0; analysisIndex < NUM_BUFFER_SIZES; analysisIndex++)
   {
      size_t sampleSize = BUFFER_SIZES[analysisIndex];
      float effectiveRate = (sampleSize > 0) ? (static_cast<float>(MAX_SAMPLE_RATE) / static_cast<float>(sampleSize)) : NAN;

      Stats avgSeriesStats = analysis.computeAverageSeriesStats(sampleSize);
      float avgRange = analysis.computeRange(avgSeriesStats.min(), avgSeriesStats.max());
      float avgStdDev = avgSeriesStats.stdDev();
      float avgMean = avgSeriesStats.get();
      size_t averageCount = avgSeriesStats.count();

      float avgStdDevPercent = NAN;
      if (isfinite(avgMean) && (fabsf(avgMean) > 0.0f) && isfinite(avgStdDev))
      {
         avgStdDevPercent = (avgStdDev / fabsf(avgMean)) * 100.0f;
      }

      SerialX::print(sampleSize, 14);
      if (averageCount == 0)
      {
         SerialX::print("n/a", 12);
         SerialX::print("n/a", 12);
         SerialX::print("n/a", 12);
      }
      else
      {
         SerialX::print(avgRange, 3, 12);
         SerialX::print(avgStdDev, 3, 12);
         SerialX::print(isfinite(avgStdDevPercent) ? String(avgStdDevPercent, 2) + "%" : "n/a", 12);
      }

      SerialX::println(isfinite(effectiveRate) ? String(effectiveRate, 1) + "/s" : "n/a", 10);
   }

   Serial.println();
}

//
// Updates the status text shown on the Feather display.
//
void updateDisplay()
{
   feather.setCursor(0, 0);
   feather.setTextSize(3);
   feather.println("Sensor Averaging", Color::HEADING);

   feather.setTextSize(2);
   if (!sensorCapture.isCaptureComplete())
   {
      feather.println("Collecting data", Color::VALUE);
      feather.println("Monitor Serial", Color::LABEL);
   }
   else
   {
      feather.println("Run complete", Color::VALUE);
      feather.println("Press A to dump", Color::SUB_LABEL);
   }
}

//
// Finalizes capture and prints summaries.
//
void finishCapture()
{
   if (captureFinalized)
   {
      return;
   }

   captureFinalized = true;
   printCaptureSummary();
   printAveragingAnalysis();
   renderHistogramsOnFeather();
}

void setup()
{
   SerialX::begin(115200, 2000);
   Wire.begin();
   feather.begin();

   sensor.begin();
   sensorCapture.reset();

   updateDisplay();
   Serial.println("Capture started...");
}

void loop()
{
   SensorCaptureState states = sensorCapture.update();

   if (sensorCapture.readyForValue())
   {
      float sensorValue = sensor.get();
      SensorCaptureState valueStates = sensorCapture.addValue(sensorValue);
      if ((valueStates & SENSOR_CAPTURE_STATE_COMPLETED) != 0)
      {
         finishCapture();
      }
   }

   if ((states & SENSOR_CAPTURE_STATE_COMPLETED) != 0)
   {
      finishCapture();
   }

   if (sensorCapture.isCaptureComplete() && feather.buttonA.wasPressed())
   {
      sensorCapture.dumpToSerial();
   }
}
