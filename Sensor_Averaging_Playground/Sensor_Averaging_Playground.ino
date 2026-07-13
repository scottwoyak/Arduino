//
// Captures finite sensor values for a fixed run window (or until the sample cap is reached), stores
// them in RAM, and reports serial/display summaries.
//
// On startup, a SetupEditor screen lets the user review/adjust the target sample rate, max samples,
// sampling duration, and warmup period (Encoder A selects a field, Encoder B adjusts it, Button B
// resets to defaults). Press Button A to confirm and start the capture using the selected values.
//
// Capture flow:
// 1) Initialize display, serial, and sensor.
// 2) Load setup values from Preferences and run the SetupEditor screen; start capture on Button A.
// 3) Sample at up to the selected target rate and store finite values in RAM.
// 4) Stop when MAX_SAMPLES are stored or SAMPLING_DURATION_S elapses.
//
// Output flow:
// - While collecting, the Playground display shows live capture progress plus target/actual sample rate.
// - After capture, the display renders a value histogram with min/max labels plus Sigma%/effective-rate
//   rows for multiple averaging window sizes, along with the target/actual sample rate.
// - Serial output includes the averaging analysis table and the warmup/real boundary sample dump.
//
// Sensor mode:
// - Reads from TestSensor (configurable via TestSensor.h using alias).
//
// Typical usage: flash the sketch, adjust the setup values with Encoder A/B, press Button A to start,
// and allow capture to complete, then review serial and display outputs to compare stability
// (StdDev%, range) across averaging sizes.
//

#include <Wire.h>

#include "ESP32_S3_Playground.h"
#include "DisplayField.h"
#include "Histogram.h"
#include "HistogramPlot.h"
#include "RollingRate.h"
#include "ScatterPlot.h"
#include "SerialTable.h"
#include "SerialX.h"
#include "SetupEditor.h"
#include "Stopwatch.h"
#include "TestSensor.h"
#include "Timer.h"
#include "Util.h"
#include "Values.h"

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
constexpr uint16_t SAMPLE_RATE_STEP_THRESHOLD = 100;
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

// ----------- Preferences Namespace
constexpr const char* PREF_NAMESPACE = "sensor_avg";

long targetSampleRate = DEFAULT_SAMPLE_RATE_PER_SEC;
long maxSamples = DEFAULT_MAX_SAMPLES;
long samplingDurationS = DEFAULT_SAMPLING_DURATION_S;
long warmupPeriodS = DEFAULT_WARMUP_PERIOD_S;
RollingRate actualSampleRate;

///
/// <summary>
/// Sample-rate setup field with non-linear stepping: SAMPLE_RATE_STEP_LOW below 100/s,
/// SAMPLE_RATE_STEP_HIGH at or above 100/s.
/// </summary>
///
class SampleRateField : public IntSetupField
{
public:
   using IntSetupField::IntSetupField;

protected:
   long _stepValue(long current, int32_t direction) override
   {
      long stepSize = (current < SAMPLE_RATE_STEP_THRESHOLD) ? SAMPLE_RATE_STEP_LOW : SAMPLE_RATE_STEP_HIGH;
      return current + (direction * stepSize);
   }
};

// ----------- Setup Screen Fields
Format setupRateFormat("####/s", Format::Alignment::LEFT);
Format setupSamplesFormat("#####", Format::Alignment::LEFT);
Format setupDurationFormat("###s", Format::Alignment::LEFT);
Format setupWarmupFormat("###s", Format::Alignment::LEFT);

SampleRateField rateField("Rate", "rate", &targetSampleRate,
   MIN_SAMPLE_RATE_PER_SEC, MAX_SAMPLE_RATE_PER_SEC, SAMPLE_RATE_STEP_LOW, DEFAULT_SAMPLE_RATE_PER_SEC, setupRateFormat);
IntSetupField samplesField("Max Samples", "samples", &maxSamples,
   MIN_MAX_SAMPLES, MAX_MAX_SAMPLES, SAMPLE_STEP, DEFAULT_MAX_SAMPLES, setupSamplesFormat);
IntSetupField durationField("Max Duration", "duration", &samplingDurationS,
   MIN_SAMPLING_DURATION_S, MAX_SAMPLING_DURATION_S, DURATION_STEP_S, DEFAULT_SAMPLING_DURATION_S, setupDurationFormat);
IntSetupField warmupField("Warmup", "warmup", &warmupPeriodS,
   0, MAX_WARMUP_PERIOD_S, WARMUP_STEP_S, DEFAULT_WARMUP_PERIOD_S, setupWarmupFormat);

SetupField* setupFields[] = { &rateField, &samplesField, &durationField, &warmupField };
SetupEditor setupEditor(&arduino, PREF_NAMESPACE, "Setup", setupFields, 4);

Values* samplesValues = nullptr;
Values warmupValues;
bool captureStarted = false;
bool captureFinalized = false;
Stopwatch captureStopwatch{ false, StopwatchPrecision::Millis };
bool running = false;
Stopwatch warmupStopwatch{ false, StopwatchPrecision::Millis };
Timer samplingTimer(1000UL / DEFAULT_SAMPLE_RATE_PER_SEC);

// Boundary diagnostics: dump the last warmup samples and first real samples once enough
// real samples exist, querying timestamps/values directly from the Values objects.
constexpr uint8_t NUM_BOUNDARY_SAMPLES = 10;
bool boundaryDumpPrinted = false;

///
/// <summary>
/// Prints the warmup/real boundary sample dump after capture is complete, querying timestamps
/// and values directly from the warmup and samples Values objects.
/// </summary>
///
void printBoundaryDump()
{
   if (boundaryDumpPrinted)
   {
      return;
   }

   boundaryDumpPrinted = true;

   static const SerialTable::Column columns[] = {
      { "Set", 8 },
      { "Time (ms)", 12 },
      { "Delta (ms)", 12 },
      { "Value", 10 },
   };
   SerialTable table("Warmup/Real Boundary Sample Dump", columns, 4);
   table.printHeader();

   size_t warmupCount = warmupValues.count();
   size_t warmupStart = (warmupCount > NUM_BOUNDARY_SAMPLES) ? (warmupCount - NUM_BOUNDARY_SAMPLES) : 0;
   unsigned long previousTimestampMs = 0;
   bool havePreviousTimestamp = false;

   for (size_t i = warmupStart; i < warmupCount; i++)
   {
      unsigned long timestampMs = warmupValues.timestamp(i);
      long deltaMs = havePreviousTimestamp ? (long)(timestampMs - previousTimestampMs) : 0;
      table.printRow("warmup", timestampMs, havePreviousTimestamp ? String(deltaMs) : String("-"),
         SerialTable::fixed(warmupValues[i], 3));
      previousTimestampMs = timestampMs;
      havePreviousTimestamp = true;
   }

   size_t realCount = min(static_cast<size_t>(NUM_BOUNDARY_SAMPLES), samplesValues->count());
   for (size_t i = 0; i < realCount; i++)
   {
      unsigned long timestampMs = samplesValues->timestamp(i);
      long deltaMs = havePreviousTimestamp ? (long)(timestampMs - previousTimestampMs) : 0;
      table.printRow("real", timestampMs, havePreviousTimestamp ? String(deltaMs) : String("-"),
         SerialTable::fixed((*samplesValues)[i], 3));
      previousTimestampMs = timestampMs;
      havePreviousTimestamp = true;
   }
}

///
/// <summary>
/// Creates a new samples Values object with current settings.
/// </summary>
///
void createSamplesValues()
{
   if (samplesValues != nullptr)
   {
      delete samplesValues;
   }
   samplesValues = new Values(maxSamples);
}

///
/// <summary>
/// (Re)configures the warmup Values object used to record warmup-phase samples for display only;
/// these samples are excluded from the histogram and statistics.
/// </summary>
///
void createWarmupValues()
{
   size_t warmupMaxValues = (warmupPeriodS > 0) ? (static_cast<size_t>(warmupPeriodS) * targetSampleRate + 1) : 0;
   warmupValues.reset(warmupMaxValues);
}

// ----------- Display Items
constexpr uint16_t DISPLAY_UPDATE_RATE_PER_SEC = 10;
RateTimer displayRefreshTimer(DISPLAY_UPDATE_RATE_PER_SEC);

// Minimum time before the next sample is due for a display redraw to be allowed to proceed.
// The redraw (field draws/sprite pushes) takes a few milliseconds; performing it right before
// a sample is due would delay that sample's timing check in loop(), so the redraw is postponed
// by one loop() iteration whenever a sample is imminent.
constexpr unsigned long DISPLAY_DRAW_SAFETY_MARGIN_MS = 3;
bool collectingViewInitialized = false;
Format progressPercentFormat("###%", Format::Alignment::LEFT);
Format maxCaptureFormat(20, Format::Alignment::LEFT);
Format targetRateFormat("####/s", Format::Alignment::LEFT);
Format actualRateFormat("####.#/s", Format::Alignment::LEFT);
Format warmupStatusFormat(20, Format::Alignment::LEFT);
DisplayField* maxField = nullptr;
DisplayField* progressField = nullptr;
DisplayField* targetRateField = nullptr;
DisplayField* actualRateField = nullptr;
DisplayField* warmupStatusField = nullptr;
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

   size_t warmupCount = warmupValues.count();
   const float* warmupValueData = warmupValues.values();

   size_t count = samplesValues->count();
   const float* values = samplesValues->values();

   if (warmupCount > 0)
   {
      ScatterPlotSeries* warmupSeries = resultsScatterPlot->createSeries(warmupCount);
      warmupSeries->showPoints = true;
      warmupSeries->showLines = false;
      warmupSeries->color = Color::LIGHTGRAY;

      for (size_t i = 0; i < warmupCount; i++)
      {
         warmupSeries->add((float)i, warmupValueData[i]);
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

   Histogram valueHistogram(samplesValues->values(), samplesValues->count(), HISTOGRAM_BINS);

   arduino.setTextSize(2);
   arduino.setCursor(0, 0);
   String headerText = String(samplesValues->count()) + " Samples";
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

   Stats rawStats = samplesValues->computeBasicStats();
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

      Stats avgSeriesStats = samplesValues->computeAverageSeriesStats(sampleSize);
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

      Stats avgSeriesStats = samplesValues->computeAverageSeriesStats(sampleSize);
      float avgRange = Values::computeRange(avgSeriesStats.min(), avgSeriesStats.max());
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
/// Initializes the DisplayField rows used by the collecting-progress screen.
/// </summary>
void initializeCollectingTable()
{
   arduino.setTextSize(3);
   int16_t collectingTableY = arduino.charH();

   arduino.setTextSize(2);
   collectingTableY += arduino.charH();
   int16_t rowHeight = arduino.charH();

   delete maxField;
   delete progressField;
   delete targetRateField;
   delete actualRateField;
   delete warmupStatusField;

   // pad labels so their ": " separators line up, matching "Target Rate"/"Actual Rate"
   maxField = new DisplayField(&arduino, 0, collectingTableY, "        Max", maxCaptureFormat, 2);
   progressField = new DisplayField(&arduino, 0, collectingTableY + rowHeight, "   Progress", progressPercentFormat, 2);
   targetRateField = new DisplayField(&arduino, 0, collectingTableY + rowHeight * 2, "Target Rate", targetRateFormat, 2);
   actualRateField = new DisplayField(&arduino, 0, collectingTableY + rowHeight * 3, "Actual Rate", actualRateFormat, 2);
   warmupStatusField = new DisplayField(&arduino, 0, collectingTableY + rowHeight * 4, "     Warmup", warmupStatusFormat, 2);

   String maxText = String(maxSamples) + " samples OR " + String(samplingDurationS) + "s";
   maxField->setValue(maxText);
   targetRateField->setValue(static_cast<unsigned long>(targetSampleRate));
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
   bool durationElapsed = captureStopwatch.elapsedMillis() >= (samplingDurationS * 1000UL);
   if (!warmupStopwatch.isRunning() && (samplesValues->isCaptureComplete() || durationElapsed))
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

   // Postpone the actual draw work (without consuming the throttle) if a sample is imminent,
   // so drawing never delays the next sample's timing check in loop().
   if (samplingTimer.remaining() < DISPLAY_DRAW_SAFETY_MARGIN_MS)
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
      maxField->invalidate();
      progressField->invalidate();
      targetRateField->invalidate();
      actualRateField->invalidate();
      warmupStatusField->invalidate();
      collectingViewInitialized = true;
   }

   maxField->draw();
   targetRateField->draw();

   if (warmupStopwatch.isRunning())
   {
      constexpr const char* PLACEHOLDER = "----";
      progressField->setValueColor(Color::GRAY);
      progressField->setValue(PLACEHOLDER);
      actualRateField->setValueColor(Color::GRAY);
      actualRateField->setValue(PLACEHOLDER);

      unsigned long warmupMs = warmupPeriodS * 1000UL;
      unsigned long elapsedWarmupMs = static_cast<unsigned long>(warmupStopwatch.elapsedMillis());
      unsigned long remainingMs = (elapsedWarmupMs < warmupMs) ? (warmupMs - elapsedWarmupMs) : 0;
      unsigned long remainingSeconds = (remainingMs + 999UL) / 1000UL;
      String warmupText = String(remainingSeconds) + "s remaining";
      warmupStatusField->setValueColor(Color::VALUE);
      warmupStatusField->setValue(warmupText);

      progressField->draw();
      actualRateField->draw();
      warmupStatusField->draw();
      return;
   }

   warmupStatusField->setValueColor(Color::VALUE);
   warmupStatusField->setValue("Complete");

   size_t count = samplesValues->count();
   unsigned long elapsedSeconds = static_cast<unsigned long>(captureStopwatch.elapsedMillis()) / 1000UL;
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

   progressField->setValueColor(Color::VALUE);
   progressField->setValue(progressPercent);
   actualRateField->setValueColor(Color::VALUE);
   actualRateField->setValue(actualSampleRate.get());

   progressField->draw();
   actualRateField->draw();
   warmupStatusField->draw();
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
   printBoundaryDump();
   printAveragingAnalysis();
   renderHistogramsOnPlayground();
}

/// <summary>
/// Starts a capture run using the currently confirmed setup values.
/// </summary>
void startCapture()
{
   captureStarted = true;
   createSamplesValues();
   samplesValues->reset();
   samplingTimer.setDuration(1000UL / targetSampleRate);
   actualSampleRate.reset();
   running = true;
   captureStopwatch.reset();
   captureStopwatch.start();
   warmupStopwatch.reset();
   if (warmupPeriodS > 0)
   {
      warmupStopwatch.start();
   }
   collectingViewInitialized = false;
   {
      String maxText = String(maxSamples) + " samples OR " + String(samplingDurationS) + "s";
      maxField->setValue(maxText);
   }
   targetRateField->setValue(static_cast<unsigned long>(targetSampleRate));
   createWarmupValues();
   boundaryDumpPrinted = false;
   updateDisplay(true);
   Serial.println(warmupStopwatch.isRunning() ? "Warming up..." : "Capture started...");
}

void setup()
{
   SerialX::begin(115200, 2000);
   Wire.begin();
   arduino.begin();

   sensor.begin();
   initializeCollectingTable();

   Util::printBoardInfo();

   setupEditor.load();
   setupEditor.run();
   startCapture();
}

void loop()
{
   if (captureFinalized)
   {
      if (arduino.buttonA.wasPressed())
      {
         captureStarted = false;
         captureFinalized = false;
         setupEditor.run();
         startCapture();
      }

      return;
   }

   if (running && samplingTimer.ready())
   {
      float sensorValue = sensor.get();
      actualSampleRate.tick();

      if (warmupStopwatch.elapsedSecs() < warmupPeriodS)
      {
         warmupValues.addValue(sensorValue);
         updateDisplay();
      }
      else
      {
         if (warmupStopwatch.isRunning())
         {
            // Transitioning from warmup to real capture.
            warmupStopwatch.stop();
            updateDisplay(true);
            Serial.println("Capture started...");
         }

         samplesValues->addValue(sensorValue);

         updateDisplay();

         bool durationElapsed = captureStopwatch.elapsedMillis() >= (samplingDurationS * 1000UL);
         if ((samplesValues->count() >= maxSamples) || durationElapsed)
         {
            finishCapture();
         }
      }
   }
}

