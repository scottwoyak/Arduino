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
// - Capture ends automatically when either SAMPLE_DURATION_MS elapses or MAX_SAMPLES is reached.
//
// Capture flow:
// 1) Initialize display, serial, and sensor, then allow a brief warm-up period.
// 2) Use a 1 ms timer to collect at most one sample per interval and store finite values in RAM.
// 3) Stop when MAX_SAMPLES are stored or SAMPLE_DURATION_MS expires.
//
// Output flow:
// - Serial summary includes run metrics, value stats, and a value histogram.
// - Feather display renders a value histogram with min/max labels plus Sigma%/effective-rate rows
//   for multiple averaging window sizes.
// - Button A triggers a paced serial dump of stored points (index, value).
//
// Sensor modes:
// - Capacitor mode reads from CapacitorSensor.
// - Temperature mode reads from TempSensor.
//
// Typical usage:
// - Select SENSOR_TYPE at compile time.
// - Flash the sketch and allow warm-up/capture to complete.
// - Review serial + display outputs to compare stability (StdDev%, range) across averaging sizes.
//
#include "Feather.h"
#include "Histogram.h"
#include "SensorCapture.h"
#include "SensorCaptureStats.h"
#include "SensorCaptureOutput.h"
#include "SerialX.h"
#include <Wire.h>
#include "CapacitorSensor.h"
#include "TempSensor.h"

#define SENSOR_TYPE_CAPACITOR 1
#define SENSOR_TYPE_TEMP 2
#define SENSOR_TYPE SENSOR_TYPE_TEMP

constexpr unsigned long WARM_UP_MS = 100;
constexpr unsigned long SAMPLE_DURATION_MS = 10000;
constexpr unsigned long SAMPLE_INTERVAL_MS = 1;
constexpr size_t MAX_SAMPLES = 1000;
constexpr size_t HISTOGRAM_BINS = 20;
constexpr uint8_t CHART_MIN_MAX_SIGNIFICANT_DIGITS = 3;
constexpr float STABILITY_DELTA_PERCENT = 0.1f;
constexpr size_t STABILITY_REQUIRED_SAMPLES = 10;
constexpr bool STABILIZATION_ENABLED = true;

constexpr size_t SAMPLE_SIZES[] = { 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 };
constexpr size_t NUM_SAMPLE_SIZES = sizeof(SAMPLE_SIZES) / sizeof(SAMPLE_SIZES[0]);

Feather feather;

#if SENSOR_TYPE == SENSOR_TYPE_CAPACITOR
constexpr uint8_t SENSE_PIN = 5;
constexpr uint8_t CHARGE_PIN_1M = 6;
constexpr uint8_t CHARGE_PIN_470K = 9;
constexpr uint8_t CHARGE_PIN_100K = 10;
constexpr uint8_t CHARGE_PIN_47K = 11;

constexpr uint8_t CHARGE_PIN = CHARGE_PIN_1M;
constexpr uint16_t DISCHARGE_DELAY_MICROS = 350;
constexpr size_t SENSOR_AVERAGE_SAMPLES = 1;
constexpr const char* SENSOR_UNIT = "us";
CapacitorSensor sensor(CHARGE_PIN, SENSE_PIN, DISCHARGE_DELAY_MICROS, SENSOR_AVERAGE_SAMPLES);
#elif SENSOR_TYPE == SENSOR_TYPE_TEMP
constexpr const char* SENSOR_UNIT = "F";
TempSensor sensor;
#else
#error "Invalid SENSOR_TYPE. Use SENSOR_TYPE_CAPACITOR or SENSOR_TYPE_TEMP."
#endif

SensorCapture sensorCapture(
   MAX_SAMPLES,
   WARM_UP_MS,
   SAMPLE_DURATION_MS,
   SAMPLE_INTERVAL_MS,
   STABILITY_DELTA_PERCENT,
   STABILITY_REQUIRED_SAMPLES,
   STABILIZATION_ENABLED);

bool captureFinalized = false;

//
// Draws a histogram panel on the Feather display.
//
void drawHistogramOnFeather(const char* title, const Histogram<HISTOGRAM_BINS>& histogram, int16_t sectionLeft, int16_t sectionWidth, int16_t sectionTop, int16_t sectionHeight, Color barColor)
{
   SensorCaptureOutput::drawHistogramOnFeather(
      feather,
      title,
      histogram,
      sectionLeft,
      sectionWidth,
      sectionTop,
      sectionHeight,
      barColor,
      CHART_MIN_MAX_SIGNIFICANT_DIGITS);
}

//
// Renders histogram and N/range analysis table on the Feather display.
//
void renderHistogramsOnFeather()
{
   feather.clearDisplay();

   Histogram<HISTOGRAM_BINS> valueHistogram;
   SensorCaptureStats analysis(sensorCapture);
   analysis.buildHistogram(valueHistogram);

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
      return (sampleSize > 0) ? (1000.0f / static_cast<float>(sampleSize * SAMPLE_INTERVAL_MS)) : NAN;
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

   constexpr size_t SAMPLE_SIZE[] = { 10, 20, 50, 100 };
   for (size_t i = 0; i < (sizeof(SAMPLE_SIZE) / sizeof(SAMPLE_SIZE[0])); i++)
   {
      feather.moveCursorY(-1);
      size_t sampleSize = SAMPLE_SIZE[i];
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
   SensorCaptureOutput::printCaptureSummary(sensorCapture, SAMPLE_DURATION_MS, 3);

   if ((sensorCapture.count() == 0) && !sensorCapture.isCaptureStabilized())
   {
      sensorCapture.printStabilizationDiagnosticsToSerial(3);
      Serial.println();
   }

   String valueHistogramTitle = String("Sensor Value Histogram (") + SENSOR_UNIT + ")";
   SensorCaptureOutput::printHistogramBins(valueHistogramTitle.c_str(), sensorCapture, HISTOGRAM_BINS, 3);

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

   for (size_t analysisIndex = 0; analysisIndex < NUM_SAMPLE_SIZES; analysisIndex++)
   {
      size_t sampleSize = SAMPLE_SIZES[analysisIndex];
      float effectiveRate = (sampleSize > 0) ? (1000.0f / static_cast<float>(sampleSize * SAMPLE_INTERVAL_MS)) : NAN;

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
   feather.println("Sensor Capture", Color::HEADING);
   feather.moveCursorY(10);

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

#if SENSOR_TYPE == SENSOR_TYPE_CAPACITOR
   sensor.begin();
#else
   sensor.begin();
#endif

   sensorCapture.startWarmUp();

   updateDisplay();
   Serial.println("Warm-up started...");
}

void loop()
{
   SensorCaptureState states = sensorCapture.update();

       if (sensorCapture.readyForValue())
       {
   #if SENSOR_TYPE == SENSOR_TYPE_CAPACITOR
          float sensorValue = sensor.chargeTimeMicros();
   #else
          float sensorValue = sensor.readTemperatureF();
   #endif
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
