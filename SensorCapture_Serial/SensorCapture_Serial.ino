// -------------------------------------------------------------------------------------------------
//
// Captures finite sensor values for a fixed run window (or until the sample cap is reached), stores
// them in RAM, and reports serial summaries plus an automatic serial dump.
//
// Capture flow:
// 1) Initialize serial and sensor, then allow a brief warm-up period.
// 2) Use a 1 ms timer to collect at most one sample per interval and store finite values in RAM.
// 3) Stop when MAX_SAMPLES are stored or SAMPLE_DURATION_MS expires.
//
// Output flow:
// - Serial summary includes run metrics, value stats, stabilization diagnostics, and a value histogram.
// - Post-capture window analysis prints effective rate/range/stddev by averaging window size.
// - Stored points are dumped to serial automatically after capture completes.
//
// Sensor modes:
// - Capacitor mode (default) reads from CapacitorSensor.
// - Temperature mode reads directly per loop iteration.
// -------------------------------------------------------------------------------------------------
#include "SensorCapture.h"
#include "SensorCaptureStats.h"
#include "SensorCaptureOutput.h"
#include "SerialX.h"
#include "TestSensor.h"

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
// Delay between serial dump rows to avoid overwhelming the serial link.
constexpr unsigned long PRINT_INTERVAL_MS = 2;
// Allowed stabilization delta as a fraction of average signal level.
constexpr float STABILITY_DELTA_PERCENT = 0.01f;
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
/// Computes and prints capture statistics and histogram data to Serial.
/// </summary>
void printCaptureSummary()
{
   SensorCaptureOutput::printCaptureSummary(sensorCapture, SAMPLE_DURATION_MS, 3);

   if ((sensorCapture.count() == 0) && !sensorCapture.isCaptureStabilized())
   {
      sensorCapture.printStabilizationDiagnosticsToSerial(3);
      Serial.println();
   }

   String valueHistogramTitle = String("Sensor Value Histogram (") + testSensor.unit() + ")";
   SensorCaptureOutput::printHistogramBins(valueHistogramTitle.c_str(), sensorCapture, HISTOGRAM_BINS, 3);
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
      float effectiveRate = (windowSize > 0) ? (1000.0f / static_cast<float>(windowSize * SAMPLE_INTERVAL_MS)) : NAN;

      float avgRange = NAN;
      float avgStdDev = NAN;
      size_t averageCount = 0;
      analysis.computeAverageSeriesStats(
         windowSize,
         avgRange,
         avgStdDev,
         averageCount);

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
   testSensor.begin();

   sensorCapture.startWarmUp();

   Serial.println("Warm-up started...");
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
}
