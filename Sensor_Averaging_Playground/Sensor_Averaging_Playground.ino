//
// Captures finite sensor values for a fixed run window (or until the sample cap is reached), stores
// them in RAM, and reports serial/display summaries.
//
// On startup, a setup screen lets the user review/adjust the target sample rate, max samples,
// sampling duration, and warmup period (Encoder A selects a field, Encoder B adjusts it, Button B
// resets to defaults). Press Button A to confirm and start the capture using the selected values.
//
// Capture flow:
// 1) Initialize display, serial, and sensor.
// 2) Load setup values from Preferences and run the setup screen; start capture on Button A.
// 3) Sample at up to the selected target rate and store finite values in RAM.
// 4) Stop when MAX_SAMPLES are stored or SAMPLING_DURATION_S elapses.
//
// Output flow:
// - While collecting, the Playground display shows capture progress plus target/actual sample rate.
// - After capture, the display renders a value histogram with min/max labels plus StdDev%/effective-rate
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
#include <array>

#include "ESP32_S3_Playground.h"
#include "Table.h"
#include "EnumSelector.h"
#include "Histogram.h"
#include "HistogramPlot.h"
#include "RollingRate.h"
#include "ScatterPlot.h"
#include "SerialTable.h"
#include "SerialX.h"
#include "FieldTableEditor.h"
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
constexpr size_t DEFAULT_MAX_SAMPLES = 1000;
constexpr size_t MIN_MAX_SAMPLES = 100;
constexpr size_t MAX_MAX_SAMPLES = 5000;
constexpr unsigned long MIN_SAMPLING_DURATION_S = 5;
constexpr unsigned long MAX_SAMPLING_DURATION_S = 60;
constexpr unsigned long DURATION_STEP_S = 5;
constexpr unsigned long DEFAULT_WARMUP_PERIOD_S = 0;
constexpr unsigned long MAX_WARMUP_PERIOD_S = 30;
constexpr unsigned long WARMUP_STEP_S = 1;

// ----------- Preferences Namespace
constexpr const char* PREF_NAMESPACE = "sensor_avg";

RollingRate actualSampleRate;

// ----------- Setup Screen Fields
ScaledStepIntEditor rateField(
   MIN_SAMPLE_RATE_PER_SEC, MAX_SAMPLE_RATE_PER_SEC, DEFAULT_SAMPLE_RATE_PER_SEC, "####/s");
ScaledStepIntEditor samplesEditor(
   MIN_MAX_SAMPLES, MAX_MAX_SAMPLES, DEFAULT_MAX_SAMPLES, "#####");
IntEditor durationEditor(
   MIN_SAMPLING_DURATION_S, MAX_SAMPLING_DURATION_S, DURATION_STEP_S, DEFAULT_SAMPLING_DURATION_S, "###s");
IntEditor warmupEditor(
   0, MAX_WARMUP_PERIOD_S, WARMUP_STEP_S, DEFAULT_WARMUP_PERIOD_S, "###s");

FieldTableEditor::Row setupCells[] = { { "Rate", &rateField }, { "Max Samples", &samplesEditor }, { "Max Duration", &durationEditor }, { "Warmup", &warmupEditor } };
FieldTableEditor setupDisplay(&arduino, PREF_NAMESPACE, setupCells);

Values samplesValues;
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
   SerialTable table("Warmup/Real Boundary Sample Dump", columns);
   table.printHeader();

   size_t warmupCount = warmupValues.count();
   size_t warmupStart = (warmupCount > NUM_BOUNDARY_SAMPLES) ? (warmupCount - NUM_BOUNDARY_SAMPLES) : 0;
   unsigned long previousTimestampMs = 0;
   bool havePreviousTimestamp = false;

   for (size_t i = warmupStart; i < warmupCount; i++)
   {
      unsigned long timestampMs = warmupValues.timestamp(i);
      long deltaMs = havePreviousTimestamp ? static_cast<long>(timestampMs - previousTimestampMs) : 0;
      table.printRow("warmup", timestampMs, havePreviousTimestamp ? String(deltaMs) : String("-"),
         SerialTable::fixed(warmupValues[i], 3));
      previousTimestampMs = timestampMs;
      havePreviousTimestamp = true;
   }

   size_t realCount = min(static_cast<size_t>(NUM_BOUNDARY_REAL_SAMPLES), samplesValues.count());
   for (size_t i = 0; i < realCount; i++)
   {
      unsigned long timestampMs = samplesValues.timestamp(i);
      long deltaMs = havePreviousTimestamp ? static_cast<long>(timestampMs - previousTimestampMs) : 0;
      table.printRow("real", timestampMs, havePreviousTimestamp ? String(deltaMs) : String("-"),
         SerialTable::fixed(samplesValues[i], 3));
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
   samplesValues.reset(samplesEditor.get());
}

///
/// <summary>
/// (Re)configures the warmup Values object used to record warmup-phase samples for display only;
/// these samples are excluded from the histogram and statistics.
/// </summary>
///
void createWarmupValues()
{
   size_t warmupMaxValues = (warmupEditor.get() > 0) ? (warmupEditor.get() * rateField.get() + 1) : 0;
   warmupValues.reset(warmupMaxValues);
}

// ----------- Display Items
constexpr uint16_t DISPLAY_UPDATE_RATE_PER_SEC = 10;
RateTimer displayRefreshTimer(DISPLAY_UPDATE_RATE_PER_SEC);

bool collectingViewInitialized = false;
enum CollectingRow : size_t { WARMUP_ROW, MAX_ROW, PROGRESS_ROW, SAMPLES_ROW, ELAPSED_ROW, TARGET_RATE_ROW, ACTUAL_RATE_ROW };

static const Table::ValueRow collectingRows[] = {
   { "Warmup", "####################" },
   { "Max", "####################" },
   { "Progress", "###%" },
   { "Samples", "#####" },
   { "Elapsed", "####s" },
   { "Target Rate", "####/s" },
   { "Actual Rate", "####.#/s" },
};
Table collectingTable(&arduino, 0, 0, collectingRows, 2, Table::Alignment::RIGHT);

// ----------- Analysis Settings
constexpr size_t HISTOGRAM_BINS = 20;
constexpr const char* CHART_MIN_MAX_FORMAT = "##.##";
constexpr const char* CHART_X_AXIS_FORMAT = "####";
constexpr const char* CHART_Y_AXIS_FORMAT = "####";
constexpr size_t BUFFER_SIZES[] = { 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 };

ScatterPlot resultsScatterPlot(&arduino, Rect16{ 0, 0, 0, 0 }, CHART_X_AXIS_FORMAT, CHART_Y_AXIS_FORMAT);
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
void drawHistogram(const char* title, const Histogram& histogram, uint16_t sectionLeft, uint16_t sectionWidth, uint16_t sectionTop, uint16_t sectionHeight, Color barColor, Color axisLabelColor, const char* yAxisFormat = nullptr)
{
   arduino.setTextSize(2);
   arduino.setCursor(sectionLeft, sectionTop);
   arduino.println(title, Color::LABEL);

   uint16_t chartTop = static_cast<uint16_t>(arduino.getCursorY() + 1);
   uint16_t adjustedHeight = sectionHeight - (chartTop - sectionTop);
   Rect16 rect{ sectionLeft, chartTop, sectionWidth, adjustedHeight };
   HistogramPlot plot(&arduino, histogram, rect, CHART_MIN_MAX_FORMAT, yAxisFormat, barColor);
   plot.setAxisLabelColor(axisLabelColor);
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
void drawScatterPlot(uint16_t sectionLeft, uint16_t sectionWidth, uint16_t sectionTop, uint16_t sectionHeight, Color pointColor)
{
   Rect16 sectionRect{ sectionLeft, sectionTop, sectionWidth, sectionHeight };
   resultsScatterPlot.setRect(static_cast<int16_t>(sectionRect.x), static_cast<int16_t>(sectionRect.y),
      static_cast<int16_t>(sectionRect.width), static_cast<int16_t>(sectionRect.height));
   resultsScatterPlot.deleteAllSeries();

   size_t warmupCount = warmupValues.count();
   const float* warmupValueData = warmupValues.values();

   size_t count = samplesValues.count();
   const float* values = samplesValues.values();

   if (warmupCount > 0)
   {
      ScatterPlotSeries* warmupSeries = resultsScatterPlot.createSeries(warmupCount);
      warmupSeries->showPoints = true;
      warmupSeries->showLines = false;
      warmupSeries->color = Color::LIGHTGRAY;

      // The full index range [0, warmupCount - 1] is known up front, so lock the X axis
      // and store this series as fixed-range bins instead of a raw array.
      warmupSeries->setFixedXRange(0.0f, static_cast<float>(warmupCount - 1), warmupCount);

      for (size_t i = 0; i < warmupCount; i++)
      {
         warmupSeries->add(static_cast<float>(i), warmupValueData[i]);
      }
      warmupSeries->finalized = true;
   }

   ScatterPlotSeries* series = resultsScatterPlot.createSeries((count > 0) ? count : 1);
   series->showPoints = true;
   series->showLines = false;
   series->color = pointColor;

   // The full index range [warmupCount, warmupCount + count - 1] is known up front, so
   // lock the X axis and store this series as fixed-range bins instead of a raw array.
   if (count > 0)
   {
      series->setFixedXRange(static_cast<float>(warmupCount), static_cast<float>(warmupCount + count - 1), count);
   }

   for (size_t i = 0; i < count; i++)
   {
      series->add(static_cast<float>(warmupCount + i), values[i]);
   }
   series->finalized = true;

   resultsScatterPlot.draw();
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

   Histogram valueHistogram(samplesValues.values(), samplesValues.count(), HISTOGRAM_BINS);

   uint16_t top = static_cast<uint16_t>(arduino.getCursorY());
   uint16_t availableHeight = arduino.height() - top;
   uint16_t totalWidth = arduino.width();
   constexpr int16_t sectionGap = 5;

   uint16_t plotHeight = (availableHeight - sectionGap) / 2;
   uint16_t scatterHeight = plotHeight;
   uint16_t histogramTop = top + scatterHeight + sectionGap;
   uint16_t histogramHeight = availableHeight - scatterHeight - sectionGap;

   drawScatterPlot(0, totalWidth, top, scatterHeight, Color::LIME);

   // Use the same Y-axis format string as the scatter plot so both charts reserve an
   // identically sized left-side label column and their x-axes line up visually.
   drawHistogram("", valueHistogram, 0, totalWidth, histogramTop, histogramHeight, Color::LIME, Color::WHITE, CHART_Y_AXIS_FORMAT);
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
   String headerText = String(samplesValues.count()) + " samples collected in " + String(elapsedSeconds) + " seconds";
   arduino.println(headerText, Color::LABEL);

   String rateText = "Target " + String(rateField.get()) + "/s  Actual " + String(actualSampleRate.get(), 0) + "/s";
   arduino.println(rateText, Color::LABEL);
   arduino.println();

   static std::array columns = {
      Table::Column("", Table::Alignment::RIGHT),
      Table::Column("Range", "###.##", Table::Alignment::RIGHT),
      Table::Column("StdDev", "###.##", Table::Alignment::RIGHT),
      Table::Column("StdDev%", "###.##%", Table::Alignment::RIGHT),
      Table::Column("Hz", " ####", Table::Alignment::RIGHT),
   };
   static Table table(&arduino, 0, 0, columns, 2, Color::VALUE3);
   table.setPosition(arduino.getCursorX(), arduino.getCursorY());
   table.clearRows();

   Stats rawStats = samplesValues.computeBasicStats();
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
      table.addRow("Raw", Color::VALUE2);
      table.setValue(0, 0, rawRange, Color::VALUE2);
      table.setValue(0, 1, rawStdDev, Color::VALUE2);
      table.setValue(0, 2, rawStdDevPercent, Color::VALUE2);
      table.setValue(0, 3, actualSampleRate.get(), Color::VALUE2);
   }
   else
   {
      table.addRow("Raw", Color::GRAY);
      table.setNoValue(0, 0, Color::GRAY, '-');
      table.setNoValue(0, 1, Color::GRAY, '-');
      table.setNoValue(0, 2, Color::GRAY, '-');
      table.setNoValue(0, 3, Color::GRAY, '-');
   }

   for (size_t i = 0; i < NUM_BUFFER_SIZES; i++)
   {
      size_t sampleSize = BUFFER_SIZES[i];
      float effectiveRateHz = actualSampleRate.get() / sampleSize;

      Stats avgSeriesStats = samplesValues.computeAverageSeriesStats(sampleSize);
      float avgMean = avgSeriesStats.get();
      float avgRange = Values::computeRange(avgSeriesStats.min(), avgSeriesStats.max());
      float avgStdDev = avgSeriesStats.stdDev();
      size_t averageCount = avgSeriesStats.count();

      float avgStdDevPercent = NAN;
      if (isfinite(avgMean) && (fabsf(avgMean) > 0.0f) && isfinite(avgStdDev))
      {
         avgStdDevPercent = (avgStdDev / fabsf(avgMean)) * 100.0f;
      }

      size_t rowIndex = i + 1;
      String rowLabel = String(sampleSize);

      if ((averageCount > 0) && isfinite(avgStdDevPercent))
      {
         table.addRow(rowLabel.c_str(), Color::VALUE);
         table.setValue(rowIndex, 0, avgRange, Color::VALUE);
         table.setValue(rowIndex, 1, avgStdDev, Color::VALUE);
         table.setValue(rowIndex, 2, avgStdDevPercent, Color::VALUE);
         table.setValue(rowIndex, 3, effectiveRateHz, Color::VALUE);
      }
      else
      {
         table.addRow(rowLabel.c_str(), Color::GRAY);
         table.setNoValue(rowIndex, 0, Color::GRAY, '-');
         table.setNoValue(rowIndex, 1, Color::GRAY, '-');
         table.setNoValue(rowIndex, 2, Color::GRAY, '-');
         table.setNoValue(rowIndex, 3, Color::GRAY, '-');
      }
   }

   table.draw();
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
/// Initializes the Table rows used by the collecting-progress screen.
/// </summary>
///
void initializeCollectingTable()
{
   arduino.setTextSize(3);
   int16_t collectingTableY = arduino.charH();

   arduino.setTextSize(2);
   collectingTableY += arduino.charH();

   collectingTable.setPosition(0, collectingTableY);
}

///
/// <summary>
/// Updates the collecting-progress screen shown on the Playground display. Max (samples/
/// duration) and Target Rate are static once capture starts; Progress, Samples, Elapsed,
/// and Actual Rate show placeholder "----" values while warming up, then updated values during
/// capture. The Warmup row (shown first) shows a countdown while warming up and "Complete"
/// afterward. Refresh is throttled to DISPLAY_UPDATE_RATE_PER_SEC unless forceRefresh is true.
/// </summary>
/// <param name="forceRefresh">When true, bypasses the refresh-rate throttle and redraws immediately.</param>
///
void updateDisplay(bool forceRefresh = false)
{
   bool durationElapsed = captureStopwatch.elapsedSecs() >= durationEditor.get();
   if (!warmupStopwatch.isRunning() && (samplesValues.isFull() || durationElapsed))
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
      collectingTable.invalidate();
      collectingViewInitialized = true;
   }

   String maxText = String(samplesEditor.get()) + " samples OR " + String(durationEditor.get()) + "s";
   collectingTable.setValue(MAX_ROW, maxText);
   collectingTable.setValue(TARGET_RATE_ROW, static_cast<int>(rateField.get()));

   if (warmupStopwatch.isRunning())
   {
      constexpr const char* PLACEHOLDER = "----";

      float remainingSeconds = max(0.0, warmupEditor.get() - warmupStopwatch.elapsedSecs());
      String warmupText = String(remainingSeconds, 1) + "s remaining";

      collectingTable.setValue(WARMUP_ROW, warmupText, Color::VALUE2);
      collectingTable.setValue(PROGRESS_ROW, PLACEHOLDER, Color::GRAY);
      collectingTable.setValue(SAMPLES_ROW, PLACEHOLDER, Color::GRAY);
      collectingTable.setValue(ELAPSED_ROW, PLACEHOLDER, Color::GRAY);
      collectingTable.setValue(ACTUAL_RATE_ROW, PLACEHOLDER, Color::GRAY);
      collectingTable.draw();
      return;
   }

   size_t count = samplesValues.count();
   float elapsedSeconds = captureStopwatch.elapsedSecs();
   if (elapsedSeconds > durationEditor.get())
   {
      elapsedSeconds = durationEditor.get();
   }

   float samplePercent = (count * 100.0f) / samplesEditor.get();
   float timePercent = (elapsedSeconds * 100.0f) / durationEditor.get();

   float progressPercent = max(samplePercent, timePercent);
   if (progressPercent > 100.0f)
   {
      progressPercent = 100.0f;
   }

   collectingTable.setValue(WARMUP_ROW, "Complete", Color::VALUE);
   collectingTable.setValue(PROGRESS_ROW, progressPercent, Color::VALUE);
   collectingTable.setValue(SAMPLES_ROW, static_cast<unsigned long>(count), Color::VALUE);
   collectingTable.setValue(ELAPSED_ROW, static_cast<unsigned long>(elapsedSeconds));
   collectingTable.setValue(ACTUAL_RATE_ROW, actualSampleRate.get());
   collectingTable.draw();
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
   samplesValues.reset();
   samplingTimer.setDurationMs(1000UL / rateField.get());
   actualSampleRate.reset();
   running = true;
   captureStopwatch.reset();
   captureStopwatch.start();
   warmupStopwatch.reset();
   if (warmupEditor.get() > 0)
   {
      warmupStopwatch.start();
   }
   collectingViewInitialized = false;
   createWarmupValues();
   boundaryDumpPrinted = false;
   updateDisplay(true);
   Serial.println(warmupStopwatch.isRunning() ? "Warming up..." : "Capture started...");
}

///
/// <summary>
/// Runs a blocking setup screen using setupDisplay: Encoder A selects a field, Encoder B
/// adjusts it, Button B resets all fields to their defaults, and Button A confirms and saves.
/// </summary>
///
void runSetupScreen()
{
   arduino.clearDisplay();
   arduino.setCursor(0, 0);
   arduino.setTextSize(3);
   arduino.println("Setup", Color::HEADING);

   arduino.setTextSize(2);
   int16_t tableTop = arduino.getCursorY();
   setupDisplay.setPosition(0, tableTop);
   setupDisplay.forceRedraw();
   setupDisplay.draw();

   arduino.setCursor(0, tableTop + setupDisplay.height());
   arduino.println();

   static constexpr const char* INSTRUCTIONS[] = {
      "Encoder A: Select",
      "Encoder B: Adjust",
      "Button A: Confirm",
      "Button B: Reset",
   };

   int16_t rowHeight = arduino.charH();
   int16_t y = (int16_t)arduino.height() - rowHeight * (int16_t)ARRAY_SIZE(INSTRUCTIONS);
   for (size_t i = 0; i < ARRAY_SIZE(INSTRUCTIONS); i++)
   {
      int16_t x = (int16_t)arduino.width() - (int16_t)arduino.textWidth(INSTRUCTIONS[i]);
      arduino.setCursor(x, y);
      arduino.println(INSTRUCTIONS[i], Color::GRAY);
      y += rowHeight;
   }

   while (true)
   {
      int32_t selectDelta = arduino.encoderA.delta();
      int32_t adjustDelta = arduino.encoderB.delta();
      if (selectDelta != 0 || adjustDelta != 0)
      {
         setupDisplay.selectNext(selectDelta);
         setupDisplay.adjustSelected(adjustDelta);
         setupDisplay.draw();
      }

      if (arduino.buttonB.wasPressed())
      {
         setupDisplay.reset();
         setupDisplay.draw();
      }

      if (arduino.buttonA.wasPressed())
      {
         setupDisplay.save();
         return;
      }
   }
}

void setup()
{
   SerialX::begin(115200, 2000);
   Wire.begin();
   arduino.begin();

   sensor.begin();
   initializeCollectingTable();

   setupDisplay.load();
   runSetupScreen();
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
         runSetupScreen();
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

      if (warmupStopwatch.elapsedSecs() < warmupEditor.get())
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

         samplesValues.addValue(sensorValue);

         updateDisplay();

         bool durationElapsed = captureStopwatch.elapsedSecs() >= durationEditor.get();
         if ((samplesValues.count() >= samplesEditor.get()) || durationElapsed)
         {
            finishCapture();
         }
      }
   }
}

