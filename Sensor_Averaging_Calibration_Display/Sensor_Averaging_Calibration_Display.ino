// -------------------------------------------------------------------------------------------------
//
// Captures finite sensor values for a fixed run window (or until the sample cap is reached), stores
// them in RAM, and reports serial/display summaries plus an optional serial dump.
//
// Capture flow:
// 1) Initialize display, serial, and sensor, then allow a brief warm-up period.
// 2) Use a 1 ms timer to collect at most one sample per interval and store finite values in RAM.
// 3) Stop when MAX_SAMPLES are stored or SAMPLE_DURATION_MS expires.
//
// Output flow:
// - Serial summary includes run metrics, value stats, and a value histogram.
// - Feather display renders a value histogram with min/max labels.
// - Button A triggers a paced serial dump of stored points (index, value).
//
// Sensor modes:
// - Capacitor mode (default) reads from CapacitorSensor.
// - Temperature mode reads directly per loop iteration.
// -------------------------------------------------------------------------------------------------
#include "Feather.h"
#include "Histogram.h"
#include "SensorCapture.h"
#include "SensorCaptureStats.h"
#include "SensorCaptureOutput.h"
#include "SerialX.h"
#include "TestSensor.h"

#define SENSOR_TYPE_CAPACITOR 1
#define SENSOR_TYPE_TEMP 2
#define SENSOR_TYPE SENSOR_TYPE_CAPACITOR

// Warm-up delay before capture starts.
constexpr unsigned long WARM_UP_MS = 100;
// Total sampling window duration before capture auto-completes.
constexpr unsigned long SAMPLE_DURATION_MS = 10000;
// Sampling interval controlled by timer (one sample per interval).
constexpr unsigned long SAMPLE_INTERVAL_MS = 1;
// Maximum number of samples stored in RAM during a run.
constexpr size_t MAX_SAMPLES = 1000;
// Number of bins used for the value histogram.
constexpr size_t HISTOGRAM_BINS = 20;
// Significant digits used for chart min/max labels on the Feather display.
constexpr uint8_t CHART_MIN_MAX_SIGNIFICANT_DIGITS = 3;
// Allowed stabilization delta as a fraction of average signal level (0.25 at ~65 average => ~0.385%).
constexpr float STABILITY_DELTA_PERCENT = 0.01f;
// Number of consecutive stable samples required before capture storage begins.
constexpr size_t STABILITY_REQUIRED_SAMPLES = 10;
// Enables or disables stabilization gating before values are stored.
constexpr bool STABILIZATION_ENABLED = true;

constexpr size_t SAMPLE_SIZES[] = { 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 };
constexpr size_t NUM_SAMPLE_SIZES = sizeof(SAMPLE_SIZES) / sizeof(SAMPLE_SIZES[0]);

Feather feather;

#if SENSOR_TYPE == SENSOR_TYPE_CAPACITOR
// Digital pin used to charge the capacitor-based sensor.
constexpr uint8_t CHARGE_PIN = 5;
// Digital pin used to read/discharge the capacitor-based sensor.
constexpr uint8_t SENSE_PIN = 6;
// Discharge delay before each capacitor charge cycle.
constexpr uint16_t DISCHARGE_DELAY_MICROS = 350;
// Internal rolling-average window size used by CapacitorSensor.
constexpr size_t SENSOR_AVERAGE_SAMPLES = 1;
CapacitorTestSensor testSensor(CHARGE_PIN, SENSE_PIN, DISCHARGE_DELAY_MICROS, SENSOR_AVERAGE_SAMPLES);
#elif SENSOR_TYPE == SENSOR_TYPE_TEMP
TempTestSensor testSensor;
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

/// <summary>
/// Draws a histogram panel on the Feather display.
/// </summary>
/// <param name="title">Panel title.</param>
/// <param name="histogram">Histogram to render.</param>
/// <param name="sectionLeft">Left X coordinate of the panel.</param>
/// <param name="sectionWidth">Panel width in pixels.</param>
/// <param name="sectionTop">Top Y coordinate of the panel.</param>
/// <param name="sectionHeight">Panel height in pixels.</param>
/// <param name="barColor">Color used for bars and labels.</param>
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

/// <summary>
/// Renders histogram and N/range analysis table on the Feather display.
/// </summary>
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
   Format stdDevFormat("##.##", 6, Format::Alignment::RIGHT);
   Format hzFormat(" ####", Format::Alignment::RIGHT);

   auto computeEffectiveRateHz = [](size_t sampleSize) -> float
   {
      return (sampleSize > 0) ? (1000.0f / static_cast<float>(sampleSize * SAMPLE_INTERVAL_MS)) : NAN;
   };

   float rawAvg = NAN;
   float rawStdDev = NAN;
   float rawMin = NAN;
   float rawMax = NAN;
   size_t rawCount = 0;
   analysis.computeBasicStats(rawAvg, rawStdDev, rawMin, rawMax, rawCount);

   feather.setCursor(x, 0);
   feather.println("   N  Sigma   Hz", Color::VALUE3);

   feather.setCursorX(x);
   if ((rawCount > 0) && isfinite(rawStdDev))
   {
      feather.print(" Raw", Color::LABEL);
      feather.print(rawStdDev, stdDevFormat, Color::VALUE2);
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

      float avgRange = NAN;
      float avgStdDev = NAN;
      size_t averageCount = 0;
      analysis.computeAverageSeriesStats(
         sampleSize,
         avgRange,
         avgStdDev,
         averageCount);

      feather.setCursorX(x);
      if ((averageCount > 0) && isfinite(avgStdDev))
      {
         feather.print(sampleSize, numSamplesFormat, Color::LABEL);
         feather.print(avgStdDev, stdDevFormat, Color::VALUE);
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

/// <summary>
/// Computes and prints capture statistics and histogram data to Serial.
/// </summary>
void printCaptureSummary()
{
   SensorCaptureOutput::printCaptureSummary(sensorCapture, SAMPLE_DURATION_MS, 3);

   if ((sensorCapture.count() == 0) && !sensorCapture.isCaptureStabilized())
   {
      sensorCapture.printStabilizationDiagnosticsToSerial(3);
      Serial.println();
   }

   String valueHistogramTitle = String("Sensor Value Histogram (") + testSensor.unit() + ")";
   SensorCaptureOutput::printHistogramBins(valueHistogramTitle.c_str(), sensorCapture, HISTOGRAM_BINS, 3);

   sensorCapture.printFirstAndLastToSerial(10, 3);
}

/// <summary>
/// Prints post-capture block-average analysis for configured sample sizes.
/// </summary>
void printAveragingAnalysis()
{
   Serial.println("Averaging Analysis");

   SensorCaptureStats analysis(sensorCapture);

   SerialX::print("Size", 8);
   SerialX::print("Range", 12);
   SerialX::print("StdDev", 12);
   SerialX::println("Resulting Rate", 14);

   SerialX::print("-", 8);
   SerialX::print("---------", 12);
   SerialX::print("------", 12);
   SerialX::println("--------------", 14);

   for (size_t analysisIndex = 0; analysisIndex < NUM_SAMPLE_SIZES; analysisIndex++)
   {
      size_t sampleSize = SAMPLE_SIZES[analysisIndex];
      float effectiveRate = (sampleSize > 0) ? (1000.0f / static_cast<float>(sampleSize * SAMPLE_INTERVAL_MS)) : NAN;

      float avgRange = NAN;
      float avgStdDev = NAN;
      size_t averageCount = 0;
      analysis.computeAverageSeriesStats(
          sampleSize,
          avgRange,
          avgStdDev,
          averageCount);

      SerialX::print(sampleSize, 8);
      if (averageCount == 0)
      {
         SerialX::print("n/a", 12);
         SerialX::print("n/a", 12);
      }
      else
      {
         SerialX::print(avgRange, 3, 12);
         SerialX::print(avgStdDev, 3, 12);
      }

      SerialX::println(isfinite(effectiveRate) ? String(effectiveRate, 1) + "/s" : "n/a", 14);
   }

   Serial.println();
}

/// <summary>
/// Updates the status text shown on the Feather display.
/// </summary>
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

/// <summary>
/// Finalizes capture and prints summaries.
/// </summary>
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
   feather.begin();
   testSensor.begin();

   sensorCapture.startWarmUp();

   updateDisplay();
   Serial.println("Warm-up started...");
}

void loop()
{
   SensorCaptureState states = sensorCapture.update();

   if (sensorCapture.readyForValue())
   {
      SensorCaptureState valueStates = sensorCapture.addValue(testSensor.readValue());
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
