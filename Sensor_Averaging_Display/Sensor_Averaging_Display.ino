//
// Captures finite sensor values for a fixed run window (or until the sample cap is reached), stores
// them in RAM, and reports serial/display summaries.
//
// The sketch is designed for repeatable calibration runs where sampling duration, interval, and
// maximum sample count are controlled by constants near the top of the file.
//
// Capture flow:
// 1) Initialize display, serial, and sensor.
// 2) Sample at up to MAX_SAMPLE_RATE_PER_SEC and store finite values in RAM.
// 3) Stop when MAX_SAMPLES are stored or SAMPLING_DURATION_S elapses.
//
// Output flow:
// - While collecting, the Feather display shows live capture progress.
// - After capture, the display renders a value histogram with min/max labels plus Sigma%/effective-rate
//   rows for multiple averaging window sizes.
// - Serial summary includes run metrics, value stats, and a value histogram.
//
// Sensor mode:
// - Reads from TestSensor (configurable via TestSensor.h using alias).
//
// Typical usage: flash the sketch and allow warm-up/capture to complete, then review serial and
// display outputs to compare stability (StdDev%, range) across averaging sizes.
//

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include "Histogram.h"
#include "SensorCapture.h"
#include "Timer.h"
#include "SerialX.h"
#include "SerialHistogram.h"
#include "HistogramPlot.h"
#include "DisplayTable.h"
#include <Wire.h>
#include "TestSensor.h"

// ----------- The Board
Arduino arduino;
TestSensor sensor;

// ----------- Capture Settings
constexpr unsigned long SAMPLING_DURATION_S = 10;
constexpr unsigned long MAX_SAMPLE_RATE_PER_SEC = 1000;
constexpr unsigned long SAMPLE_INTERVAL_MS =
   (MAX_SAMPLE_RATE_PER_SEC == 0) ? 1UL :
   ((1000UL / MAX_SAMPLE_RATE_PER_SEC) == 0 ? 1UL : (1000UL / MAX_SAMPLE_RATE_PER_SEC));
constexpr size_t MAX_SAMPLES = 1000;
SensorCapture sensorCapture(
   MAX_SAMPLES,
   SAMPLING_DURATION_S * 1000UL,
   SAMPLE_INTERVAL_MS);
bool captureFinalized = false;
unsigned long captureStartMs = 0;

// ----------- Display Items
constexpr uint16_t DISPLAY_UPDATE_RATE_PER_SEC = 5;
RateTimer displayRefreshTimer(DISPLAY_UPDATE_RATE_PER_SEC);
bool collectingViewInitialized = false;
Format progressPercentFormat("###%", Format::Alignment::LEFT);
Format collectingSamplesFormat("####/1000", Format::Alignment::LEFT);
Format collectingTimeFormat("##/10s", Format::Alignment::LEFT);
DisplayTable collectingTable(&arduino, 0, 0);

// ----------- Analysis Settings
constexpr size_t HISTOGRAM_BINS = 20;
constexpr uint8_t CHART_MIN_MAX_SIGNIFICANT_DIGITS = 3;
constexpr size_t BUFFER_SIZES[] = { 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 };
constexpr size_t NUM_BUFFER_SIZES = sizeof(BUFFER_SIZES) / sizeof(BUFFER_SIZES[0]);

/// <summary>
/// Draws a histogram panel on the Feather display.
/// </summary>
/// <param name="title">Title text drawn above the panel, or empty for none.</param>
/// <param name="histogram">Histogram data to render.</param>
/// <param name="sectionLeft">Left X coordinate of the panel.</param>
/// <param name="sectionWidth">Width of the panel in pixels.</param>
/// <param name="sectionTop">Top Y coordinate of the panel.</param>
/// <param name="sectionHeight">Height of the panel in pixels.</param>
/// <param name="barColor">Color used to draw histogram bars.</param>
void drawHistogramOnFeather(const char* title, const Histogram& histogram, int16_t sectionLeft, int16_t sectionWidth, int16_t sectionTop, int16_t sectionHeight, Color barColor)
{
   arduino.setTextSize(2);
   arduino.setCursor(sectionLeft, sectionTop);
   arduino.println(title, Color::LABEL);

   int16_t chartTop = arduino.getCursorY() + 1;
   int16_t adjustedHeight = sectionHeight - (chartTop - sectionTop);
   HistogramPlot plot(&arduino, histogram, sectionLeft, sectionWidth, chartTop, adjustedHeight, barColor, CHART_MIN_MAX_SIGNIFICANT_DIGITS);
   plot.render();
}

/// <summary>
/// Renders histogram and N/range analysis table on the Feather display.
/// </summary>
void renderHistogramsOnFeather()
{
   arduino.clearDisplay();

   Histogram valueHistogram(sensorCapture.values(), sensorCapture.count(), HISTOGRAM_BINS);

   arduino.setTextSize(2);
   arduino.setCursor(0, 0);
   String headerText = String(sensorCapture.count()) + " Samples";
   arduino.println(headerText.c_str(), Color::HEADING);

   int16_t top = arduino.getCursorY();
   int16_t messageHeight = arduino.charH() + 2;
   int16_t availableHeight = (int16_t)arduino.height() - top - messageHeight;
   int16_t totalWidth = (int16_t)arduino.width();
   constexpr int16_t sectionGap = 5;
   constexpr int16_t tableWidth = 140;
   int16_t leftWidth = totalWidth - sectionGap - tableWidth;
   int16_t x = leftWidth + sectionGap;

   drawHistogramOnFeather("", valueHistogram, 0, leftWidth, top, availableHeight, Color::VALUE2);

   arduino.setTextSize(2);
   Format numSamplesFormat("####", Format::Alignment::RIGHT);
   Format sigmaPercentFormat("###.##%", Format::Alignment::RIGHT);
   Format hzFormat(" ####", Format::Alignment::RIGHT);

   auto computeEffectiveRateHz = [](size_t sampleSize) -> float
   {
      return (sampleSize > 0) ? (static_cast<float>(MAX_SAMPLE_RATE_PER_SEC) / static_cast<float>(sampleSize)) : NAN;
   };

   Stats rawStats = sensorCapture.computeBasicStats();
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

      Stats avgSeriesStats = sensorCapture.computeAverageSeriesStats(sampleSize);
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
   Stats basicStats = sensorCapture.computeBasicStats();

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
   SerialX::println(SAMPLING_DURATION_S * 1000UL, 20);
   SerialX::print("Samples", 20);
   SerialX::println(sensorCapture.count(), 20);
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

   Histogram histogram(sensorCapture.values(), sensorCapture.count(), HISTOGRAM_BINS);
   SerialHistogram::print("Sensor Value Histogram", histogram, 3);

   sensorCapture.printFirstAndLastToSerial(10, 3);
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
      float effectiveRate = (sampleSize > 0) ? (static_cast<float>(MAX_SAMPLE_RATE_PER_SEC) / static_cast<float>(sampleSize)) : NAN;

      Stats avgSeriesStats = sensorCapture.computeAverageSeriesStats(sampleSize);
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
   collectingTable.addRow("Samples", collectingSamplesFormat, Color::LABEL, Color::VALUE);
   collectingTable.addRow("Time", collectingTimeFormat, Color::LABEL, Color::VALUE);
   collectingTable.addRow("Progress", progressPercentFormat, Color::LABEL, Color::VALUE);
}

/// <summary>
/// Updates collecting status text shown on the Feather display.
/// Refresh is throttled to DISPLAY_UPDATE_RATE_PER_SEC unless forceRefresh is true.
/// </summary>
/// <param name="forceRefresh">When true, bypasses the refresh-rate throttle and redraws immediately.</param>
void updateDisplay(bool forceRefresh = false)
{
   if (sensorCapture.isCaptureComplete())
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

   if (forceRefresh || !collectingViewInitialized)
   {
      arduino.clearDisplay();
      arduino.setCursor(0, 0);
      arduino.setTextSize(3);
      arduino.println("Sensor Averaging", Color::HEADING);

      arduino.setTextSize(2);
      arduino.println("Collecting data", Color::VALUE);
      collectingViewInitialized = true;
   }

   size_t count = sensorCapture.count();
   unsigned long elapsedSeconds = (nowMs - captureStartMs) / 1000UL;
   if (elapsedSeconds > SAMPLING_DURATION_S)
   {
      elapsedSeconds = SAMPLING_DURATION_S;
   }

   float samplePercent = (MAX_SAMPLES > 0) ? ((static_cast<unsigned long>(count) * 100.0f) / MAX_SAMPLES) : 0.0f;
   float timePercent = (SAMPLING_DURATION_S > 0) ? ((elapsedSeconds * 100.0f) / SAMPLING_DURATION_S) : 0.0f;
   bool samplesAreLimiting = (samplePercent >= timePercent);

   Color sampleColor = samplesAreLimiting ? Color::VALUE : Color::GRAY;
   Color timeColor = !samplesAreLimiting ? Color::VALUE : Color::GRAY;

   float progressPercent = max(samplePercent, timePercent);
   if (progressPercent > 100.0f)
   {
      progressPercent = 100.0f;
   }

   collectingTable.updateValue(0, count, sampleColor);
   collectingTable.updateValue(1, elapsedSeconds, timeColor);
   collectingTable.updateValue(2, progressPercent, Color::VALUE);
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
   renderHistogramsOnFeather();
}

void setup()
{
   SerialX::begin(115200, 2000);
   Wire.begin();
   arduino.begin();

   sensor.begin();
   sensorCapture.reset();
   initializeCollectingTable();

   captureStartMs = millis();
   updateDisplay(true);
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
