//
// Captures finite temperature samples for a fixed run period (or until the sample cap is reached),
// stores them in RAM, and reports serial summaries plus an automatic serial dump.
//
// Capture flow:
// 1) Initialize serial, display, and sensor, then start sampling immediately.
// 2) Sample at up to MAX_SAMPLING_RATE_PER_SEC and store finite values in RAM.
// 3) Stop when MAX_SAMPLES are stored or MAX_CAPTURE_TIME_S is reached.
//
// Output flow:
// - Display shows live progress during capture, including elapsed seconds.
// - After capture, button A cycles display modes: summary, histogram, post warm-up histogram, and scatter plot.
// - Serial summary includes run metrics, value stats, histogram bins, and warm-up analysis.
// - Stored points are dumped to serial automatically after capture completes.
//
// Sensor mode:
// - Reads from TestSensor (configurable via TestSensor.h using alias).
//
#include "ArduinoBoard.h"

#ifndef ARDUINO_BUTTON_SUPPORTED
#error "This sketch requires a board with button support (e.g. Feather ESP32-S3 or Feather M0)."
#endif
#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include "SensorCapture.h"
#include "SensorCaptureStats.h"
#include "SerialX.h"
#include "SerialTable.h"
#include "SerialHistogram.h"
#include "HistogramPlot.h"
#include "DisplayTable.h"
#include "ScatterPlot.h"
#include <math.h>
#include "TestSensor.h"

constexpr unsigned long MAX_SAMPLING_RATE_PER_SEC = 100;
// ----- capture configuration
constexpr size_t MAX_SAMPLES = 5000;
constexpr unsigned long MAX_CAPTURE_TIME_S = 120;

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
constexpr size_t ANALYSIS_TABLE_COLUMN_COUNT = sizeof(ANALYSIS_TABLE_COLUMNS) / sizeof(ANALYSIS_TABLE_COLUMNS[0]);
SerialTable analysisTable(ANALYSIS_TABLE_TITLE, ANALYSIS_TABLE_COLUMNS, ANALYSIS_TABLE_COLUMN_COUNT);

// ----- warm-up analysis table
constexpr const char* WARMUP_TABLE_TITLE = "Warm-up Stability Analysis";
const SerialTable::Column WARMUP_TABLE_COLUMNS[] = {
   { "Metric", 12 },
   { "Start", 12 },
   { "End", 12 },
   { "Delta", 12 },
};
constexpr size_t WARMUP_TABLE_COLUMN_COUNT = sizeof(WARMUP_TABLE_COLUMNS) / sizeof(WARMUP_TABLE_COLUMNS[0]);
SerialTable warmupTable(WARMUP_TABLE_TITLE, WARMUP_TABLE_COLUMNS, WARMUP_TABLE_COLUMN_COUNT);

enum class DisplayMode : uint8_t
{
   Summary = 0,
   Histogram,
   PostWarmupHistogram,
   ScatterPlot,
   Count
};

Arduino arduino;
TestSensor sensor;
Format progressPercentFormat("###%", Format::Alignment::LEFT);
Format samplesFormat("#####", Format::Alignment::LEFT);
Format collectingSamplesFormat("#####/5000", Format::Alignment::LEFT);
Format timeFormat("###s", Format::Alignment::LEFT);
Format collectingTimeFormat("###/120s", Format::Alignment::LEFT);
Format statsFormat("######.##", Format::Alignment::LEFT);
Format stdDevPercentFormat("##.##%", Format::Alignment::LEFT);
Format rateFormat("#####/s", Format::Alignment::LEFT);

SensorCapture sensorCapture(
   MAX_SAMPLES,
   MAX_CAPTURE_TIME_S * 1000UL,
   ((1000UL / MAX_SAMPLING_RATE_PER_SEC) == 0 ? 1UL : (1000UL / MAX_SAMPLING_RATE_PER_SEC)));

bool captureFinalized = false;
unsigned long captureStartMs = 0;
unsigned long lastDisplayRefreshMs = 0;
DisplayMode displayMode = DisplayMode::Summary;
size_t postWarmupStartIndex = 0;
bool postWarmupReady = false;

bool serialDumpStarted = false;
bool serialDumpComplete = false;
size_t serialDumpIndex = 0;
size_t serialDumpCount = 0;
unsigned long lastSerialDumpMs = 0;

// Display tables
DisplayTable summaryTable(&arduino, 0, 0);
DisplayTable collectingTable(&arduino, 0, 0);

/// <summary>
/// Initializes display tables used by summary and collecting screens.
/// </summary>
void initializeDisplayTables()
{
   arduino.setTextSize(3);
   int16_t summaryTableY = arduino.charH();

   arduino.setTextSize(2);
   int16_t collectingTableY = summaryTableY;

   summaryTable = DisplayTable(&arduino, 0, summaryTableY);
   summaryTable.addRow("Samples", samplesFormat, Color::LABEL, Color::VALUE);
   summaryTable.addRow("Time", timeFormat, Color::LABEL, Color::VALUE);
   summaryTable.addRow("Rate", rateFormat, Color::LABEL, Color::VALUE);
   summaryTable.addRow("Avg", statsFormat, Color::LABEL, Color::VALUE2);
   summaryTable.addRow("StdDev", statsFormat, Color::LABEL, Color::VALUE3);
   summaryTable.addRow("StdDev%", stdDevPercentFormat, Color::LABEL, Color::VALUE3);

   collectingTable = DisplayTable(&arduino, 0, collectingTableY);
   collectingTable.addRow("Samples", collectingSamplesFormat, Color::LABEL, Color::VALUE);
   collectingTable.addRow("Time", collectingTimeFormat, Color::LABEL, Color::VALUE);
   collectingTable.addRow("Progress", progressPercentFormat, Color::LABEL, Color::VALUE);
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
   size_t sampleCount = sensorCapture.count();
   float samplesPerSecond = (captureTimeMs > 0) ? (sampleCount * 1000.0f / captureTimeMs) : 0.0f;

   SensorCaptureStats analysis(sensorCapture);
   Stats basicStats = analysis.computeBasicStats();
   float stdDevPercent = NAN;
   float avg = basicStats.get();
   float stdDev = basicStats.stdDev();
   if (isfinite(avg) && (fabsf(avg) > 0.0f) && isfinite(stdDev))
   {
      stdDevPercent = (stdDev / fabsf(avg)) * 100.0f;
   }

   arduino.setTextSize(2);
   summaryTable.updateValue(0, static_cast<unsigned long>(sampleCount));
   summaryTable.updateValue(1, captureTimeSec);
   summaryTable.updateValue(2, samplesPerSecond);
   summaryTable.updateValue(3, avg);
   summaryTable.updateValue(4, stdDev);
   summaryTable.updateValue(5, isfinite(stdDevPercent) ? String(stdDevPercent, 2) + "%" : "n/a");

   summaryTable.draw();
}

/// <summary>
/// Draws a histogram for a selected capture value range.
/// </summary>
/// <param name="startIndex">First sample index to include.</param>
/// <param name="top">Top Y coordinate of the histogram area.</param>
/// <param name="height">Histogram drawing height in pixels.</param>
void drawDisplayHistogramForRange(size_t startIndex, int16_t top, int16_t height)
{
   const float* values = sensorCapture.values() + startIndex;
   size_t valueCount = sensorCapture.count() - startIndex;
   Histogram histogram(values, valueCount, MIN_HISTOGRAM_BINS, MAX_HISTOGRAM_BINS);

   HistogramPlot plot(&arduino, histogram, 0, arduino.width(), top, height, Color::VALUE2, 3);
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
   arduino.println("Post Warm-up", Color::HEADING);

   if (!postWarmupReady || (postWarmupStartIndex >= sensorCapture.count()))
   {
      arduino.setTextSize(2);
      arduino.println("Not enough data", Color::LABEL);
      return;
   }

   int16_t top = arduino.getCursorY();
   int16_t h = arduino.height() - top - 2;
   drawDisplayHistogramForRange(postWarmupStartIndex, top, h);
}

/// <summary>
/// Draws a compact histogram display mode for all captured samples.
/// </summary>
void renderDisplayHistogram()
{
   arduino.clearDisplay();
   arduino.setTextSize(3);
   arduino.setCursor(0, 0);
   arduino.println("Histogram", Color::HEADING);

   int16_t top = arduino.getCursorY();
   int16_t h = arduino.height() - top - 2;
   drawDisplayHistogramForRange(0, top, h);
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
   int16_t plotTop = arduino.getCursorY();
   int16_t footerHeight = arduino.charH() + 2;
   int16_t plotHeight = arduino.height() - plotTop - footerHeight - 2;
   int16_t plotWidth = arduino.width();

   if (plotHeight < 20 || plotWidth < 20)
   {
      arduino.setTextSize(2);
      arduino.println("Plot area too small", Color::LABEL);
      return;
   }

   ScatterPlot plot(&arduino, 0, plotTop, plotWidth, plotHeight);
   plot.render(sensorCapture.values(), totalCount);
}

   /// <summary>
   /// Updates the progress and capture display.
   /// </summary>
   void updateDisplayProgress(bool forceRefresh = false)
   {
      unsigned long nowMs = millis();
      unsigned long displayUpdateIntervalMs = (DISPLAY_UPDATE_RATE_PER_SEC == 0) ? 0 : (1000UL / DISPLAY_UPDATE_RATE_PER_SEC);
      if (!forceRefresh && ((nowMs - lastDisplayRefreshMs) < displayUpdateIntervalMs))
      {
         return;
      }

   lastDisplayRefreshMs = nowMs;

   if (sensorCapture.isCaptureComplete())
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

   size_t count = sensorCapture.count();
   unsigned long elapsedSeconds = (nowMs - captureStartMs) / 1000UL;
   if (elapsedSeconds > MAX_CAPTURE_TIME_S)
   {
      elapsedSeconds = MAX_CAPTURE_TIME_S;
   }

   float samplePercent = (MAX_SAMPLES > 0) ? ((static_cast<unsigned long>(count) * 100.0f) / MAX_SAMPLES) : 0.0f;
   float timePercent = (MAX_CAPTURE_TIME_S > 0) ? ((elapsedSeconds * 100.0f) / MAX_CAPTURE_TIME_S) : 0.0f;

   bool samplesAreLimiting = (samplePercent >= timePercent);

   // Set sample values
   Color sampleColor = samplesAreLimiting ? Color::VALUE : Color::GRAY;
   collectingTable.updateValue(0, count, sampleColor);

   // Set time values
   Color timeColor = !samplesAreLimiting ? Color::VALUE : Color::GRAY;
   collectingTable.updateValue(1, elapsedSeconds, timeColor);

   float progressPercent = max(samplePercent, timePercent);
   if (progressPercent > 100.0f)
   {
      progressPercent = 100.0f;
   }

   collectingTable.updateValue(2, progressPercent, Color::VALUE);
   collectingTable.draw();
}

/// <summary>
/// Computes and prints capture statistics to Serial.
/// </summary>
void printCaptureSummary()
{
   SensorCaptureStats analysis(sensorCapture);
   Stats basicStats = analysis.computeBasicStats();

   float valueAvg = basicStats.get();
   float valueStdDev = basicStats.stdDev();
   float valueMin = basicStats.min();
   float valueMax = basicStats.max();
   float valueRange = analysis.computeRange(valueMin, valueMax);
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
   SensorCaptureStats analysis(sensorCapture);
   analysisTable.printHeader();

   for (size_t samplingRateIndex = 0; samplingRateIndex < SAMPLING_RATE_COUNT; samplingRateIndex++)
   {
      size_t samplingRate = SAMPLING_RATES[samplingRateIndex];
      float effectiveRate = (samplingRate > 0) ? (static_cast<float>(MAX_SAMPLING_RATE_PER_SEC) / static_cast<float>(samplingRate)) : NAN;

      Stats avgSeriesStats = analysis.computeAverageSeriesStats(samplingRate);
      float avgRange = analysis.computeRange(avgSeriesStats.min(), avgSeriesStats.max());
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

   captureStartMs = millis();
   updateDisplayProgress(true);
   Serial.println("Capture started...");
}

void loop()
{
   SensorCaptureState states = sensorCapture.update();
   SensorCaptureState valueStates = SENSOR_CAPTURE_STATE_NONE;

   if (sensorCapture.readyForValue())
   {
      float sensorValue = sensor.get();
      valueStates = sensorCapture.addValue(sensorValue);

      if ((valueStates & SENSOR_CAPTURE_STATE_VALUE_STORED) != 0)
      {
         updateDisplayProgress();
      }
   }

   if (((states | valueStates) & SENSOR_CAPTURE_STATE_COMPLETED) != 0)
   {
      finishCapture();
   }

   updateSerialDump();

   if (captureFinalized && arduino.buttonA.wasPressed())
   {
      displayMode = static_cast<DisplayMode>((static_cast<uint8_t>(displayMode) + 1U) % static_cast<uint8_t>(DisplayMode::Count));
      updateDisplayProgress(true);
   }
}
