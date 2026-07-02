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
// - Reads from TempSensor.
//
#include "Feather.h"
#include "SensorCapture.h"
#include "SensorCaptureStats.h"
#include "SensorCaptureOutput.h"
#include "SerialX.h"
#include "SerialTable.h"
#include <Wire.h>
#include <math.h>
#include "TempSensor.h"

constexpr unsigned long MAX_SAMPLING_RATE_PER_SEC = 100;
constexpr size_t MAX_SAMPLES = 5000;
constexpr unsigned long MAX_CAPTURE_TIME_S = 120;
constexpr size_t MIN_HISTOGRAM_BINS = 5;
constexpr size_t MAX_HISTOGRAM_BINS = 50;
constexpr unsigned long SAMPLE_INTERVAL_MS =
   ((1000UL / MAX_SAMPLING_RATE_PER_SEC) == 0 ? 1UL : (1000UL / MAX_SAMPLING_RATE_PER_SEC));
constexpr unsigned long PRINT_INTERVAL_MS = 2;
constexpr unsigned long DISPLAY_UPDATE_INTERVAL_MS = 200;
// Number of samples used for each side of the warm-up comparison (start segment vs end segment).
constexpr size_t STARTUP_SAMPLE_COUNT = 100;

constexpr size_t SAMPLING_RATES[] = { 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 };
constexpr size_t SAMPLING_RATE_COUNT = sizeof(SAMPLING_RATES) / sizeof(SAMPLING_RATES[0]);

const char ANALYSIS_TABLE_TITLE[] = "Sample Analysis";
const SerialTable::Column ANALYSIS_TABLE_COLUMNS[] = {
   { "N", 8 },
   { "Rate", 10 },
   { "Range", 12 },
   { "StdDev", 12 },
};
const size_t ANALYSIS_TABLE_COLUMN_COUNT = sizeof(ANALYSIS_TABLE_COLUMNS) / sizeof(ANALYSIS_TABLE_COLUMNS[0]);
SerialTable analysisTable(ANALYSIS_TABLE_TITLE, ANALYSIS_TABLE_COLUMNS, ANALYSIS_TABLE_COLUMN_COUNT);

const char WARMUP_TABLE_TITLE[] = "Warm-up Stability Analysis";
const SerialTable::Column WARMUP_TABLE_COLUMNS[] = {
   { "Metric", 12 },
   { "Start", 12 },
   { "End", 12 },
   { "Delta", 12 },
};
const size_t WARMUP_TABLE_COLUMN_COUNT = sizeof(WARMUP_TABLE_COLUMNS) / sizeof(WARMUP_TABLE_COLUMNS[0]);
SerialTable warmupTable(WARMUP_TABLE_TITLE, WARMUP_TABLE_COLUMNS, WARMUP_TABLE_COLUMN_COUNT);

enum class DisplayMode : uint8_t
{
   Summary = 0,
   Histogram,
   PostWarmupHistogram,
   ScatterPlot,
   Count
};

DisplayMode& operator++(DisplayMode& mode)
{
   mode = static_cast<DisplayMode>((static_cast<uint8_t>(mode) + 1U) % static_cast<uint8_t>(DisplayMode::Count));
   return mode;
}

Feather feather;
TempSensor sensor;
Format progressPercentFormat("###%", 6, Format::Alignment::RIGHT);

SensorCapture sensorCapture(
   MAX_SAMPLES,
   MAX_CAPTURE_TIME_S * 1000UL,
   SAMPLE_INTERVAL_MS);

bool captureFinalized = false;
unsigned long captureStartMs = 0;
unsigned long lastDisplayRefreshMs = 0;
DisplayMode displayMode = DisplayMode::Summary;
Stats warmupStartStats;
Stats warmupEndStats;
bool warmupHasComparison = false;
size_t postWarmupStartIndex = 0;
bool postWarmupReady = false;

/// <summary>
/// Draws the summary display mode after capture completion.
/// </summary>
void renderDisplaySummary()
{
   feather.clearDisplay();
   feather.setTextSize(3);
   feather.setCursor(0, 0);
   feather.println("Capture Summary", Color::HEADING);

   feather.setTextSize(2);
   feather.print("Samples: ", Color::LABEL);
   feather.println(static_cast<unsigned long>(sensorCapture.count()), Color::VALUE);

   SensorCaptureStats analysis(sensorCapture);
   Stats basicStats = analysis.computeBasicStats();
   feather.print("Avg: ", Color::LABEL);
   feather.println(basicStats.get(), 2, Color::VALUE2);
   feather.print("Std: ", Color::LABEL);
   feather.println(basicStats.stdDev(), 3, Color::VALUE3);
}

void drawDisplayHistogramForRange(size_t startIndex, int16_t top, int16_t height)
{
   const float* values = sensorCapture.values() + startIndex;
   size_t valueCount = sensorCapture.count() - startIndex;
   Histogram histogram(values, valueCount, MIN_HISTOGRAM_BINS, MAX_HISTOGRAM_BINS);

   SensorCaptureOutput::drawHistogramOnFeather(feather, "", histogram, 0, feather.width(), top, height, Color::VALUE2, 3);
}

/// <summary>
/// Draws a histogram for only the post-warmup data range.
/// </summary>
void renderDisplayPostWarmupHistogram()
{
   feather.clearDisplay();
   feather.setTextSize(3);
   feather.setCursor(0, 0);
   feather.println("Post Warm-up", Color::HEADING);

   if (!postWarmupReady || (postWarmupStartIndex >= sensorCapture.count()))
   {
      feather.setTextSize(2);
      feather.println("Not enough data", Color::LABEL);
      return;
   }

   int16_t top = feather.getCursorY();
   int16_t h = feather.height() - top - 2;
   drawDisplayHistogramForRange(postWarmupStartIndex, top, h);
}

/// <summary>
/// Draws a compact histogram display mode for all captured samples.
/// </summary>
void renderDisplayHistogram()
{
   feather.clearDisplay();
   feather.setTextSize(3);
   feather.setCursor(0, 0);
   feather.println("Histogram", Color::HEADING);

   int16_t top = feather.getCursorY();
   int16_t h = feather.height() - top - 2;
   drawDisplayHistogramForRange(0, top, h);
}

/// <summary>
/// Draws a compact x-y scatter plot for all captured samples.
/// </summary>
void renderDisplayScatterPlot()
   {
      feather.clearDisplay();
      feather.setTextSize(3);
      feather.setCursor(0, 0);
      feather.println("Scatter Plot", Color::HEADING);

      size_t totalCount = sensorCapture.count();
      if (totalCount < 2)
      {
         feather.setTextSize(2);
         feather.println("Not enough data", Color::LABEL);
         return;
      }

      float plotYMin = sensorCapture[0];
      float plotYMax = sensorCapture[0];
      for (size_t i = 1; i < totalCount; i++)
      {
         float value = sensorCapture[i];
         plotYMin = min(plotYMin, value);
         plotYMax = max(plotYMax, value);
      }

      if (!isfinite(plotYMin) || !isfinite(plotYMax))
      {
         feather.setTextSize(2);
         feather.println("Not enough data", Color::LABEL);
         return;
      }

      int16_t chartLeft = 0;
      int16_t chartTop = feather.getCursorY();
      feather.setTextSize(2);
      int16_t footerHeight = feather.charH() + 2;
      int16_t chartHeight = feather.height() - chartTop - footerHeight - 2;
      int16_t chartWidth = feather.width();

      if (chartHeight < 20 || chartWidth < 20)
      {
         feather.setTextSize(2);
         feather.println("Plot area too small", Color::LABEL);
         return;
      }

      float ySpan = plotYMax - plotYMin;
      if (ySpan <= 0.0f)
      {
         ySpan = 1.0f;
      }

      feather.fillRect(chartLeft, chartTop, chartWidth, chartHeight, Color::BLACK);
      feather.fillRect(chartLeft, chartTop + chartHeight - 1, chartWidth, 1, Color::DARKGRAY);
      feather.fillRect(chartLeft, chartTop, 1, chartHeight, Color::DARKGRAY);

      size_t pixelCount = static_cast<size_t>(chartWidth) * static_cast<size_t>(chartHeight);
      uint8_t* density = new uint8_t[pixelCount];
      if (density == nullptr)
      {
         feather.setTextSize(2);
         feather.println("Memory unavailable", Color::LABEL);
         return;
      }

      for (size_t i = 0; i < pixelCount; i++)
      {
         density[i] = 0;
      }

      uint8_t maxDensity = 0;
      for (size_t sampleIndex = 0; sampleIndex < totalCount; sampleIndex++)
      {
         int16_t x = chartLeft + static_cast<int16_t>((sampleIndex * static_cast<size_t>(chartWidth - 1)) / max(static_cast<size_t>(1), totalCount - 1));
         float value = sensorCapture[sampleIndex];
         int16_t y = chartTop + static_cast<int16_t>((plotYMax - value) * static_cast<float>(chartHeight - 1) / ySpan);

         if (y < chartTop) y = chartTop;
         if (y >= (chartTop + chartHeight)) y = chartTop + chartHeight - 1;

         size_t densityIndex = static_cast<size_t>(y - chartTop) * static_cast<size_t>(chartWidth) + static_cast<size_t>(x - chartLeft);
         if (density[densityIndex] < 255)
         {
            density[densityIndex]++;
         }

         if (density[densityIndex] > maxDensity)
         {
            maxDensity = density[densityIndex];
         }
      }

      for (int16_t y = 0; y < chartHeight; y++)
      {
         for (int16_t x = 0; x < chartWidth; x++)
         {
            size_t densityIndex = static_cast<size_t>(y) * static_cast<size_t>(chartWidth) + static_cast<size_t>(x);
            uint8_t value = density[densityIndex];
            if (value == 0)
            {
               continue;
            }

            float ratio = 0.0f;
            if (maxDensity > 0)
            {
               ratio = static_cast<float>(value) / static_cast<float>(maxDensity);
            }

            Color color = Color565::blend(Color565::fromRGB(0, 128, 0), Color::GREEN, ratio);
            feather.fillRect(chartLeft + x, chartTop + y, 1, 1, color);
         }
      }

      delete[] density;

      feather.setTextSize(2);
      feather.setCursor(chartLeft, chartTop);
      feather.print(plotYMax, 2, Color::LABEL);

      feather.setCursor(chartLeft, chartTop + chartHeight - feather.charH());
      feather.print(plotYMin, 2, Color::LABEL);

      feather.setCursor(chartLeft, chartTop + chartHeight + 1);
      feather.print("0", Color::LABEL);

      String xMaxLabel = String(static_cast<unsigned long>(totalCount - 1));
      int16_t xMaxX = chartLeft + chartWidth - static_cast<int16_t>(xMaxLabel.length() * feather.charW());
      if (xMaxX < chartLeft)
      {
         xMaxX = chartLeft;
      }
      feather.setCursor(xMaxX, chartTop + chartHeight + 1);
      feather.print(xMaxLabel, Color::LABEL);
   }

   /// <summary>
   /// Updates on-screen capture progress and post-capture display modes.
   /// Refresh is throttled to DISPLAY_UPDATE_INTERVAL_MS unless forceRefresh is true.
   /// </summary>
   void updateDisplayProgress(bool forceRefresh = false)
{
   unsigned long nowMs = millis();
   if (!forceRefresh && ((nowMs - lastDisplayRefreshMs) < DISPLAY_UPDATE_INTERVAL_MS))
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

   feather.setTextSize(3);
   feather.setCursor(0, 0);
   feather.print("Sensor Capture      ", Color::HEADING);

   feather.setTextSize(2);
   int16_t y = feather.charH() * 2;

   feather.setCursor(0, y);
   feather.print("Collecting...      ", Color::VALUE);

   size_t count = sensorCapture.count();
   y += feather.charH();
   feather.setCursor(0, y);
   feather.print("Samples: ", Color::LABEL);
   feather.print(static_cast<unsigned long>(count));
   feather.print("/");
   feather.print(static_cast<unsigned long>(MAX_SAMPLES));

   unsigned long elapsedSeconds = (nowMs - captureStartMs) / 1000UL;
   if (elapsedSeconds > MAX_CAPTURE_TIME_S)
   {
      elapsedSeconds = MAX_CAPTURE_TIME_S;
   }

   feather.print(" (");
   feather.print(elapsedSeconds);
   feather.print("/");
   feather.print(MAX_CAPTURE_TIME_S);
   feather.print("s)   ");

   unsigned long samplePercent = (MAX_SAMPLES > 0) ? ((static_cast<unsigned long>(count) * 100UL) / static_cast<unsigned long>(MAX_SAMPLES)) : 0UL;
   unsigned long timePercent = (MAX_CAPTURE_TIME_S > 0) ? ((elapsedSeconds * 100UL) / MAX_CAPTURE_TIME_S) : 0UL;
   unsigned long progressPercent = max(samplePercent, timePercent);
   if (progressPercent > 100UL)
   {
      progressPercent = 100UL;
   }

   y += feather.charH();
   feather.setCursor(0, y);
   feather.print("Progress: ", Color::LABEL);
   feather.print(static_cast<float>(progressPercent), progressPercentFormat, Color::VALUE);
}

/// <summary>
/// Computes and prints capture statistics and histogram data to Serial.
/// </summary>
void printCaptureSummary()
{
   SensorCaptureOutput::printCaptureSummary(sensorCapture, MAX_CAPTURE_TIME_S * 1000UL, 3);

   SensorCaptureOutput::printHistogramBins("Sensor Value Histogram", sensorCapture, MIN_HISTOGRAM_BINS, MAX_HISTOGRAM_BINS, 3);
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
   warmupHasComparison = false;
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

   warmupStartStats = startStats;
   warmupEndStats = endStats;
   warmupHasComparison = true;
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
   sensorCapture.dumpToSerial(PRINT_INTERVAL_MS);
}

void setup()
{
   SerialX::begin(115200, 2000);
   Wire.begin();
   feather.begin();
   feather.clearDisplay();
   sensor.begin();

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
      float sensorValue = sensor.readTemperatureF();
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

   if (captureFinalized && feather.buttonA.wasPressed())
   {
      ++displayMode;
      updateDisplayProgress(true);
   }
}
