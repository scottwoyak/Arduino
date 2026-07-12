//
// Captures finite sensor values for a fixed run window (or until the sample cap is reached), stores
// them in RAM, and reports serial/display summaries.
//
// On startup, a prompt screen is shown where Encoder A adjusts the target sample rate (starting at
// 100/s); steps are 10/s below 100/s and 100/s at or above 100/s. Press Encoder A's button to start
// the capture using the selected rate.
//
// Capture flow:
// 1) Initialize display, serial, and sensor.
// 2) Prompt for the target sample rate via Encoder A; start capture on button press.
// 3) Sample at up to the selected target rate and store finite values in RAM.
// 4) Stop when MAX_SAMPLES are stored or SAMPLING_DURATION_S elapses.
//
// Output flow:
// - While collecting, the Playground display shows live capture progress plus target/actual sample rate.
// - After capture, the display renders a value histogram with min/max labels plus Sigma%/effective-rate
//   rows for multiple averaging window sizes, along with the target/actual sample rate.
// - Serial summary includes run metrics, value stats, target/actual sample rate, and a value histogram.
//
// Sensor mode:
// - Reads from TestSensor (configurable via TestSensor.h using alias).
//
// Typical usage: flash the sketch, dial in the desired sample rate with Encoder A, press Encoder A's
// button to start, and allow capture to complete, then review serial and display outputs to compare
// stability (StdDev%, range) across averaging sizes.
//

#include "ESP32_S3_Playground.h"
#include "Histogram.h"
#include "SensorCapture.h"
#include "Timer.h"
#include "SerialTable.h"
#include "SerialX.h"
#include "SerialHistogram.h"
#include "HistogramPlot.h"
#include "ScatterPlot.h"
#include "DisplayTable.h"
#include "RollingRate.h"
#include "Util.h"
#include <Wire.h>
#include "TestSensor.h"

// ----------- The Board
ESP32_S3_Playground arduino;
TestSensor sensor;

// ----------- Capture Settings
constexpr unsigned long DEFAULT_SAMPLING_DURATION_S = 10;
constexpr uint16_t DEFAULT_SAMPLE_RATE_PER_SEC = 100;
constexpr uint16_t MIN_SAMPLE_RATE_PER_SEC = 10;
constexpr uint16_t MAX_SAMPLE_RATE_PER_SEC = 1000;
constexpr uint16_t SAMPLE_RATE_STEP_LOW = 10;
constexpr uint16_t SAMPLE_RATE_STEP_HIGH = 100;
constexpr size_t DEFAULT_MAX_SAMPLES = 1000;
constexpr size_t MIN_MAX_SAMPLES = 100;
constexpr size_t MAX_MAX_SAMPLES = 5000;
constexpr size_t SAMPLE_STEP = 100;
constexpr unsigned long MIN_SAMPLING_DURATION_S = 5;
constexpr unsigned long MAX_SAMPLING_DURATION_S = 60;
constexpr unsigned long DURATION_STEP_S = 5;
constexpr unsigned long DEFAULT_WARMUP_PERIOD_S = 0;
constexpr unsigned long MAX_WARMUP_PERIOD_S = 30;
constexpr unsigned long WARMUP_STEP_S = 1;

// ----------- Preferences Keys
constexpr const char* PREF_NAMESPACE = "sensor_avg";
constexpr const char* PREF_RATE_KEY = "rate";
constexpr const char* PREF_SAMPLES_KEY = "samples";
constexpr const char* PREF_DURATION_KEY = "duration";
constexpr const char* PREF_WARMUP_KEY = "warmup";

uint16_t targetSampleRate = DEFAULT_SAMPLE_RATE_PER_SEC;
size_t maxSamples = DEFAULT_MAX_SAMPLES;
unsigned long samplingDurationS = DEFAULT_SAMPLING_DURATION_S;
unsigned long warmupPeriodS = DEFAULT_WARMUP_PERIOD_S;
RollingRate actualSampleRate;

// Setup page selection state
enum SetupItem { SAMPLE_RATE = 0, MAX_SAMPLES_ITEM = 1, SAMPLING_DURATION = 2, WARMUP_PERIOD = 3 };
SetupItem selectedSetupItem = SAMPLE_RATE;

SensorCapture* sensorCapture = nullptr;
SensorCapture* warmupCapture = nullptr;
bool captureStarted = false;
bool captureFinalized = false;
unsigned long captureStartMs = 0;
bool warmupActive = false;
unsigned long warmupStartMs = 0;

// Boundary-diagnostics buffers: last 10 warmup samples (circular) and first 10 real samples.
constexpr uint8_t NUM_BOUNDARY_SAMPLES = 10;
unsigned long warmupBoundaryTimestampsMs[NUM_BOUNDARY_SAMPLES];
float warmupBoundaryValues[NUM_BOUNDARY_SAMPLES];
uint8_t warmupBoundaryCount = 0;
uint8_t warmupBoundaryNextIndex = 0;
unsigned long realBoundaryTimestampsMs[NUM_BOUNDARY_SAMPLES];
float realBoundaryValues[NUM_BOUNDARY_SAMPLES];
uint8_t realBoundaryCount = 0;
bool boundaryDumpPrinted = false;

///
/// <summary>
/// Records one warmup sample timestamp/value into the circular boundary-diagnostics buffer.
/// </summary>
/// <param name="timestampMs">Sample timestamp in milliseconds.</param>
/// <param name="value">Sample value.</param>
///
void recordWarmupBoundarySample(unsigned long timestampMs, float value)
{
   warmupBoundaryTimestampsMs[warmupBoundaryNextIndex] = timestampMs;
   warmupBoundaryValues[warmupBoundaryNextIndex] = value;
   warmupBoundaryNextIndex = (warmupBoundaryNextIndex + 1) % NUM_BOUNDARY_SAMPLES;
   if (warmupBoundaryCount < NUM_BOUNDARY_SAMPLES)
   {
      warmupBoundaryCount++;
   }
}

///
/// <summary>
/// Records one real (post-warmup) sample timestamp/value, then prints the boundary diagnostics
/// table once the first 10 real samples have been collected.
/// </summary>
/// <param name="timestampMs">Sample timestamp in milliseconds.</param>
/// <param name="value">Sample value.</param>
///
void recordRealBoundarySample(unsigned long timestampMs, float value)
{
   if (realBoundaryCount < NUM_BOUNDARY_SAMPLES)
   {
      realBoundaryTimestampsMs[realBoundaryCount] = timestampMs;
      realBoundaryValues[realBoundaryCount] = value;
      realBoundaryCount++;
   }

   if ((realBoundaryCount >= NUM_BOUNDARY_SAMPLES) && !boundaryDumpPrinted)
   {
      boundaryDumpPrinted = true;

      static const SerialTable::Column columns[] = {
         { "Set", 8 },
         { "Time (ms)", 12 },
         { "Value", 10 },
      };
      SerialTable table("Warmup/Real Boundary Sample Dump", columns, 3);
      table.printHeader();

      uint8_t oldestIndex = (warmupBoundaryCount < NUM_BOUNDARY_SAMPLES)
         ? 0
         : warmupBoundaryNextIndex;
      for (uint8_t i = 0; i < warmupBoundaryCount; i++)
      {
         uint8_t index = (oldestIndex + i) % NUM_BOUNDARY_SAMPLES;
         table.printRow("warmup", warmupBoundaryTimestampsMs[index], SerialTable::fixed(warmupBoundaryValues[index], 3));
      }

      for (uint8_t i = 0; i < realBoundaryCount; i++)
      {
         table.printRow("real", realBoundaryTimestampsMs[i], SerialTable::fixed(realBoundaryValues[i], 3));
      }
   }
}

///
/// <summary>
/// Creates a new SensorCapture with current settings.
/// </summary>
///
void createSensorCapture()
{
   if (sensorCapture != nullptr)
   {
      delete sensorCapture;
   }
   sensorCapture = new SensorCapture(
      maxSamples,
      samplingDurationS * 1000UL,
      1000UL / targetSampleRate);
}

///
/// <summary>
/// Creates a new SensorCapture used to record warmup-phase samples for display only;
/// these samples are excluded from the histogram and statistics.
/// </summary>
///
void createWarmupCapture()
{
   if (warmupCapture != nullptr)
   {
      delete warmupCapture;
      warmupCapture = nullptr;
   }

   if (warmupPeriodS > 0)
   {
      size_t warmupMaxValues = static_cast<size_t>(warmupPeriodS) * targetSampleRate + 1;
      warmupCapture = new SensorCapture(
         warmupMaxValues,
         warmupPeriodS * 1000UL,
         1000UL / targetSampleRate);
   }
}

///
/// <summary>
/// Persists the current setup settings (sample rate, max samples, sampling duration, warmup period) to Preferences.
/// </summary>
///
void saveSetupSettings()
{
   arduino.preferences.begin(PREF_NAMESPACE, false);
   arduino.preferences.putUShort(PREF_RATE_KEY, targetSampleRate);
   arduino.preferences.putULong(PREF_SAMPLES_KEY, static_cast<uint32_t>(maxSamples));
   arduino.preferences.putULong(PREF_DURATION_KEY, samplingDurationS);
   arduino.preferences.putULong(PREF_WARMUP_KEY, warmupPeriodS);
   arduino.preferences.end();
}

///
/// <summary>
/// Loads setup settings from Preferences, falling back to defaults for any missing values.
/// </summary>
///
void loadSetupSettings()
{
   arduino.preferences.begin(PREF_NAMESPACE, true);
   targetSampleRate = arduino.preferences.getUShort(PREF_RATE_KEY, DEFAULT_SAMPLE_RATE_PER_SEC);
   maxSamples = static_cast<size_t>(arduino.preferences.getULong(PREF_SAMPLES_KEY, DEFAULT_MAX_SAMPLES));
   samplingDurationS = arduino.preferences.getULong(PREF_DURATION_KEY, DEFAULT_SAMPLING_DURATION_S);
   warmupPeriodS = arduino.preferences.getULong(PREF_WARMUP_KEY, DEFAULT_WARMUP_PERIOD_S);
   arduino.preferences.end();
}

///
/// <summary>
/// Resets setup settings to their defaults and persists them, overwriting any saved values.
/// </summary>
///
void resetSetupSettings()
{
   targetSampleRate = DEFAULT_SAMPLE_RATE_PER_SEC;
   maxSamples = DEFAULT_MAX_SAMPLES;
   samplingDurationS = DEFAULT_SAMPLING_DURATION_S;
   warmupPeriodS = DEFAULT_WARMUP_PERIOD_S;
   saveSetupSettings();
}

// ----------- Display Items
constexpr uint16_t DISPLAY_UPDATE_RATE_PER_SEC = 2;
RateTimer displayRefreshTimer(DISPLAY_UPDATE_RATE_PER_SEC);
bool collectingViewInitialized = false;
bool promptViewInitialized = false;
Format progressPercentFormat("###%", Format::Alignment::LEFT);
Format maxCaptureFormat(20, Format::Alignment::LEFT);
Format targetRateFormat("####/s", Format::Alignment::LEFT);
Format actualRateFormat("####.#/s", Format::Alignment::LEFT);
Format warmupStatusFormat(20, Format::Alignment::LEFT);
DisplayTable collectingTable(&arduino, 0, 0);
ScatterPlot* resultsScatterPlot = nullptr;

// ----------- Analysis Settings
constexpr size_t HISTOGRAM_BINS = 20;
constexpr uint8_t CHART_MIN_MAX_SIGNIFICANT_DIGITS = 3;
constexpr size_t BUFFER_SIZES[] = { 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 };
constexpr size_t NUM_BUFFER_SIZES = sizeof(BUFFER_SIZES) / sizeof(BUFFER_SIZES[0]);

/// <summary>
/// Draws a histogram panel on the Playground display.
/// </summary>
/// <param name="title">Title text drawn above the panel, or empty for none.</param>
/// <param name="histogram">Histogram data to render.</param>
/// <param name="sectionLeft">Left X coordinate of the panel.</param>
/// <param name="sectionWidth">Width of the panel in pixels.</param>
/// <param name="sectionTop">Top Y coordinate of the panel.</param>
/// <param name="sectionHeight">Height of the panel in pixels.</param>
/// <param name="barColor">Color used to draw histogram bars.</param>
/// <param name="axisLabelColor">Color used to draw axis labels (min/max text).</param>
void drawHistogram(const char* title, const Histogram& histogram, int16_t sectionLeft, int16_t sectionWidth, int16_t sectionTop, int16_t sectionHeight, Color barColor, Color axisLabelColor)
{
   arduino.setTextSize(2);
   arduino.setCursor(sectionLeft, sectionTop);
   arduino.println(title, Color::LABEL);

   int16_t chartTop = arduino.getCursorY() + 1;
   int16_t adjustedHeight = sectionHeight - (chartTop - sectionTop);
   HistogramPlot plot(&arduino, histogram, sectionLeft, sectionWidth, chartTop, adjustedHeight, barColor, CHART_MIN_MAX_SIGNIFICANT_DIGITS, axisLabelColor);
   plot.render();
}

/// <summary>
/// Computes the effective rate for a given averaging block size using the current target sample rate.
/// </summary>
/// <param name="sampleSize">Averaging block size (N).</param>
/// <returns>Effective rate in samples/sec, or NaN when sampleSize is zero.</returns>
float computeEffectiveRateHz(size_t sampleSize)
{
   return (sampleSize > 0) ? (static_cast<float>(targetSampleRate) / static_cast<float>(sampleSize)) : NAN;
}

///
/// <summary>
/// Draws a scatter plot of captured sensor values (sample index vs. value) on the Playground display.
/// Warmup-phase samples (if any) are drawn as a separate gray series before the retained samples,
/// which are drawn in the given point color.
/// </summary>
/// <param name="sectionLeft">Left X coordinate of the panel.</param>
/// <param name="sectionWidth">Width of the panel in pixels.</param>
/// <param name="sectionTop">Top Y coordinate of the panel.</param>
/// <param name="sectionHeight">Height of the panel in pixels.</param>
/// <param name="pointColor">Color used to draw retained (post-warmup) scatter points.</param>
///
void drawScatterPlot(int16_t sectionLeft, int16_t sectionWidth, int16_t sectionTop, int16_t sectionHeight, Color pointColor)
{
   if (resultsScatterPlot != nullptr)
   {
      delete resultsScatterPlot;
   }
   resultsScatterPlot = new ScatterPlot(&arduino, sectionLeft, sectionTop, sectionWidth, sectionHeight);

   size_t warmupCount = (warmupCapture != nullptr) ? warmupCapture->count() : 0;
   const float* warmupValues = (warmupCapture != nullptr) ? warmupCapture->values() : nullptr;

   size_t count = sensorCapture->count();
   const float* values = sensorCapture->values();

   if (warmupCount > 0)
   {
      ScatterPlotSeries* warmupSeries = resultsScatterPlot->createSeries(warmupCount);
      warmupSeries->showPoints = true;
      warmupSeries->showLines = false;
      warmupSeries->color = Color::LIGHTGRAY;

      for (size_t i = 0; i < warmupCount; i++)
      {
         warmupSeries->add((float)i, warmupValues[i]);
      }
      warmupSeries->finalized = true;
   }

   ScatterPlotSeries* series = resultsScatterPlot->createSeries((count > 0) ? count : 1);
   series->showPoints = true;
   series->showLines = false;
   series->color = pointColor;

   for (size_t i = 0; i < count; i++)
   {
      series->add((float)(warmupCount + i), values[i]);
   }
   series->finalized = true;

   resultsScatterPlot->render();
}

/// <summary>
/// Renders histogram and N/range analysis table on the Playground display.
/// </summary>
void renderHistogramsOnPlayground()
{
   arduino.clearDisplay();

   Histogram valueHistogram(sensorCapture->values(), sensorCapture->count(), HISTOGRAM_BINS);

   arduino.setTextSize(2);
   arduino.setCursor(0, 0);
   String headerText = String(sensorCapture->count()) + " Samples";
   arduino.println(headerText.c_str(), Color::HEADING);

   String rateText = "Target " + String(targetSampleRate) + "/s  Actual " + String(actualSampleRate.get(), 1) + "/s";
   arduino.println(rateText.c_str(), Color::LABEL);

   int16_t top = arduino.getCursorY();
   int16_t availableHeight = (int16_t)arduino.height() - top;
   int16_t totalWidth = (int16_t)arduino.width();
   constexpr int16_t sectionGap = 5;
   constexpr int16_t tableWidth = 160;
   int16_t leftWidth = totalWidth - sectionGap - tableWidth;
   int16_t x = leftWidth + sectionGap;

   int16_t plotHeight = (availableHeight - sectionGap) / 2;
   int16_t scatterHeight = plotHeight;
   int16_t histogramTop = top + scatterHeight + sectionGap;
   int16_t histogramHeight = availableHeight - scatterHeight - sectionGap;

   drawScatterPlot(0, leftWidth, top, scatterHeight, Color::GREEN);
   drawHistogram("", valueHistogram, 0, leftWidth, histogramTop, histogramHeight, Color::GREEN, Color::WHITE);

   arduino.setTextSize(2);
   Format numSamplesFormat("####", Format::Alignment::RIGHT);
   Format sigmaPercentFormat("###.##%", Format::Alignment::RIGHT);
   Format hzFormat(" ####", Format::Alignment::RIGHT);

   Stats rawStats = sensorCapture->computeBasicStats();
   float rawAvg = rawStats.get();
   float rawStdDev = rawStats.stdDev();
   size_t rawCount = rawStats.count();

   arduino.setCursor(x, 0);
   arduino.println("   N Sigma%   Hz", Color::VALUE3);

   arduino.setCursorX(x);
   float rawSigmaPercent = NAN;
   if (isfinite(rawAvg) && (fabsf(rawAvg) > 0.0f) && isfinite(rawStdDev))
   {
      rawSigmaPercent = (rawStdDev / fabsf(rawAvg)) * 100.0f;
   }

   if ((rawCount > 0) && isfinite(rawSigmaPercent))
   {
      arduino.print(" Raw", Color::LABEL);
      arduino.print(rawSigmaPercent, sigmaPercentFormat, Color::VALUE2);
      arduino.println(computeEffectiveRateHz(1), hzFormat, Color::VALUE2);
   }
   else
   {
      arduino.println(" Raw   n/a   n/a", Color::GRAY);
   }

   constexpr size_t BUFFER_SIZE_FOR_DISPLAY[] = { 10, 20, 50, 100 };
   for (size_t i = 0; i < (sizeof(BUFFER_SIZE_FOR_DISPLAY) / sizeof(BUFFER_SIZE_FOR_DISPLAY[0])); i++)
   {
      arduino.moveCursorY(-1);
      size_t sampleSize = BUFFER_SIZE_FOR_DISPLAY[i];
      float effectiveRateHz = computeEffectiveRateHz(sampleSize);

      Stats avgSeriesStats = sensorCapture->computeAverageSeriesStats(sampleSize);
      float avgMean = avgSeriesStats.get();
      float avgStdDev = avgSeriesStats.stdDev();
      size_t averageCount = avgSeriesStats.count();

      float avgSigmaPercent = NAN;
      if (isfinite(avgMean) && (fabsf(avgMean) > 0.0f) && isfinite(avgStdDev))
      {
         avgSigmaPercent = (avgStdDev / fabsf(avgMean)) * 100.0f;
      }

      arduino.setCursorX(x);
      if ((averageCount > 0) && isfinite(avgSigmaPercent))
      {
         arduino.print(sampleSize, numSamplesFormat, Color::LABEL);
         arduino.print(avgSigmaPercent, sigmaPercentFormat, Color::VALUE);
         arduino.println(effectiveRateHz, hzFormat, Color::VALUE);
      }
      else
      {
         std::string rowText = numSamplesFormat.toString((double)sampleSize) + "   n/a   n/a";
         arduino.println(rowText, Color::GRAY);
      }
   }
}

/// <summary>
/// Computes and prints capture statistics and histogram data to Serial.
/// </summary>
void printCaptureSummary()
{
   Stats basicStats = sensorCapture->computeBasicStats();

   float valueAvg = basicStats.get();
   float valueStdDev = basicStats.stdDev();
   float valueMin = basicStats.min();
   float valueMax = basicStats.max();
   float valueRange = SensorCapture::computeRange(valueMin, valueMax);
   float valueStdDevPercent = NAN;
   if (isfinite(valueAvg) && (fabsf(valueAvg) > 0.0f) && isfinite(valueStdDev))
   {
      valueStdDevPercent = (valueStdDev / fabsf(valueAvg)) * 100.0f;
   }

   Serial.println();
   Serial.println("Capture Summary");
   SerialX::print("Capture ms", 20);
   SerialX::println(samplingDurationS * 1000UL, 20);
   SerialX::print("Target Rate", 20);
   SerialX::println(String(targetSampleRate) + "/s", 20);
   SerialX::print("Actual Rate", 20);
   SerialX::println(String(actualSampleRate.get(), 1) + "/s", 20);
   SerialX::print("Samples", 20);
   SerialX::println(sensorCapture->count(), 20);
   SerialX::print("Avg", 20);
   SerialX::println(valueAvg, 3, 20);
   SerialX::print("StdDev", 20);
   SerialX::println(valueStdDev, 3, 20);
   SerialX::print("StdDev%", 20);
   SerialX::println(isfinite(valueStdDevPercent) ? String(valueStdDevPercent, 2) + "%" : "n/a", 20);
   SerialX::print("Min", 20);
   SerialX::println(valueMin, 3, 20);
   SerialX::print("Max", 20);
   SerialX::println(valueMax, 3, 20);
   SerialX::print("Range", 20);
   SerialX::println(valueRange, 3, 20);
   Serial.println();

   Histogram histogram(sensorCapture->values(), sensorCapture->count(), HISTOGRAM_BINS);
   SerialHistogram::print("Sensor Value Histogram", histogram, 3);

   sensorCapture->printFirstAndLastToSerial(10, 3);
}

/// <summary>
/// Prints post-capture block-average analysis for configured sample sizes.
/// </summary>
void printAveragingAnalysis()
{
   Serial.println("Averaging Analysis");

   SerialX::print("Num Samples", 12);
   SerialX::print("Range", 10);
   SerialX::print("StdDev", 10);
   SerialX::print("StdDev%", 10);
   SerialX::println("Eff Rate", 8);

   SerialX::print("-----------", 12);
   SerialX::print("-----", 10);
   SerialX::print("------", 10);
   SerialX::print("-------", 10);
   SerialX::println("--------", 8);

   for (size_t analysisIndex = 0; analysisIndex < NUM_BUFFER_SIZES; analysisIndex++)
   {
      size_t sampleSize = BUFFER_SIZES[analysisIndex];
      float effectiveRate = (sampleSize > 0) ? (static_cast<float>(targetSampleRate) / static_cast<float>(sampleSize)) : NAN;

      Stats avgSeriesStats = sensorCapture->computeAverageSeriesStats(sampleSize);
      float avgRange = SensorCapture::computeRange(avgSeriesStats.min(), avgSeriesStats.max());
      float avgStdDev = avgSeriesStats.stdDev();
      float avgMean = avgSeriesStats.get();
      size_t averageCount = avgSeriesStats.count();

      float avgStdDevPercent = NAN;
      if (isfinite(avgMean) && (fabsf(avgMean) > 0.0f) && isfinite(avgStdDev))
      {
         avgStdDevPercent = (avgStdDev / fabsf(avgMean)) * 100.0f;
      }

      SerialX::print(sampleSize, 12);
      if (averageCount == 0)
      {
         SerialX::print("n/a", 10);
         SerialX::print("n/a", 10);
         SerialX::print("n/a", 10);
      }
      else
      {
         SerialX::print(avgRange, 3, 10);
         SerialX::print(avgStdDev, 3, 10);
         SerialX::print(isfinite(avgStdDevPercent) ? String(avgStdDevPercent, 2) + "%" : "n/a", 10);
      }

      SerialX::println(isfinite(effectiveRate) ? String(effectiveRate, 1) + "/s" : "n/a", 8);
   }

   Serial.println();
}

/// <summary>
/// Initializes the display table used by the collecting-progress screen.
/// </summary>
void initializeCollectingTable()
{
   arduino.setTextSize(3);
   int16_t collectingTableY = arduino.charH();

   arduino.setTextSize(2);
   collectingTableY += arduino.charH();

   collectingTable = DisplayTable(&arduino, 0, collectingTableY);
   collectingTable.addRow("Max", maxCaptureFormat, Color::LABEL, Color::VALUE);
   collectingTable.addRow("Progress", progressPercentFormat, Color::LABEL, Color::VALUE);
   collectingTable.addRow("Target Rate", targetRateFormat, Color::LABEL, Color::VALUE);
   collectingTable.addRow("Actual Rate", actualRateFormat, Color::LABEL, Color::VALUE);
   collectingTable.addRow("Warmup", warmupStatusFormat, Color::LABEL, Color::VALUE);

   String maxText = String(maxSamples) + " samples OR " + String(samplingDurationS) + "s";
   collectingTable.updateValue(0, maxText, Color::VALUE);
   collectingTable.updateValue(2, targetSampleRate, Color::VALUE);
}

/// <summary>
/// Updates the collecting-progress screen shown on the Playground display. Max (samples/
/// duration) and Target Rate are static once capture starts; Progress and Actual Rate show
/// placeholder "----" values while warming up, then live values during capture. The Warmup
/// row shows a countdown while warming up and "Complete" afterward. Refresh is throttled to
/// DISPLAY_UPDATE_RATE_PER_SEC unless forceRefresh is true.
/// </summary>
/// <param name="forceRefresh">When true, bypasses the refresh-rate throttle and redraws immediately.</param>
void updateDisplay(bool forceRefresh = false)
{
   if (!warmupActive && sensorCapture->isCaptureComplete())
   {
      return;
   }

   unsigned long nowMs = millis();
   if (forceRefresh)
   {
      displayRefreshTimer.reset();
   }
   else if (!displayRefreshTimer.ready())
   {
      return;
   }

   if (!collectingViewInitialized)
   {
      arduino.clearDisplay();
      arduino.setCursor(0, 0);
      arduino.setTextSize(3);
      arduino.println("Sensor Averaging", Color::HEADING);

      arduino.setTextSize(2);
      arduino.println("Collecting data", Color::VALUE);
      collectingTable.invalidate();
      collectingViewInitialized = true;
   }

   if (warmupActive)
   {
      constexpr const char* PLACEHOLDER = "----";
      collectingTable.updateValue(1, PLACEHOLDER, Color::GRAY);
      collectingTable.updateValue(3, PLACEHOLDER, Color::GRAY);

      unsigned long warmupMs = warmupPeriodS * 1000UL;
      unsigned long elapsedWarmupMs = nowMs - warmupStartMs;
      unsigned long remainingMs = (elapsedWarmupMs < warmupMs) ? (warmupMs - elapsedWarmupMs) : 0;
      unsigned long remainingSeconds = (remainingMs + 999UL) / 1000UL;
      String warmupText = String(remainingSeconds) + "s remaining";
      collectingTable.updateValue(4, warmupText, Color::VALUE);

      collectingTable.draw();
      return;
   }

   collectingTable.updateValue(4, "Complete", Color::VALUE);

   size_t count = sensorCapture->count();
   unsigned long elapsedSeconds = (nowMs - captureStartMs) / 1000UL;
   if (elapsedSeconds > samplingDurationS)
   {
      elapsedSeconds = samplingDurationS;
   }

   float samplePercent = (maxSamples > 0) ? ((static_cast<unsigned long>(count) * 100.0f) / maxSamples) : 0.0f;
   float timePercent = (samplingDurationS > 0) ? ((elapsedSeconds * 100.0f) / samplingDurationS) : 0.0f;

   float progressPercent = max(samplePercent, timePercent);
   if (progressPercent > 100.0f)
   {
      progressPercent = 100.0f;
   }

   collectingTable.updateValue(1, progressPercent, Color::VALUE);
   collectingTable.updateValue(3, actualSampleRate.get(), Color::VALUE);
   collectingTable.draw();
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
   renderHistogramsOnPlayground();
}

/// <summary>
/// Adjusts the target sample rate by one encoder step, using 10/s steps below 100/s and
/// 100/s steps at or above 100/s, clamped to [MIN_SAMPLE_RATE_PER_SEC, MAX_SAMPLE_RATE_PER_SEC].
/// </summary>
/// <param name="steps">Signed number of encoder steps to apply.</param>
void adjustTargetSampleRate(int32_t steps)
{
   int32_t direction = (steps > 0) ? 1 : -1;
   int32_t remainingSteps = abs(steps);

   for (int32_t i = 0; i < remainingSteps; i++)
   {
      uint16_t stepSize = (targetSampleRate < 100) ? SAMPLE_RATE_STEP_LOW : SAMPLE_RATE_STEP_HIGH;
      int32_t newRate = static_cast<int32_t>(targetSampleRate) + (direction * stepSize);
      newRate = constrain(newRate, MIN_SAMPLE_RATE_PER_SEC, MAX_SAMPLE_RATE_PER_SEC);
      targetSampleRate = static_cast<uint16_t>(newRate);
   }
}

///
/// <summary>
/// Adjusts the maximum samples setting by encoder steps, clamped to [MIN_MAX_SAMPLES, MAX_MAX_SAMPLES].
/// </summary>
/// <param name="steps">Signed number of encoder steps to apply.</param>
///
void adjustMaxSamples(int32_t steps)
{
   int32_t direction = (steps > 0) ? 1 : -1;
   int32_t remainingSteps = abs(steps);

   for (int32_t i = 0; i < remainingSteps; i++)
   {
      int32_t newValue = static_cast<int32_t>(maxSamples) + (direction * SAMPLE_STEP);
      newValue = constrain(newValue, MIN_MAX_SAMPLES, MAX_MAX_SAMPLES);
      maxSamples = static_cast<size_t>(newValue);
   }
}

///
/// <summary>
/// Adjusts the sampling duration setting by encoder steps, clamped to [MIN_SAMPLING_DURATION_S, MAX_SAMPLING_DURATION_S].
/// </summary>
/// <param name="steps">Signed number of encoder steps to apply.</param>
///
void adjustSamplingDuration(int32_t steps)
{
   int32_t direction = (steps > 0) ? 1 : -1;
   int32_t remainingSteps = abs(steps);

   for (int32_t i = 0; i < remainingSteps; i++)
   {
      int32_t newValue = static_cast<int32_t>(samplingDurationS) + (direction * DURATION_STEP_S);
      newValue = constrain(newValue, MIN_SAMPLING_DURATION_S, MAX_SAMPLING_DURATION_S);
      samplingDurationS = static_cast<unsigned long>(newValue);
   }
}

///
/// <summary>
/// Adjusts the warmup period setting by encoder steps, clamped to [0, MAX_WARMUP_PERIOD_S].
/// </summary>
/// <param name="steps">Signed number of encoder steps to apply.</param>
///
void adjustWarmupPeriod(int32_t steps)
{
   int32_t direction = (steps > 0) ? 1 : -1;
   int32_t remainingSteps = abs(steps);

   for (int32_t i = 0; i < remainingSteps; i++)
   {
      int32_t newValue = static_cast<int32_t>(warmupPeriodS) + (direction * WARMUP_STEP_S);
      newValue = constrain(newValue, 0, MAX_WARMUP_PERIOD_S);
      warmupPeriodS = static_cast<unsigned long>(newValue);
   }
}

/// <summary>
/// Draws the startup prompt screen showing configurable options with the selected one highlighted.
/// EncoderA selects the option, EncoderB adjusts the value. Only clears the display on the first
/// draw (or after leaving the setup page) to avoid flicker on subsequent redraws, since every line
/// uses a fixed-width value with its own background color and fully overwrites itself in place.
/// </summary>
void drawPromptScreen()
{
   if (!promptViewInitialized)
   {
      arduino.clearDisplay();
      promptViewInitialized = true;
   }
   arduino.setCursor(0, 0);
   arduino.setTextSize(3);
   arduino.println("Setup", Color::HEADING);

   arduino.setTextSize(2);

   // Sample Rate option
   Color rateBgColor = (selectedSetupItem == SAMPLE_RATE) ? Color::BLUE : Color::BLACK;
   String rateValueText = String(targetSampleRate) + "/s";
   arduino.print("Rate: ", Color::LABEL);
   arduino.println(rateValueText.c_str(), Color::WHITE, rateBgColor);

   // Max Samples option
   Color samplesBgColor = (selectedSetupItem == MAX_SAMPLES_ITEM) ? Color::BLUE : Color::BLACK;
   String samplesValueText = String(maxSamples);
   arduino.print("Max Samples: ", Color::LABEL);
   arduino.println(samplesValueText.c_str(), Color::WHITE, samplesBgColor);

   // Sampling Duration option
   Color durationBgColor = (selectedSetupItem == SAMPLING_DURATION) ? Color::BLUE : Color::BLACK;
   String durationValueText = String(samplingDurationS) + "s";
   arduino.print("Max Duration: ", Color::LABEL);
   arduino.println(durationValueText.c_str(), Color::WHITE, durationBgColor);

   // Warmup Period option
   Color warmupBgColor = (selectedSetupItem == WARMUP_PERIOD) ? Color::BLUE : Color::BLACK;
   String warmupValueText = String(warmupPeriodS) + "s";
   arduino.print("Warmup: ", Color::LABEL);
   arduino.println(warmupValueText.c_str(), Color::WHITE, warmupBgColor);

   arduino.println();
   arduino.println("Enc A: Select", Color::VALUE);
   arduino.println("Enc B: Adjust", Color::VALUE);
   arduino.println("Button A: Start", Color::VALUE);
   arduino.println("Button B: Reset", Color::VALUE);
}


void setup()
{
   SerialX::begin(115200, 2000);
   Wire.begin();
   arduino.begin();

   sensor.begin();
   initializeCollectingTable();

   Util::printBoardInfo();

   loadSetupSettings();
   createSensorCapture();

   drawPromptScreen();
   Serial.println("Turn Encoder A to select, Encoder B to adjust, Button A to start, Button B to reset.");
}

void loop()
{
   if (!captureStarted)
   {
      // EncoderA cycles through setup items
      int32_t deltaA = arduino.encoderA.delta();
      if (deltaA != 0)
      {
         int32_t newItem = static_cast<int32_t>(selectedSetupItem) + (deltaA > 0 ? 1 : -1);
         newItem = (newItem + 4) % 4; // Wrap around (0-3)
         selectedSetupItem = static_cast<SetupItem>(newItem);
         drawPromptScreen();
      }

      // EncoderB adjusts the selected item's value
      int32_t deltaB = arduino.encoderB.delta();
      if (deltaB != 0)
      {
         switch (selectedSetupItem)
         {
            case SAMPLE_RATE:
               adjustTargetSampleRate(deltaB);
               break;
            case MAX_SAMPLES_ITEM:
               adjustMaxSamples(deltaB);
               break;
            case SAMPLING_DURATION:
               adjustSamplingDuration(deltaB);
               break;
            case WARMUP_PERIOD:
               adjustWarmupPeriod(deltaB);
               break;
         }
         drawPromptScreen();
      }

      if (arduino.buttonB.wasPressed())
      {
         resetSetupSettings();
         drawPromptScreen();
         Serial.println("Settings reset to defaults.");
      }

      if (arduino.buttonA.wasPressed())
      {
         captureStarted = true;
         promptViewInitialized = false;
         createSensorCapture();
         sensorCapture->setSamplingInterval(1000UL / targetSampleRate);
         sensorCapture->reset();
         actualSampleRate.reset();
         captureStartMs = millis();
         warmupActive = (warmupPeriodS > 0);
         warmupStartMs = captureStartMs;
         collectingViewInitialized = false;
         {
            String maxText = String(maxSamples) + " samples OR " + String(samplingDurationS) + "s";
            collectingTable.updateValue(0, maxText, Color::VALUE);
         }
         collectingTable.updateValue(2, targetSampleRate, Color::VALUE);
         createWarmupCapture();
         saveSetupSettings();
         warmupBoundaryCount = 0;
         warmupBoundaryNextIndex = 0;
         realBoundaryCount = 0;
         boundaryDumpPrinted = false;
         if (warmupActive)
         {
            updateDisplay(true);
            Serial.println("Warming up...");
         }
         else
         {
            updateDisplay(true);
            Serial.println("Capture started...");
         }
      }

      return;
   }

   if (warmupActive)
   {
      unsigned long warmupMs = warmupPeriodS * 1000UL;
      unsigned long elapsedMs = millis() - warmupStartMs;

      if ((warmupCapture != nullptr) && warmupCapture->readyForValue())
      {
         unsigned long sampleTimestampMs = millis();
         float sensorValue = sensor.get();
         warmupCapture->addValue(sensorValue);
         recordWarmupBoundarySample(sampleTimestampMs, sensorValue);
      }

      if (elapsedMs >= warmupMs)
      {
         warmupActive = false;
         sensorCapture->reset();
         actualSampleRate.reset();
         captureStartMs = millis();

         // Take the first retained sample immediately, before the display redraw below, so
         // there is no gap between the last warmup sample and the first retained sample
         // (a delay here could let the sensor cool down and skew the retained data).
         unsigned long firstValueTimestampMs = millis();
         float firstValue = sensor.get();
         SensorCaptureState firstValueStates = sensorCapture->addValue(firstValue);
         if ((firstValueStates & SENSOR_CAPTURE_STATE_VALUE_STORED) != 0)
         {
            actualSampleRate.tick();
            recordRealBoundarySample(firstValueTimestampMs, firstValue);
         }

         updateDisplay(true);
         Serial.println("Capture started...");
      }
      else
      {
         updateDisplay();
      }

      return;
   }

   if (captureFinalized)
   {
      if (arduino.buttonA.wasPressed())
      {
         captureStarted = false;
         captureFinalized = false;
         drawPromptScreen();
         Serial.println("Turn Encoder A to set sample rate, press Button A to start.");
      }

      return;
   }

   SensorCaptureState states = sensorCapture->update();
   SensorCaptureState valueStates = SENSOR_CAPTURE_STATE_NONE;

   if (sensorCapture->readyForValue())
   {
      unsigned long sampleTimestampMs = millis();
      float sensorValue = sensor.get();
      valueStates = sensorCapture->addValue(sensorValue);

      if ((valueStates & SENSOR_CAPTURE_STATE_VALUE_STORED) != 0)
      {
         actualSampleRate.tick();
         recordRealBoundarySample(sampleTimestampMs, sensorValue);
         updateDisplay();
      }

      if ((valueStates & SENSOR_CAPTURE_STATE_COMPLETED) != 0)
      {
         finishCapture();
      }
   }

   if ((states & SENSOR_CAPTURE_STATE_COMPLETED) != 0)
   {
      finishCapture();
   }
}

