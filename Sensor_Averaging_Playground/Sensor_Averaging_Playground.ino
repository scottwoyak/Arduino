//
// Captures finite sensor values for a fixed run window (or until the sample cap is reached), stores
// them in RAM, and reports serial/display summaries.
//
// On startup, a SetupDisplay screen lets the user review/adjust the target sample rate, max samples,
// sampling duration, and warmup period (Encoder A selects a field, Encoder B adjusts it, Button B
// resets to defaults). Press Button A to confirm and start the capture using the selected values.
//
// Capture flow:
// 1) Initialize display, serial, and sensor.
// 2) Load setup values from Preferences and run the SetupDisplay screen; start capture on Button A.
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
#include "DisplayGrid.h"
#include "EnumSelector.h"
#include "Histogram.h"
#include "HistogramPlot.h"
#include "RollingRate.h"
#include "ScatterPlot.h"
#include "SerialTable.h"
#include "SerialX.h"
#include "SetupDisplay.h"
#include "Stopwatch.h"
#include "TestSensor.h"
#include "Timer.h"
#include "Util.h"
#include "Values.h"

// ----------- The Board
ESP32_S3_Playground arduino;
TestSensor sensor;

///
/// <summary>
/// Which post-capture results view is currently displayed: the scatter/histogram charts,
/// or the Sigma%/effective-rate summary table.
/// </summary>
///
enum class ResultView : uint8_t { Charts, Table };

EnumSelector<ResultView> resultViewSelector(arduino.encoderA, ResultView::Table, ResultView::Charts);

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

SampleRateField rateField("Rate", &targetSampleRate,
   MIN_SAMPLE_RATE_PER_SEC, MAX_SAMPLE_RATE_PER_SEC, SAMPLE_RATE_STEP_LOW, DEFAULT_SAMPLE_RATE_PER_SEC, setupRateFormat);
IntSetupField samplesField("Max Samples", &maxSamples,
   MIN_MAX_SAMPLES, MAX_MAX_SAMPLES, SAMPLE_STEP, DEFAULT_MAX_SAMPLES, setupSamplesFormat);
IntSetupField durationField("Max Duration", &samplingDurationS,
   MIN_SAMPLING_DURATION_S, MAX_SAMPLING_DURATION_S, DURATION_STEP_S, DEFAULT_SAMPLING_DURATION_S, setupDurationFormat);
IntSetupField warmupField("Warmup", &warmupPeriodS,
   0, MAX_WARMUP_PERIOD_S, WARMUP_STEP_S, DEFAULT_WARMUP_PERIOD_S, setupWarmupFormat);

SetupField* setupFields[] = { &rateField, &samplesField, &durationField, &warmupField };
SetupDisplay setupDisplay(&arduino, PREF_NAMESPACE, "Setup", setupFields, 4);

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
constexpr uint8_t NUM_BOUNDARY_REAL_SAMPLES = 40;
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

   size_t realCount = min(static_cast<size_t>(NUM_BOUNDARY_REAL_SAMPLES), samplesValues->count());
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
   size_t warmupMaxValues = (warmupPeriodS > 0) ? (warmupPeriodS * targetSampleRate + 1) : 0;
   warmupValues.reset(warmupMaxValues);
}

// ----------- Display Items
constexpr uint16_t DISPLAY_UPDATE_RATE_PER_SEC = 10;
RateTimer displayRefreshTimer(DISPLAY_UPDATE_RATE_PER_SEC);

bool collectingViewInitialized = false;
Format progressPercentFormat("###%", Format::Alignment::LEFT);
Format maxCaptureFormat(20, Format::Alignment::LEFT);
Format targetRateFormat("####/s", Format::Alignment::LEFT);
Format actualRateFormat("####.#/s", Format::Alignment::LEFT);
Format warmupStatusFormat(20, Format::Alignment::LEFT);
Format samplesCountFormat("#####", Format::Alignment::LEFT);
Format elapsedFormat("####s", Format::Alignment::LEFT);
DisplayField* warmupStatusField = nullptr;
DisplayField* maxField = nullptr;
DisplayField* progressField = nullptr;
DisplayField* samplesCountField = nullptr;
DisplayField* elapsedField = nullptr;
DisplayField* targetRateField = nullptr;
DisplayField* actualRateField = nullptr;
ScatterPlot* resultsScatterPlot = nullptr;

// ----------- Analysis Settings
constexpr size_t HISTOGRAM_BINS = 20;
constexpr const char* CHART_MIN_MAX_FORMAT = "##.##";
constexpr const char* CHART_Y_AXIS_FORMAT = "####";
constexpr size_t BUFFER_SIZES[] = { 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 };
constexpr size_t NUM_BUFFER_SIZES = sizeof(BUFFER_SIZES) / sizeof(BUFFER_SIZES[0]);

///
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
/// <param name="yAxisFormat">If not nullptr, reserves a left-side Y-axis column sized to fit
/// labels formatted with this pattern (max bin count at top, 1 at bottom); pass nullptr for
/// no Y-axis. Use the same format string as the paired ScatterPlot's setYAxisFormat() so
/// both charts' reserved label columns end up the same width.</param>
///
void drawHistogram(const char* title, const Histogram& histogram, int16_t sectionLeft, int16_t sectionWidth, int16_t sectionTop, int16_t sectionHeight, Color barColor, Color axisLabelColor, const char* yAxisFormat = nullptr)
{
   arduino.setTextSize(2);
   arduino.setCursor(sectionLeft, sectionTop);
   arduino.println(title, Color::LABEL);

   int16_t chartTop = arduino.getCursorY() + 1;
   int16_t adjustedHeight = sectionHeight - (chartTop - sectionTop);
   HistogramPlot plot(&arduino, histogram, sectionLeft, sectionWidth, chartTop, adjustedHeight, barColor, Format(CHART_MIN_MAX_FORMAT), axisLabelColor, yAxisFormat);
   plot.render();
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
   resultsScatterPlot->setYAxisFormat(CHART_Y_AXIS_FORMAT);

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

///
/// <summary>
/// Draws the shared "Averaging Results" header, common to both result views. The caller
/// is responsible for clearing the display beforehand.
/// </summary>
///
void drawResultsHeader()
{
   arduino.setTextSize(3);
   arduino.setCursor(0, 0);
   arduino.println("Averaging Results", Color::HEADING);
}

///
/// <summary>
/// Draws the scatter plot and histogram charts view on the Playground display.
/// </summary>
///
void drawChartsView()
{
   drawResultsHeader();

   Histogram valueHistogram(samplesValues->values(), samplesValues->count(), HISTOGRAM_BINS);

   int16_t top = arduino.getCursorY();
   int16_t availableHeight = (int16_t)arduino.height() - top;
   int16_t totalWidth = (int16_t)arduino.width();
   constexpr int16_t sectionGap = 5;

   int16_t plotHeight = (availableHeight - sectionGap) / 2;
   int16_t scatterHeight = plotHeight;
   int16_t histogramTop = top + scatterHeight + sectionGap;
   int16_t histogramHeight = availableHeight - scatterHeight - sectionGap;

   drawScatterPlot(0, totalWidth, top, scatterHeight, Color::GREEN);

   // Use the same Y-axis format string as the scatter plot so both charts reserve an
   // identically sized left-side label column and their x-axes line up visually.
   drawHistogram("", valueHistogram, 0, totalWidth, histogramTop, histogramHeight, Color::GREEN, Color::WHITE, CHART_Y_AXIS_FORMAT);
}

///
/// <summary>
/// Draws the StdDev/StdDev%/effective-rate summary table view on the Playground display.
/// </summary>
///
void drawTableView()
{
   drawResultsHeader();

   arduino.setTextSize(2);

   unsigned long elapsedSeconds = static_cast<unsigned long>(captureStopwatch.elapsedSecs());
   String headerText = String(samplesValues->count()) + " samples collected in " + String(elapsedSeconds) + " seconds";
   arduino.println(headerText, Color::LABEL);

   String rateText = "Target " + String(targetSampleRate) + "/s  Actual " + String(actualSampleRate.get(), 0) + "/s";
   arduino.println(rateText, Color::LABEL);
   arduino.println();

   static Format numSamplesFormat("####", Format::Alignment::RIGHT);
   static Format rangeFormat("###.##", Format::Alignment::RIGHT);
   static Format stdDevFormat("###.##", Format::Alignment::RIGHT);
   static Format stdDevPercentFormat("###.##%", Format::Alignment::RIGHT);
   static Format hzFormat(" ####", Format::Alignment::RIGHT);

   static const DisplayGrid::Column columns[] = {
      { "N", &numSamplesFormat },
      { "Range", &rangeFormat },
      { "StdDev", &stdDevFormat },
      { "StdDev%", &stdDevPercentFormat },
      { "Hz", &hzFormat },
   };
   DisplayGrid grid(&arduino, nullptr, columns, 5);
   grid.printHeader();

   Stats rawStats = samplesValues->computeBasicStats();
   float rawAvg = rawStats.get();
   float rawRange = Values::computeRange(rawStats.min(), rawStats.max());
   float rawStdDev = rawStats.stdDev();
   size_t rawCount = rawStats.count();

   float rawStdDevPercent = NAN;
   if (isfinite(rawAvg) && (fabsf(rawAvg) > 0.0f) && isfinite(rawStdDev))
   {
      rawStdDevPercent = (rawStdDev / fabsf(rawAvg)) * 100.0f;
   }

   if ((rawCount > 0) && isfinite(rawStdDevPercent))
   {
      grid.printRow(Color::VALUE2, "Raw", rawRange, rawStdDev, rawStdDevPercent, actualSampleRate.get());
   }
   else
   {
      grid.printRow(Color::GRAY, "Raw", "n/a", "n/a", "n/a", "n/a");
   }

   for (size_t i = 0; i < NUM_BUFFER_SIZES; i++)
   {
      size_t sampleSize = BUFFER_SIZES[i];
      float effectiveRateHz = actualSampleRate.get() / sampleSize;

      Stats avgSeriesStats = samplesValues->computeAverageSeriesStats(sampleSize);
      float avgMean = avgSeriesStats.get();
      float avgRange = Values::computeRange(avgSeriesStats.min(), avgSeriesStats.max());
      float avgStdDev = avgSeriesStats.stdDev();
      size_t averageCount = avgSeriesStats.count();

      float avgStdDevPercent = NAN;
      if (isfinite(avgMean) && (fabsf(avgMean) > 0.0f) && isfinite(avgStdDev))
      {
         avgStdDevPercent = (avgStdDev / fabsf(avgMean)) * 100.0f;
      }

      if ((averageCount > 0) && isfinite(avgStdDevPercent))
      {
         grid.printRow(Color::VALUE, sampleSize, avgRange, avgStdDev, avgStdDevPercent, effectiveRateHz);
      }
      else
      {
         grid.printRow(Color::GRAY, sampleSize, "n/a", "n/a", "n/a", "n/a");
      }
   }
}

///
/// <summary>
/// Draws the currently selected results view on the Playground display. Rotating
/// encoderA cycles between the charts view and the summary table view.
/// </summary>
///
void drawResultView()
{
   arduino.clearDisplay();

   if (resultViewSelector.value() == ResultView::Charts)
   {
      drawChartsView();
   }
   else
   {
      drawTableView();
   }
}

///
/// <summary>
/// Initializes the DisplayField rows used by the collecting-progress screen.
/// </summary>
///
void initializeCollectingTable()
{
   arduino.setTextSize(3);
   int16_t collectingTableY = arduino.charH();

   arduino.setTextSize(2);
   collectingTableY += arduino.charH();
   int16_t rowHeight = arduino.charH();

   delete warmupStatusField;
   delete maxField;
   delete progressField;
   delete samplesCountField;
   delete elapsedField;
   delete targetRateField;
   delete actualRateField;

   // pad labels so their ": " separators line up, matching "Target Rate"/"Actual Rate"
   warmupStatusField = new DisplayField(&arduino, 0, collectingTableY, "     Warmup", warmupStatusFormat, 2);
   maxField = new DisplayField(&arduino, 0, collectingTableY + rowHeight, "        Max", maxCaptureFormat, 2);
   progressField = new DisplayField(&arduino, 0, collectingTableY + rowHeight * 2, "   Progress", progressPercentFormat, 2);
   samplesCountField = new DisplayField(&arduino, 0, collectingTableY + rowHeight * 3, "    Samples", samplesCountFormat, 2);
   elapsedField = new DisplayField(&arduino, 0, collectingTableY + rowHeight * 4, "    Elapsed", elapsedFormat, 2);
   targetRateField = new DisplayField(&arduino, 0, collectingTableY + rowHeight * 5, "Target Rate", targetRateFormat, 2);
   actualRateField = new DisplayField(&arduino, 0, collectingTableY + rowHeight * 6, "Actual Rate", actualRateFormat, 2);

   String maxText = String(maxSamples) + " samples OR " + String(samplingDurationS) + "s";
   maxField->setValue(maxText);
   targetRateField->setValue(targetSampleRate);
}

///
/// <summary>
/// Updates the collecting-progress screen shown on the Playground display. Max (samples/
/// duration) and Target Rate are static once capture starts; Progress, Samples, Elapsed,
/// and Actual Rate show placeholder "----" values while warming up, then live values during
/// capture. The Warmup row (shown first) shows a countdown while warming up and "Complete"
/// afterward. Refresh is throttled to DISPLAY_UPDATE_RATE_PER_SEC unless forceRefresh is true.
/// </summary>
/// <param name="forceRefresh">When true, bypasses the refresh-rate throttle and redraws immediately.</param>
///
void updateDisplay(bool forceRefresh = false)
{
   bool durationElapsed = captureStopwatch.elapsedSecs() >= samplingDurationS;
   if (!warmupStopwatch.isRunning() && (samplesValues->isFull() || durationElapsed))
   {
      return;
   }

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
      warmupStatusField->invalidate();
      maxField->invalidate();
      progressField->invalidate();
      samplesCountField->invalidate();
      elapsedField->invalidate();
      targetRateField->invalidate();
      actualRateField->invalidate();
      collectingViewInitialized = true;
   }

   maxField->draw();
   targetRateField->draw();

   if (warmupStopwatch.isRunning())
   {
      constexpr const char* PLACEHOLDER = "----";
      progressField->setValueColor(Color::GRAY);
      progressField->setValue(PLACEHOLDER);
      samplesCountField->setValueColor(Color::GRAY);
      samplesCountField->setValue(PLACEHOLDER);
      elapsedField->setValueColor(Color::GRAY);
      elapsedField->setValue(PLACEHOLDER);
      actualRateField->setValueColor(Color::GRAY);
      actualRateField->setValue(PLACEHOLDER);

      float remainingSeconds = max(0.0, warmupPeriodS - warmupStopwatch.elapsedSecs());
      String warmupText = String(remainingSeconds, 1) + "s remaining";
      warmupStatusField->setValueColor(Color::VALUE2);
      warmupStatusField->setValue(warmupText);

      warmupStatusField->draw();
      progressField->draw();
      samplesCountField->draw();
      elapsedField->draw();
      actualRateField->draw();
      return;
   }

   warmupStatusField->setValueColor(Color::VALUE);
   warmupStatusField->setValue("Complete");

   size_t count = samplesValues->count();
   float elapsedSeconds = captureStopwatch.elapsedSecs();
   if (elapsedSeconds > samplingDurationS)
   {
      elapsedSeconds = samplingDurationS;
   }

   float samplePercent = (count * 100.0f) / maxSamples;
   float timePercent = (elapsedSeconds * 100.0f) / samplingDurationS;

   float progressPercent = max(samplePercent, timePercent);
   if (progressPercent > 100.0f)
   {
      progressPercent = 100.0f;
   }

   progressField->setValueColor(Color::VALUE);
   progressField->setValue(progressPercent);
   samplesCountField->setValueColor(Color::VALUE);
   samplesCountField->setValue(static_cast<unsigned long>(count));
   elapsedField->setValueColor(Color::VALUE);
   elapsedField->setValue(static_cast<unsigned long>(elapsedSeconds));
   actualRateField->setValueColor(Color::VALUE);
   actualRateField->setValue(actualSampleRate.get());

   warmupStatusField->draw();
   progressField->draw();
   samplesCountField->draw();
   elapsedField->draw();
   actualRateField->draw();
}

///
/// <summary>
/// Finalizes capture and prints summaries.
/// </summary>
///
void finishCapture()
{
   if (captureFinalized)
   {
      return;
   }

   captureFinalized = true;
   printBoundaryDump();
   resultViewSelector.reset(ResultView::Charts);
   drawResultView();
}

///
/// <summary>
/// Starts a capture run using the currently confirmed setup values.
/// </summary>
///
void startCapture()
{
   captureStarted = true;
   createSamplesValues();
   samplesValues->reset();
   samplingTimer.setDurationMs(1000UL / targetSampleRate);
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
   targetRateField->setValue(targetSampleRate);
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

   setupDisplay.load();
   setupDisplay.run();
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
         setupDisplay.run();
         startCapture();
         return;
      }

      if (resultViewSelector.hasChanged())
      {
         drawResultView();
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
            // Transitioning from warmup to real capture. Avoid forcing an immediate
            // full redraw here (and avoid a blocking Serial write) since that adds
            // extra work to this one sample iteration; let the normal, possibly
            // throttled updateDisplay() call below pick up the change instead.
            warmupStopwatch.stop();
         }

         samplesValues->addValue(sensorValue);

         updateDisplay();

         bool durationElapsed = captureStopwatch.elapsedSecs() >= samplingDurationS;
         if ((samplesValues->count() >= maxSamples) || durationElapsed)
         {
            finishCapture();
         }
      }
   }
}

