//
// Captures finite temperature samples for a fixed run period (or until the sample cap is reached),
// stores them in RAM, and reports serial summaries plus an automatic serial dump.
//
// Capture flow:
// 1) Initialize serial, display, and sensor.
// 2) Show a combined setup/capture table letting the user review/adjust the max sample count
//    and max capture time (Encoder A selects a field, Encoder B adjusts it, Button B resets to
//    defaults) before sampling starts. Press Button A to confirm and start sampling; the same
//    table then switches to showing live Samples/Time/Progress rows.
// 3) Sample at up to MAX_SAMPLING_RATE_PER_SEC and store finite values in RAM.
// 4) Stop when the selected max sample count is stored or the selected max capture time is reached.
//
// Output flow:
// - Display shows live progress during capture, including elapsed seconds.
// - After capture, encoderA cycles display modes: summary, histogram, post warm-up histogram, and scatter plot.
// - After capture, pressing Button A returns to the setup screen so another capture can be configured and run.
// - Serial summary includes run metrics, value stats, histogram bins, and warm-up analysis.
// - Stored points are dumped to serial automatically after capture completes.
//
// Sensor mode:
// - Reads from TestSensor (configurable via TestSensor.h using alias).
//
#include "ArduinoBoard.h"

#ifndef ARDUINO_PLAYGROUND_SUPPORTED
#error "This sketch requires a Playground board (e.g. ESP32-S3 Dev Module wired as a Playground)."
#endif
#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include "Values.h"
#include "Timer.h"
#include "SerialX.h"
#include "SerialTable.h"
#include "SerialHistogram.h"
#include "HistogramPlot.h"
#include "DisplayTable.h"
#include "DisplayTableCellEditor.h"
#include "DisplayTableEditor.h"
#include "ScatterPlot.h"
#include <math.h>
#include "TestSensor.h"

constexpr unsigned long MAX_SAMPLING_RATE_PER_SEC = 100;
// ----- capture configuration
constexpr size_t DEFAULT_MAX_SAMPLES = 5000;
constexpr size_t MIN_MAX_SAMPLES = 100;
constexpr size_t MAX_MAX_SAMPLES = 5000;
constexpr size_t MAX_SAMPLES_STEP = 100;
constexpr unsigned long DEFAULT_MAX_CAPTURE_TIME_S = 120;
constexpr unsigned long MIN_MAX_CAPTURE_TIME_S = 5;
constexpr unsigned long MAX_MAX_CAPTURE_TIME_S = 300;
constexpr unsigned long MAX_CAPTURE_TIME_STEP_S = 5;

// ----- Preferences namespace
constexpr const char* PREF_NAMESPACE = "sensor_capture";

// ----- histogram display
constexpr size_t MIN_HISTOGRAM_BINS = 5;
constexpr size_t MAX_HISTOGRAM_BINS = 50;

// ----- serial output
constexpr unsigned long PRINT_INTERVAL_MS = 2;

// ----- display refresh
constexpr uint16_t DISPLAY_UPDATE_RATE_PER_SEC = 5;

// ----- warm-up analysis
// Number of samples used for each side of the warm-up comparison (start segment vs end segment).
constexpr size_t STARTUP_SAMPLE_COUNT = 100;

// ----- sampling rate analysis
constexpr size_t SAMPLING_RATES[] = { 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 };
constexpr size_t SAMPLING_RATE_COUNT = sizeof(SAMPLING_RATES) / sizeof(SAMPLING_RATES[0]);

// ----- analysis table
constexpr const char* ANALYSIS_TABLE_TITLE = "Sample Analysis";
const SerialTable::Column ANALYSIS_TABLE_COLUMNS[] = {
   { "N", 8 },
   { "Rate", 10 },
   { "Range", 12 },
   { "StdDev", 12 },
};
SerialTable analysisTable(ANALYSIS_TABLE_TITLE, ANALYSIS_TABLE_COLUMNS);

// ----- warm-up analysis table
constexpr const char* WARMUP_TABLE_TITLE = "Warm-up Stability Analysis";
const SerialTable::Column WARMUP_TABLE_COLUMNS[] = {
   { "Metric", 12 },
   { "Start", 12 },
   { "End", 12 },
   { "Delta", 12 },
};
SerialTable warmupTable(WARMUP_TABLE_TITLE, WARMUP_TABLE_COLUMNS);

// ----- display modes
enum class DisplayMode : uint8_t
{
   Summary = 0,
   Histogram,
   PostWarmupHistogram,
   ScatterPlot,
   Count
};

// ----- board and sensor
Arduino arduino;
TestSensor sensor;

// ----- display value formats
constexpr const char* PROGRESS_PERCENT_FORMAT = "###%";
constexpr const char* SAMPLES_FORMAT = "#####";
constexpr const char* TIME_FORMAT = "###s";
constexpr const char* STATS_FORMAT = "######.##";
constexpr const char* STDDEV_PERCENT_FORMAT = "##.##%";
constexpr const char* RATE_FORMAT = "#####/s";

// ----- capture state
bool captureStarted = false;
bool captureFinalized = false;
unsigned long captureStartMs = 0;
RateTimer displayRefreshTimer(DISPLAY_UPDATE_RATE_PER_SEC);
DisplayMode displayMode = DisplayMode::Summary;
size_t postWarmupStartIndex = 0;
bool postWarmupReady = false;

Values sensorCapture;
Timer samplingTimer((1000UL / MAX_SAMPLING_RATE_PER_SEC) == 0 ? 1UL : (1000UL / MAX_SAMPLING_RATE_PER_SEC));

// ----- serial dump state
bool serialDumpStarted = false;
bool serialDumpComplete = false;
size_t serialDumpIndex = 0;
size_t serialDumpCount = 0;
unsigned long lastSerialDumpMs = 0;

// ----- combined setup/capture table (editable capture limits plus live progress)
// Max Samples/Max Time are editable via Encoder A/B until capture starts, at which point they
// become disabled (grayed out, skipped by selection) since the sample buffer is already sized
// to the confirmed value. Samples/Time/Progress are read-only rows updated live during capture.
long maxSamples = DEFAULT_MAX_SAMPLES;
long maxCaptureTimeS = DEFAULT_MAX_CAPTURE_TIME_S;
float progressSamplesValue = 0.0f;
float progressTimeValue = 0.0f;
float progressPercentValue = 0.0f;

///
/// <summary>
/// Editable capture-limit field that becomes disabled once capture has started, since the
/// sample buffer is already sized to the confirmed value at that point.
/// </summary>
///
class CaptureLimitCell : public IntCellEditor
{
public:
   using IntCellEditor::IntCellEditor;

   bool isEnabled() const override
   {
      return !captureStarted;
   }
};

CaptureLimitCell samplesCell(&maxSamples,
   MIN_MAX_SAMPLES, MAX_MAX_SAMPLES, MAX_SAMPLES_STEP, DEFAULT_MAX_SAMPLES, Format(SAMPLES_FORMAT, Format::Alignment::LEFT));
CaptureLimitCell durationCell(&maxCaptureTimeS,
   MIN_MAX_CAPTURE_TIME_S, MAX_MAX_CAPTURE_TIME_S, MAX_CAPTURE_TIME_STEP_S, DEFAULT_MAX_CAPTURE_TIME_S, Format(TIME_FORMAT, Format::Alignment::LEFT));
ReadOnlyCell samplesReadCell(&progressSamplesValue, SAMPLES_FORMAT);
ReadOnlyCell timeReadCell(&progressTimeValue, TIME_FORMAT);
ReadOnlyCell progressReadCell(&progressPercentValue, PROGRESS_PERCENT_FORMAT);

TableEditorRow captureCells[] =
{
   { "Max Samples", &samplesCell },
   { "Max Time", &durationCell },
   { "Samples", &samplesReadCell },
   { "Time", &timeReadCell },
   { "Progress", &progressReadCell },
};
DisplayTableEditor captureTable(&arduino, PREF_NAMESPACE, captureCells, 0, 0);

// ----- display tables
DisplayTable summaryTable(&arduino, 0, 0);

/// <summary>
/// Initializes display tables used by summary and collecting screens.
/// </summary>
void initializeDisplayTables()
{
   arduino.setTextSize(3);
   int16_t summaryTableY = arduino.charH();

   arduino.setTextSize(2);
   int16_t captureTableY = summaryTableY;

   summaryTable = DisplayTable(&arduino, 0, summaryTableY);
   summaryTable.addRow("Samples", SAMPLES_FORMAT);
   summaryTable.addRow("Time", TIME_FORMAT);
   summaryTable.addRow("Rate", RATE_FORMAT);
   summaryTable.addRow("Avg", STATS_FORMAT, Color::VALUE2);
   summaryTable.addRow("StdDev", STATS_FORMAT, Color::VALUE3);
   summaryTable.addRow("StdDev%", STDDEV_PERCENT_FORMAT, Color::VALUE3);

   captureTable.setPosition(0, captureTableY);
}

/// <summary>
/// Draws the summary display mode after capture completion.
/// </summary>
void renderDisplaySummary()
{
   arduino.clearDisplay();
   arduino.setTextSize(3);
   arduino.setCursor(0, 0);
   arduino.println("Capture Summary", Color::HEADING);

   unsigned long captureTimeMs = millis() - captureStartMs;
   unsigned long captureTimeSec = captureTimeMs / 1000UL;
   unsigned long sampleCount = sensorCapture.count();
   float samplesPerSecond = (captureTimeMs > 0) ? (sampleCount * 1000.0f / captureTimeMs) : 0.0f;

   Stats basicStats = sensorCapture.computeBasicStats();
   float stdDevPercent = NAN;
   float avg = basicStats.get();
   float stdDev = basicStats.stdDev();
   if (isfinite(avg) && (fabsf(avg) > 0.0f) && isfinite(stdDev))
   {
      stdDevPercent = (stdDev / fabsf(avg)) * 100.0f;
   }

   arduino.setTextSize(2);
   summaryTable.invalidate();
   summaryTable.setValue(0, sampleCount);
   summaryTable.setValue(1, captureTimeSec);
   summaryTable.setValue(2, samplesPerSecond);
   summaryTable.setValue(3, avg);
   summaryTable.setValue(4, stdDev);
   summaryTable.setValue(5, isfinite(stdDevPercent) ? String(stdDevPercent, 2) + "%" : "n/a");

   summaryTable.draw();
}

/// <summary>
/// Draws a histogram for a selected capture value range.
/// </summary>
/// <param name="startIndex">First sample index to include.</param>
/// <param name="top">Top Y coordinate of the histogram area.</param>
/// <param name="height">Histogram drawing height in pixels.</param>
void drawHistogram(size_t startIndex, uint16_t top, uint16_t height)
{
   const float* values = sensorCapture.values() + startIndex;
   size_t valueCount = sensorCapture.count() - startIndex;
   Histogram histogram(values, valueCount, MIN_HISTOGRAM_BINS, MAX_HISTOGRAM_BINS);

   Rect16 rect{ 0, top, arduino.width(), height };
   HistogramPlot plot(&arduino, histogram, rect, "##.##");
   plot.render();
}

/// <summary>
/// Draws a histogram for only the post-warmup data range.
/// </summary>
void renderDisplayPostWarmupHistogram()
{
   arduino.clearDisplay();
   arduino.setTextSize(3);
   arduino.setCursor(0, 0);
   arduino.println("Post Warm-up Samples", Color::HEADING);

   if (!postWarmupReady || (postWarmupStartIndex >= sensorCapture.count()))
   {
      arduino.setTextSize(2);
      arduino.println("Not enough data", Color::LABEL);
      return;
   }

   uint16_t top = arduino.getCursorY();
   uint16_t h = arduino.height() - top - 2;
   drawHistogram(postWarmupStartIndex, top, h);
}

/// <summary>
/// Draws a compact histogram display mode for all captured samples.
/// </summary>
void renderDisplayHistogram()
{
   arduino.clearDisplay();
   arduino.setTextSize(3);
   arduino.setCursor(0, 0);
   arduino.println("All Samples", Color::HEADING);

   uint16_t top = arduino.getCursorY();
   uint16_t h = arduino.height() - top - 2;
   drawHistogram(0, top, h);
}

/// <summary>
/// Draws a compact x-y scatter plot for all captured samples.
/// </summary>
void renderDisplayScatterPlot()
{
   arduino.clearDisplay();
   arduino.setTextSize(3);
   arduino.setCursor(0, 0);
   arduino.println("Scatter Plot", Color::HEADING);

   size_t totalCount = sensorCapture.count();
   if (totalCount < 2)
   {
      arduino.setTextSize(2);
      arduino.println("Not enough data", Color::LABEL);
      return;
   }

   arduino.setTextSize(2);
   uint16_t plotTop = arduino.getCursorY();
   uint16_t plotHeight = arduino.height() - plotTop - 2;
   uint16_t plotWidth = arduino.width();

   if (plotHeight < 20 || plotWidth < 20)
   {
      arduino.setTextSize(2);
      arduino.println("Plot area too small", Color::LABEL);
      return;
   }

   ScatterPlot plot(&arduino, 0, plotTop, plotWidth, plotHeight, SAMPLES_FORMAT, STATS_FORMAT);
   ScatterPlotSeries* series = plot.createSeries(totalCount);
   const float* values = sensorCapture.values();
   for (size_t i = 0; i < totalCount; i++)
   {
      series->add(static_cast<float>(i), values[i]);
   }
   plot.draw();
}

   /// <summary>
   /// Updates the progress and capture display.
   /// </summary>
   void updateDisplayProgress(bool forceRefresh = false)
   {
      unsigned long nowMs = millis();
      if (forceRefresh)
      {
         displayRefreshTimer.reset();
      }
      else if (!displayRefreshTimer.ready())
      {
         return;
      }

      if (sensorCapture.isFull() || ((nowMs - captureStartMs) >= (static_cast<unsigned long>(maxCaptureTimeS) * 1000UL)))
      {
         switch (displayMode)
         {
         case DisplayMode::Summary:
            renderDisplaySummary();
            break;
         case DisplayMode::Histogram:
            renderDisplayHistogram();
            break;
         case DisplayMode::PostWarmupHistogram:
            renderDisplayPostWarmupHistogram();
            break;
         case DisplayMode::ScatterPlot:
         default:
            renderDisplayScatterPlot();
            break;
         }
         return;
      }

      arduino.setTextSize(3);
      arduino.setCursor(0, 0);
      arduino.print("Sensor Capture", Color::HEADING);

      arduino.setTextSize(2);

      unsigned long count = sensorCapture.count();
      unsigned long elapsedSeconds = (nowMs - captureStartMs) / 1000UL;
      if (elapsedSeconds > static_cast<unsigned long>(maxCaptureTimeS))
      {
         elapsedSeconds = static_cast<unsigned long>(maxCaptureTimeS);
      }

      float samplePercent = (maxSamples > 0) ? ((count * 100.0f) / maxSamples) : 0.0f;
      float timePercent = (maxCaptureTimeS > 0) ? ((elapsedSeconds * 100.0f) / maxCaptureTimeS) : 0.0f;

      progressSamplesValue = static_cast<float>(count);
      progressTimeValue = static_cast<float>(elapsedSeconds);
      progressPercentValue = min(max(samplePercent, timePercent), 100.0f);

      captureTable.draw();
   }

/// <summary>
/// Computes and prints capture statistics to Serial.
/// </summary>
void printCaptureSummary()
{
   Stats basicStats = sensorCapture.computeBasicStats();

   float valueAvg = basicStats.get();
   float valueStdDev = basicStats.stdDev();
   float valueMin = basicStats.min();
   float valueMax = basicStats.max();
   float valueRange = Values::computeRange(valueMin, valueMax);
   float valueStdDevPercent = NAN;
   if (isfinite(valueAvg) && (fabsf(valueAvg) > 0.0f) && isfinite(valueStdDev))
   {
      valueStdDevPercent = (valueStdDev / fabsf(valueAvg)) * 100.0f;
   }
   unsigned long captureTimeMs = millis() - captureStartMs;
   float samplesPerSecond = (captureTimeMs > 0) ? (sensorCapture.count() * 1000.0f / captureTimeMs) : 0.0f;

   Serial.println();
   Serial.println("Capture Summary");
   SerialX::print("Capture Time", 20);
   SerialX::println(String(captureTimeMs) + " ms", 20);
   SerialX::print("Samples", 20);
   SerialX::println(sensorCapture.count(), 20);
   SerialX::print("Rate", 20);
   SerialX::println(String(samplesPerSecond, 1) + "/s", 20);
   SerialX::print("Sensor Avg", 20);
   SerialX::println(valueAvg, 3, 20);
   SerialX::print("Sensor StdDev", 20);
   SerialX::println(valueStdDev, 3, 20);
   SerialX::print("Sensor StdDev%", 20);
   SerialX::println(isfinite(valueStdDevPercent) ? String(valueStdDevPercent, 2) + "%" : "n/a", 20);
   SerialX::print("Sensor Min", 20);
   SerialX::println(valueMin, 3, 20);
   SerialX::print("Sensor Max", 20);
   SerialX::println(valueMax, 3, 20);
   SerialX::print("Sensor Range", 20);
   SerialX::println(valueRange, 3, 20);
   Serial.println();

   Histogram histogram(sensorCapture.values(), sensorCapture.count(), MIN_HISTOGRAM_BINS, MAX_HISTOGRAM_BINS);
   SerialHistogram::print("Sensor Value Histogram", histogram, 3);
}

/// <summary>
/// Prints post-capture block-average analysis for configured sampling rates.
/// </summary>
void printSamplingRateAnalysis()
{
   analysisTable.printHeader();

   for (size_t samplingRateIndex = 0; samplingRateIndex < SAMPLING_RATE_COUNT; samplingRateIndex++)
   {
      size_t samplingRate = SAMPLING_RATES[samplingRateIndex];
      float effectiveRate = (samplingRate > 0) ? (static_cast<float>(MAX_SAMPLING_RATE_PER_SEC) / static_cast<float>(samplingRate)) : NAN;

      Stats avgSeriesStats = sensorCapture.computeAverageSeriesStats(samplingRate);
      float avgRange = Values::computeRange(avgSeriesStats.min(), avgSeriesStats.max());
      float avgStdDev = avgSeriesStats.stdDev();
      size_t averageCount = avgSeriesStats.count();

      String rateText = isfinite(effectiveRate) ? String(effectiveRate, 1) + "/s" : "n/a";
      if (averageCount == 0)
      {
         analysisTable.printRow(samplingRate, rateText, "n/a", "n/a");
      }
      else
      {
         analysisTable.printRow(samplingRate, rateText, SerialTable::fixed(avgRange, 3), SerialTable::fixed(avgStdDev, 3));
      }
   }

   Serial.println();
}

/// <summary>
/// Prints warm-up stability analysis and rolling min/max table data.
/// </summary>
void printWarmupAnalysis()
{
   postWarmupReady = false;
   postWarmupStartIndex = 0;

   size_t totalCount = sensorCapture.count();
   if (totalCount < 2)
   {
      Serial.println("Warm-up Stability Analysis");
      Serial.println("Not enough samples for warm-up analysis");
      Serial.println();
      return;
   }

   size_t segmentSize = min(STARTUP_SAMPLE_COUNT, totalCount / 2);
   if (segmentSize == 0)
   {
      Serial.println("Warm-up Stability Analysis");
      Serial.println("Not enough samples for warm-up analysis");
      Serial.println();
      return;
   }

   Stats startStats;
   Stats endStats;
   for (size_t i = 0; i < segmentSize; i++)
   {
      startStats.add(sensorCapture[i]);
      endStats.add(sensorCapture[totalCount - segmentSize + i]);
   }

   postWarmupStartIndex = segmentSize;
   postWarmupReady = (postWarmupStartIndex < totalCount);

   float startRange = startStats.max() - startStats.min();
   float endRange = endStats.max() - endStats.min();
   float startStdDev = startStats.stdDev();
   float endStdDev = endStats.stdDev();
   float startMean = startStats.get();
   float endMean = endStats.get();

   warmupTable.printHeader();
   warmupTable.printRow("Mean", SerialTable::fixed(startMean, 3), SerialTable::fixed(endMean, 3), SerialTable::fixed(endMean - startMean, 3));
   warmupTable.printRow("Range", SerialTable::fixed(startRange, 3), SerialTable::fixed(endRange, 3), SerialTable::fixed(endRange - startRange, 3));
   warmupTable.printRow("StdDev", SerialTable::fixed(startStdDev, 3), SerialTable::fixed(endStdDev, 3), SerialTable::fixed(endStdDev - startStdDev, 3));

   bool endMoreStable = isfinite(startStdDev) && isfinite(endStdDev) && (endStdDev < startStdDev);
   Serial.print("End more stable: ");
   Serial.println(endMoreStable ? "yes" : "no");
   Serial.println();
}

void startSerialDump()
{
   serialDumpStarted = true;
   serialDumpComplete = false;
   serialDumpIndex = 0;
   serialDumpCount = sensorCapture.count();
   lastSerialDumpMs = 0;

   if (serialDumpCount == 0)
   {
      Serial.println("No values captured");
      serialDumpComplete = true;
      return;
   }

   Serial.println("Dump start");
   SerialX::print("Index", 8);
   SerialX::println("Value", 12);
   SerialX::print("-----", 8);
   SerialX::println("-----------", 12);
}

void updateSerialDump()
{
   if (!serialDumpStarted || serialDumpComplete)
   {
      return;
   }

   unsigned long nowMs = millis();
   if ((PRINT_INTERVAL_MS > 0) && (lastSerialDumpMs != 0) && ((nowMs - lastSerialDumpMs) < PRINT_INTERVAL_MS))
   {
      return;
   }

   SerialX::print(serialDumpIndex, 8);
   SerialX::println(sensorCapture[serialDumpIndex], 3, 12);
   serialDumpIndex++;
   lastSerialDumpMs = nowMs;

   if (serialDumpIndex >= serialDumpCount)
   {
      Serial.println("Dump complete");
      serialDumpComplete = true;
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
   displayMode = DisplayMode::Summary;
   printCaptureSummary();
   printSamplingRateAnalysis();
   printWarmupAnalysis();
   updateDisplayProgress(true);
   startSerialDump();
}

void setup()
{
   SerialX::begin(115200, 2000);
   Wire.begin();
   arduino.begin();
   arduino.clearDisplay();
   sensor.begin();

   initializeDisplayTables();
   captureTable.load();

   arduino.setTextSize(3);
   arduino.setCursor(0, 0);
   arduino.println("Sensor Capture", Color::HEADING);
   arduino.setTextSize(2);
   captureTable.setPosition(0, arduino.getCursorY());
   captureTable.draw();
}

/// <summary>
/// Confirms the setup fields, sizes the sample buffer, and starts capturing.
/// </summary>
void startCapture()
{
   captureStarted = true;
   captureTable.save();

   sensorCapture.reset(static_cast<size_t>(maxSamples));

   captureStartMs = millis();
   updateDisplayProgress(true);
   Serial.println("Capture started...");
}

/// <summary>
/// Returns to the setup screen so another capture can be configured and run.
/// </summary>
void resetToSetup()
{
   captureStarted = false;
   captureFinalized = false;
   displayMode = DisplayMode::Summary;
   postWarmupStartIndex = 0;
   postWarmupReady = false;
   serialDumpStarted = false;
   serialDumpComplete = false;
   serialDumpIndex = 0;
   serialDumpCount = 0;

   arduino.clearDisplay();
   arduino.setTextSize(3);
   arduino.setCursor(0, 0);
   arduino.println("Sensor Capture", Color::HEADING);
   arduino.setTextSize(2);
   captureTable.setPosition(0, arduino.getCursorY());
   captureTable.forceRedraw();
   captureTable.draw();
}

void loop()
{
   if (!captureStarted)
   {
      int32_t selectDelta = arduino.encoderA.delta();
      int32_t adjustDelta = arduino.encoderB.delta();
      if (selectDelta != 0 || adjustDelta != 0)
      {
         captureTable.selectNext(selectDelta);
         captureTable.adjustSelected(adjustDelta);
         captureTable.draw();
      }

      if (arduino.buttonB.wasPressed())
      {
         captureTable.reset();
         captureTable.draw();
      }

      if (arduino.buttonA.wasPressed())
      {
         startCapture();
      }

      return;
   }

   if (samplingTimer.ready())
   {
      float sensorValue = sensor.get();
      bool stored = sensorCapture.addValue(sensorValue);

      if (stored)
      {
         updateDisplayProgress();
      }
   }

   if ((sensorCapture.count() >= static_cast<size_t>(maxSamples)) || ((millis() - captureStartMs) >= (static_cast<unsigned long>(maxCaptureTimeS) * 1000UL)))
   {
      finishCapture();
   }

   updateSerialDump();

   if (captureFinalized && arduino.buttonA.wasPressed())
   {
      resetToSetup();
      return;
   }

   int32_t modeDelta = arduino.encoderA.delta();
   if (captureFinalized && (modeDelta != 0))
   {
      int32_t modeCount = static_cast<int32_t>(DisplayMode::Count);
      int32_t newMode = (static_cast<int32_t>(displayMode) + modeDelta) % modeCount;
      if (newMode < 0)
      {
         newMode += modeCount;
      }

      displayMode = static_cast<DisplayMode>(newMode);
      updateDisplayProgress(true);
   }
}
