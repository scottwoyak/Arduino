// -------------------------------------------------------------------------------------------------
//
// Captures finite sensor values for a fixed run window (or until the sample cap is reached), stores
// them in RAM, and reports serial summaries plus an automatic serial dump.
//
// Capture flow:
// 1) Initialize serial and sensor, then start sampling immediately.
// 2) Sample at up to MAX_SAMPLING_RATE and store finite values in RAM.
// 3) Stop when MAX_SAMPLES are stored or MAX_SAMPLE_DURATION_S expires.
//
// Output flow:
// - Serial summary includes run metrics, value stats, and a value histogram.
// - Post-capture window analysis prints effective rate/range/stddev by averaging window size.
// - Stored points are dumped to serial automatically after capture completes.
//
// Sensor mode:
// - Reads from TempSensor.
// -------------------------------------------------------------------------------------------------
#include "SensorCapture.h"
#include "SensorCaptureStats.h"
#include "SensorCaptureOutput.h"
#include "SerialX.h"
#include <Wire.h>
#include "TempSensor.h"

// Total sampling window duration before capture auto-completes.
constexpr unsigned long MAX_SAMPLE_DURATION_S = 30;
// Maximum sampling rate (samples per second).
constexpr unsigned long MAX_SAMPLING_RATE = 10;
// Maximum number of samples stored in RAM during a run.
constexpr size_t MAX_SAMPLES = 1000;
// Number of bins used for the value histogram.
constexpr size_t HISTOGRAM_BINS = 20;
// Delay between serial dump rows to avoid overwhelming the serial link.
constexpr unsigned long PRINT_INTERVAL_MS = 2;

constexpr size_t ANALYSIS_WINDOW_SIZES[] = { 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 };
constexpr size_t ANALYSIS_WINDOW_COUNT = sizeof(ANALYSIS_WINDOW_SIZES) / sizeof(ANALYSIS_WINDOW_SIZES[0]);

TempSensor sensor;

SensorCapture sensorCapture(
   MAX_SAMPLES,
   0,
   MAX_SAMPLE_DURATION_S * 1000UL,
   (MAX_SAMPLING_RATE == 0) ? 1UL : ((1000UL / MAX_SAMPLING_RATE) == 0 ? 1UL : (1000UL / MAX_SAMPLING_RATE)),
   0.0f,
   0,
   false);

bool captureFinalized = false;

/// <summary>
/// Computes and prints capture statistics and histogram data to Serial.
/// </summary>
void printCaptureSummary()
{
   SensorCaptureOutput::printCaptureSummary(sensorCapture, MAX_SAMPLE_DURATION_S * 1000UL, 3);

   SensorCaptureOutput::printHistogramBins("Sensor Value Histogram", sensorCapture, HISTOGRAM_BINS, 3);
}

/// <summary>
/// Prints post-capture block-average analysis for configured sample sizes.
/// </summary>
void printWindowAnalysis()
{
   SensorCaptureStats analysis(sensorCapture);

   Serial.println("Sample Analysis");
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
      float effectiveRate = (windowSize > 0) ? (static_cast<float>(MAX_SAMPLING_RATE) / static_cast<float>(windowSize)) : NAN;

      Stats avgSeriesStats = analysis.computeAverageSeriesStats(windowSize);
      float avgRange = analysis.computeRange(avgSeriesStats.min(), avgSeriesStats.max());
      float avgStdDev = avgSeriesStats.stdDev();
      size_t averageCount = avgSeriesStats.count();

      SerialX::print(windowSize, 8);
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
   sensorCapture.dumpToSerial(PRINT_INTERVAL_MS);
}

void setup()
{
   SerialX::begin(115200, 2000);
   Wire.begin();
   sensor.begin();

   Serial.println("Capture started...");
}

void loop()
{
   SensorCaptureState states = sensorCapture.update();
   if ((states & SENSOR_CAPTURE_STATE_CAPTURE_STARTED) != 0)
   {
      Serial.println("Sampling started...");
   }

   if (sensorCapture.readyForValue())
   {
      float sensorValue = sensor.readTemperatureF();
      SensorCaptureState valueStates = sensorCapture.addValue(sensorValue);
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
