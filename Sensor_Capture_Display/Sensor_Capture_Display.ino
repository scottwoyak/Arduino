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
#include "SensorCaptureAnalysis.h"
#include "SensorCaptureOutput.h"
#include "SerialX.h"
#include "Timer.h"
#include "../libraries/Woyak/TestSensor.h"

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
// Delay between serial dump rows to avoid overwhelming the serial link.
constexpr unsigned long PRINT_INTERVAL_MS = 2;
// Allowed stabilization delta as a fraction of average signal level (0.25 at ~65 average => ~0.385%).
constexpr float STABILITY_DELTA_PERCENT = 0.00385f;
// Number of consecutive stable samples required before capture storage begins.
constexpr size_t STABILITY_REQUIRED_SAMPLES = 5;

constexpr size_t ANALYSIS_WINDOW_SIZES[] = { 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 };
constexpr size_t ANALYSIS_WINDOW_COUNT = sizeof(ANALYSIS_WINDOW_SIZES) / sizeof(ANALYSIS_WINDOW_SIZES[0]);

#if SENSOR_TYPE == SENSOR_TYPE_CAPACITOR
// Digital pin used to charge the capacitor-based sensor.
constexpr uint8_t CHARGE_PIN = 5;
// Digital pin used to read/discharge the capacitor-based sensor.
constexpr uint8_t SENSE_PIN = 6;
// Discharge delay before each capacitor charge cycle.
constexpr uint16_t DISCHARGE_DELAY_MICROS = 350;
#endif

Feather feather;

#if SENSOR_TYPE == SENSOR_TYPE_CAPACITOR
CapacitorTestSensor testSensor(CHARGE_PIN, SENSE_PIN, DISCHARGE_DELAY_MICROS);
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
   STABILITY_REQUIRED_SAMPLES);

bool captureFinalized = false;

/// <summary>
/// Builds a value histogram from captured samples.
/// </summary>
/// <param name="histogram">Histogram instance to populate.</param>
void buildValueHistogram(Histogram<HISTOGRAM_BINS>& histogram)
{
   SensorCaptureAnalysis analysis(sensorCapture);
   analysis.buildHistogram(histogram);
}

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
/// Renders the capture histogram summary on the Feather display.
/// </summary>
void renderHistogramsOnFeather()
{
   feather.clearDisplay();

   Histogram<HISTOGRAM_BINS> valueHistogram;
   buildValueHistogram(valueHistogram);

   feather.setTextSize(3);
   feather.setCursor(0, 0);
   feather.println("Results", Color::HEADING);

   feather.setTextSize(2);
   int16_t top = feather.getCursorY() + 2;
   int16_t messageHeight = feather.charH() + 2;
   int16_t availableHeight = (int16_t)feather.height() - top - messageHeight;
   int16_t totalWidth = (int16_t)feather.width();

   drawHistogramOnFeather("Values", valueHistogram, 0, totalWidth, top, availableHeight, Color::VALUE2);

   feather.setCursor(0, top + availableHeight + 1);
   feather.println("Button A to dump to Serial", Color::GRAY);
}

/// <summary>
/// Computes and prints capture statistics and histogram data to Serial.
/// </summary>
void printCaptureSummary()
{
   float valueAvg = NAN;
   float valueStdDev = NAN;
   float valueMin = NAN;
   float valueMax = NAN;
   size_t valueCount = 0;

   SensorCaptureAnalysis analysis(sensorCapture);
   analysis.computeBasicStats(
      valueAvg,
      valueStdDev,
      valueMin,
      valueMax,
      valueCount);

   String sensorMetric = String("Sensor value (") + testSensor.unit() + ")";

   Serial.println();
   Serial.println("Capture Summary");
   SerialX::print("Metric", 20);
   SerialX::println("Value", 20);
   SerialX::print("Capture ms", 20);
   SerialX::println((float)SAMPLE_DURATION_MS, 3, 20);
   SerialX::print("Samples", 20);
   SerialX::println((unsigned long)sensorCapture.count(), 20);
   Serial.println();

   SerialX::print("Metric", 20);
   SerialX::print("Avg", 12);
   SerialX::print("StdDev", 12);
   SerialX::print("Min", 12);
   SerialX::print("Max", 12);
   SerialX::println("Range", 12);
   SensorCaptureOutput::printStatsRow(sensorMetric.c_str(), valueAvg, valueStdDev, valueMin, valueMax, 3);
   Serial.println();

   if ((sensorCapture.count() == 0) && !sensorCapture.isCaptureStabilized())
   {
      sensorCapture.printStabilizationDiagnosticsToSerial(3);
      Serial.println();
   }

   String valueHistogramTitle = String("Sensor Value Histogram (") + testSensor.unit() + ")";
   Histogram<HISTOGRAM_BINS> valueHistogram;
   buildValueHistogram(valueHistogram);
   SensorCaptureOutput::printHistogramBins(valueHistogramTitle.c_str(), valueHistogram, 3);

   sensorCapture.printFirstAndLastToSerial(10, 3);
}

/// <summary>
/// Prints post-capture block-average analysis for configured sample sizes.
/// </summary>
void printWindowAnalysis()
{
   float rawAvg = NAN;
   float rawStdDev = NAN;
   float rawMin = NAN;
   float rawMax = NAN;
   size_t rawCount = 0;
   SensorCaptureAnalysis analysis(sensorCapture);
   analysis.computeBasicStats(
      rawAvg,
      rawStdDev,
      rawMin,
      rawMax,
      rawCount);
   float rawRange = analysis.computeRange(rawMin, rawMax);

   Serial.println("Window Analysis");
   SerialX::print("Raw N", 10);
   SerialX::print("Raw Range", 12);
   SerialX::println("Raw StdDev", 12);
   SerialX::print((unsigned long)rawCount, 10);
   SerialX::print(rawRange, 3, 12);
   SerialX::println(rawStdDev, 3, 12);
   Serial.println();

   SerialX::print("N", 8);
   SerialX::print("Rate", 10);
   SerialX::print("Range", 12);
   SerialX::println("StdDev", 12);

   SerialX::print("-", 8);
   SerialX::print("----", 10);
   SerialX::print("---------", 12);
   SerialX::println("------", 12);

   for (size_t analysisIndex = 0; analysisIndex < ANALYSIS_WINDOW_COUNT; analysisIndex++)
   {
      size_t windowSize = ANALYSIS_WINDOW_SIZES[analysisIndex];
      float effectiveRate = (windowSize > 0) ? (1000.0f / static_cast<float>(windowSize * SAMPLE_INTERVAL_MS)) : NAN;

      float avgRange = NAN;
      float avgStdDev = NAN;
      size_t averageCount = 0;
      analysis.computeAverageSeriesStats(
         windowSize,
         avgRange,
         avgStdDev,
         averageCount);

      SerialX::print((unsigned long)windowSize, 8);
      SerialX::print(isfinite(effectiveRate) ? String(effectiveRate, 1) + "/s" : "n/a", 10);
      if (averageCount == 0)
      {
         SerialX::print("n/a", 12);
         SerialX::println("n/a", 12);
      }
      else
      {
         SerialX::print(avgRange, 3, 12);
         SerialX::println(avgStdDev, 3, 12);
      }
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
   printWindowAnalysis();
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
   if ((states & SENSOR_CAPTURE_STATE_CAPTURE_STARTED) != 0)
   {
      Serial.println("Sampling started...");
      updateDisplay();
   }

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
      sensorCapture.dumpToSerial(PRINT_INTERVAL_MS);
   }
}
