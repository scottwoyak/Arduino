//
// Capacitor sensor tuner: displays real-time measurements and runs calibration tests on Button A.
//
// Combines live monitoring with an automated calibration sweep for CapacitorSensor. During normal
// runtime, the display shows active resistor selection, discharge delay, deferred processing period,
// buffer size, average charge time, sample rate, and effective output rate.
//
// Calibration sweep (Button A):
// - Covers resistor/charge pin options (e.g., 1M, 470K, 100K, 47K), discharge delay values
//   (microseconds), and target effective output rates (samples per second); the deferred processing
//   period is fixed at 500 microseconds.
// - For each test case, the sensor is configured (discharge delay + deferred period + buffer size), a
//   fixed number of samples is captured, and stats are computed (average charge time, variation,
//   StdDev, raw rate, and effective rate).
// - Results are printed immediately to Serial in a fixed-width table, then ranked later by lowest
//   StdDev % per target rate.
// - After the sweep, a "best configurations" summary is printed for each target rate, and the best
//   configuration for 30/s is automatically applied and persisted for continued live viewing.
//
// Serial commands:
// - Send 'L' to reload and apply the last saved best configuration.
//
// Typical usage: let the live display stabilize, press Button A to run calibration, then use the
// serial rankings to choose resistor/delay combinations for your deployment.
//

#include <Arduino.h>
#include "CapacitorSensor.h"
#include "ArduinoBoard.h"

#ifndef ARDUINO_BUTTON_SUPPORTED
#error "This sketch requires a board with button support (e.g. Feather ESP32-S3 or Feather M0)."
#endif
#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif
#ifndef ARDUINO_PREFERENCES_SUPPORTED
#error "This sketch requires a board with Preferences support (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include "RollingStats.h"
#include "SerialTable.h"
#include "SerialX.h"
#include "Timer.h"

// ----------- Display
Arduino arduino;
Format chargeFormat("####.# us");
Format deferredFormat("#### us");
Format effectiveRateFormat("###/s");
Format sampleRateFormat("######/s");
constexpr size_t DISPLAY_INTERVAL_MS = 100;
Timer displayTimer(DISPLAY_INTERVAL_MS);

// ----------- Test Sweep Parameters
constexpr size_t STATS_SAMPLE_COUNT = 1000;
constexpr float TARGET_EFFECTIVE_RATES[] = { 15.0f, 30.0f, 50.0f };
constexpr size_t TARGET_EFFECTIVE_RATE_COUNT = sizeof(TARGET_EFFECTIVE_RATES) / sizeof(TARGET_EFFECTIVE_RATES[0]);
constexpr float PRIMARY_TARGET_EFFECTIVE_RATE = 30.0f;

constexpr uint16_t TEST_DISCHARGE_DELAYS[] = { 100, 200, 300 };
constexpr size_t TEST_DISCHARGE_DELAY_COUNT = sizeof(TEST_DISCHARGE_DELAYS) / sizeof(TEST_DISCHARGE_DELAYS[0]);

constexpr uint32_t FIXED_DEFERRED_PROCESSING_PERIOD_MICROS = 500; // 500 us allows 2000 samples/s for the internal averaging.

constexpr size_t MIN_TARGET_BUFFER_SIZE = 1;
constexpr size_t MAX_TARGET_BUFFER_SIZE = 500;

// ----------- Hardware Pin Assignments
constexpr uint8_t SENSE_PIN = 5;
constexpr uint8_t CHARGE_PIN_1M = 6;
constexpr uint8_t CHARGE_PIN_470K = 9;
constexpr uint8_t CHARGE_PIN_100K = 10;
constexpr uint8_t CHARGE_PIN_47K = 11;

// ----------- Resistor (Charge Pin) Sweep
/// <summary>Pairs a charge pin with its resistor value label for the sweep.</summary>
struct ResistorOption
{
   /// <summary>Charge pin that drives this resistor.</summary>
   uint8_t chargePin = 0;
   /// <summary>Human-readable resistor value label.</summary>
   const char* label = nullptr;
};

constexpr ResistorOption RESISTOR_OPTIONS[] = {
   { CHARGE_PIN_1M, "1M" },
   { CHARGE_PIN_470K, "470K" },
   { CHARGE_PIN_100K, "100K" },
   { CHARGE_PIN_47K, "47K" },
};
constexpr size_t RESISTOR_OPTION_COUNT = sizeof(RESISTOR_OPTIONS) / sizeof(RESISTOR_OPTIONS[0]);
constexpr size_t MAX_TEST_RESULTS = RESISTOR_OPTION_COUNT * TEST_DISCHARGE_DELAY_COUNT * TARGET_EFFECTIVE_RATE_COUNT;

// ----------- Preferences Keys
const char PREF_NAMESPACE[] = "cap_tune";
const char PREF_VALID_KEY[] = "valid";
const char PREF_CHARGE_PIN_KEY[] = "cpin";
const char PREF_DISCHARGE_KEY[] = "dly";
const char PREF_BUFFER_KEY[] = "buf";

// Forward declarations for types used in function signatures
struct TestCase;
struct TestRunResult;

/// <summary>Look up the resistor value label for a charge pin.</summary>
/// <param name="chargePin">Charge pin to look up</param>
/// <returns>Matching resistor value label, or "?" when the pin is unknown</returns>
const char* resistorLabel(uint8_t chargePin)
{
   for (size_t i = 0; i < RESISTOR_OPTION_COUNT; i++)
   {
      if (RESISTOR_OPTIONS[i].chargePin == chargePin)
      {
         return RESISTOR_OPTIONS[i].label;
      }
   }

   return "?";
}

/// <summary>Check whether a charge pin matches one of the configured resistor options.</summary>
/// <param name="chargePin">Charge pin to check</param>
/// <returns>True if the pin is a known resistor option</returns>
bool isKnownChargePin(uint8_t chargePin)
{
   for (size_t i = 0; i < RESISTOR_OPTION_COUNT; i++)
   {
      if (RESISTOR_OPTIONS[i].chargePin == chargePin)
      {
         return true;
      }
   }

   return false;
}

/// <summary>Look up the charge pin for a resistor value label.</summary>
/// <param name="label">Resistor value label to look up</param>
/// <returns>Matching charge pin, or the first resistor option's pin when the label is unknown</returns>
uint8_t chargePinFromResistorLabel(const char* label)
{
   if (label == nullptr)
   {
      return RESISTOR_OPTIONS[0].chargePin;
   }

   for (size_t i = 0; i < RESISTOR_OPTION_COUNT; i++)
   {
      if (strcmp(RESISTOR_OPTIONS[i].label, label) == 0)
      {
         return RESISTOR_OPTIONS[i].chargePin;
      }
   }

   return RESISTOR_OPTIONS[0].chargePin;
}

CapacitorSensor capacitorSensor(RESISTOR_OPTIONS[0].chargePin, SENSE_PIN);

/// <summary>Applies a resistor/discharge/buffer configuration to the capacitor sensor.</summary>
/// <param name="chargePin">Charge pin to select</param>
/// <param name="dischargeDelayMicros">Discharge delay in microseconds</param>
/// <param name="deferredProcessingPeriodMicros">Deferred processing period in microseconds</param>
/// <param name="bufferSize">Rolling average buffer size (falls back to the sensor default when zero)</param>
void applyConfiguration(uint8_t chargePin, uint16_t dischargeDelayMicros, uint32_t deferredProcessingPeriodMicros, size_t bufferSize)
{
   capacitorSensor.setChargePin(chargePin);
   capacitorSensor.setDischargeDelayMicros(dischargeDelayMicros);
   capacitorSensor.setDeferredProcessingPeriodMicros(deferredProcessingPeriodMicros);
   capacitorSensor.setBufferSize(bufferSize > 0 ? bufferSize : CapacitorSensor::DEFAULT_BUFFER_SIZE);
}

/// <summary>Persists the best-known configuration to Preferences.</summary>
/// <param name="chargePin">Charge pin to save</param>
/// <param name="dischargeDelayMicros">Discharge delay in microseconds to save</param>
/// <param name="bufferSize">Rolling average buffer size to save</param>
void saveBestConfiguration(uint8_t chargePin, uint16_t dischargeDelayMicros, size_t bufferSize)
{
   arduino.preferences.begin(PREF_NAMESPACE, false);
   arduino.preferences.putBool(PREF_VALID_KEY, true);
   arduino.preferences.putUChar(PREF_CHARGE_PIN_KEY, chargePin);
   arduino.preferences.putUShort(PREF_DISCHARGE_KEY, dischargeDelayMicros);
   arduino.preferences.putUInt(PREF_BUFFER_KEY, (uint32_t)bufferSize);
   arduino.preferences.end();
}

/// <summary>
/// Loads the best-known configuration from Preferences and applies it, printing a summary to Serial.
/// </summary>
void loadBestConfiguration()
{
   if (capacitorSensor.counter() == 0 || !isfinite(capacitorSensor.chargeTimeMicros()) || capacitorSensor.rate() <= 0.0f)
   {
      Serial.println("Saved config not applied: sensor is not ready yet.");
      return;
   }

   arduino.preferences.begin(PREF_NAMESPACE, true);
   bool hasBest = arduino.preferences.getBool(PREF_VALID_KEY, false);
   uint8_t chargePin = arduino.preferences.getUChar(PREF_CHARGE_PIN_KEY, RESISTOR_OPTIONS[0].chargePin);
   uint16_t dischargeDelayMicros = arduino.preferences.getUShort(PREF_DISCHARGE_KEY, CapacitorSensor::DEFAULT_DISCHARGE_DELAY_MICROS);
   uint32_t bufferSize = arduino.preferences.getUInt(PREF_BUFFER_KEY, CapacitorSensor::DEFAULT_BUFFER_SIZE);
   arduino.preferences.end();

   if (!hasBest)
   {
      Serial.println("No saved best configuration found.");
      return;
   }

   if (!isKnownChargePin(chargePin))
   {
      chargePin = RESISTOR_OPTIONS[0].chargePin;
   }
   if (dischargeDelayMicros == 0)
   {
      dischargeDelayMicros = CapacitorSensor::DEFAULT_DISCHARGE_DELAY_MICROS;
   }
   if (bufferSize < MIN_TARGET_BUFFER_SIZE || bufferSize > MAX_TARGET_BUFFER_SIZE)
   {
      bufferSize = CapacitorSensor::DEFAULT_BUFFER_SIZE;
   }

   applyConfiguration(chargePin, dischargeDelayMicros, FIXED_DEFERRED_PROCESSING_PERIOD_MICROS, (size_t)bufferSize);

   const SerialTable::Column columns[] = {
      { "Field", 18 },
      { "Value", 24 },
   };
   SerialTable table("Loaded Saved Best Configuration", columns, sizeof(columns) / sizeof(columns[0]));
   table.printHeader();
   table.printRow("Resistor Value", String(resistorLabel(chargePin)) + " (Pin " + String((unsigned long)chargePin) + ")");
   table.printRow("Discharge Delay", String((unsigned long)dischargeDelayMicros) + " us");
   table.printRow("Buffer Size", (unsigned long)bufferSize);
   table.printRow("Deferred Period", String((unsigned long)FIXED_DEFERRED_PROCESSING_PERIOD_MICROS) + " us");
   Serial.println();
}

/// <summary>Parameters for a single test case.</summary>
struct TestCase
{
   /// <summary>Target effective rate for this test.</summary>
   float targetEffectiveRate = NAN;
   /// <summary>Discharge delay in microseconds for this test.</summary>
   uint16_t dischargeDelayMicros = 0;
};

/// <summary>Result data from a single test run.</summary>
struct TestRunResult
{
   /// <summary>Resistor value label for this test.</summary>
   const char* resistorLabel = nullptr;
   /// <summary>Target effective rate for this test.</summary>
   float targetEffectiveRate = NAN;
   /// <summary>Discharge delay used for this test.</summary>
   uint16_t dischargeDelayMicros = 0;
   /// <summary>Deferred processing period used for this test.</summary>
   uint32_t deferredProcessingPeriodMicros = 0;
   /// <summary>Average charge time measured during test.</summary>
   float averageChargeTimeMicros = NAN;
   /// <summary>Min-to-max variation in rolling average during test.</summary>
   float chargeTimeVariationMicros = NAN;
   /// <summary>Standard deviation of charge time during test.</summary>
   float chargeStdDevMicros = NAN;
   /// <summary>Buffer size used for this test.</summary>
   size_t bufferSize = 0;
   /// <summary>Sample rate in Hz during this test.</summary>
   float rawRateHz = NAN;
   /// <summary>Effective output rate in Hz during this test.</summary>
   float effectiveRateHz = NAN;
};

/// <summary>Executes a single test run on a capacitor sensor.</summary>
class TestRun
{
private:
   CapacitorSensor& _sensor;

public:
   /// <summary>Create a test run for the given sensor.</summary>
   /// <param name="sensor">Reference to the capacitor sensor to test</param>
   explicit TestRun(CapacitorSensor& sensor)
      : _sensor(sensor)
   {
   }

   /// <summary>Execute a single test run with the specified parameters.</summary>
   /// <param name="targetEffectiveRate">Target effective output rate in Hz</param>
   /// <param name="dischargeDelayMicros">Discharge delay in microseconds</param>
   /// <returns>Test result data structure</returns>
   TestRunResult run(float targetEffectiveRate, uint16_t dischargeDelayMicros)
   {
      TestRunResult result;
      result.targetEffectiveRate = targetEffectiveRate;
      result.dischargeDelayMicros = dischargeDelayMicros;
      result.deferredProcessingPeriodMicros = FIXED_DEFERRED_PROCESSING_PERIOD_MICROS;

      // Stabilize the sensor at the new discharge delay, then read the rate
      _sensor.setDischargeDelayMicros(dischargeDelayMicros);
      _sensor.setDeferredProcessingPeriodMicros(FIXED_DEFERRED_PROCESSING_PERIOD_MICROS);
      delay(100);

      float rawRate = _sensor.rate();
      result.rawRateHz = rawRate;

      // Calculate target buffer size based on raw rate and target effective rate
      size_t targetBufferSize = _calculateTargetBufferSize(rawRate, targetEffectiveRate);
      result.bufferSize = targetBufferSize;

      // Configure sensor buffer for this test and collect a fixed number of output samples
      _sensor.setBufferSize(targetBufferSize);
      RollingStats stats(STATS_SAMPLE_COUNT);
      float measuredAverageMin = NAN;
      float measuredAverageMax = NAN;
      size_t sampleCount = 0;

      uint32_t lastCounter = _sensor.counter();
      while (sampleCount < STATS_SAMPLE_COUNT)
      {
         uint32_t counter = _sensor.counter();
         if (counter == lastCounter)
         {
            continue;
         }

         lastCounter = counter;
         float chargeTime = _sensor.chargeTimeMicros();
         if (isfinite(chargeTime))
         {
            stats.set(chargeTime);
            sampleCount++;

            float average = stats.average();
            if (isfinite(average))
            {
               if (!isfinite(measuredAverageMin) || average < measuredAverageMin)
               {
                  measuredAverageMin = average;
               }

               if (!isfinite(measuredAverageMax) || average > measuredAverageMax)
               {
                  measuredAverageMax = average;
               }
            }
         }
      }

      // Compile results
      result.averageChargeTimeMicros = stats.average();
      result.chargeStdDevMicros = stats.stdDev();
      if (isfinite(measuredAverageMin) && isfinite(measuredAverageMax))
      {
         result.chargeTimeVariationMicros = measuredAverageMax - measuredAverageMin;
      }

      float finalRawRate = _sensor.rate();
      result.effectiveRateHz = (result.bufferSize > 0) ? (finalRawRate / result.bufferSize) : 0;

      _sensor.setBufferSize(CapacitorSensor::DEFAULT_BUFFER_SIZE);
      _sensor.setDeferredProcessingPeriodMicros(FIXED_DEFERRED_PROCESSING_PERIOD_MICROS);

      return result;
   }

private:
   /// <summary>Calculate target buffer size based on raw rate and target effective rate.</summary>
   /// <param name="rawRate">Raw sensor rate in Hz</param>
   /// <param name="targetEffectiveRate">Target effective output rate in Hz</param>
   /// <returns>Computed buffer size constrained to valid range</returns>
   size_t _calculateTargetBufferSize(float rawRate, float targetEffectiveRate)
   {
      if (!isfinite(rawRate) || rawRate <= 0.0f)
      {
         return CapacitorSensor::DEFAULT_BUFFER_SIZE;
      }

      float targetBufferSize = rawRate / targetEffectiveRate;
      if (targetBufferSize < MIN_TARGET_BUFFER_SIZE)
      {
         targetBufferSize = MIN_TARGET_BUFFER_SIZE;
      }

      if (targetBufferSize > MAX_TARGET_BUFFER_SIZE)
      {
         targetBufferSize = MAX_TARGET_BUFFER_SIZE;
      }

      return (size_t)(targetBufferSize + 0.5f);
   }
};

/// <summary>Stores all test case parameters to be executed.</summary>
struct TestCaseParameters
{
   /// <summary>Array of test case parameters.</summary>
   TestCase cases[TEST_DISCHARGE_DELAY_COUNT * TARGET_EFFECTIVE_RATE_COUNT];
   /// <summary>Number of test cases.</summary>
   size_t count = 0;
};

TestCaseParameters testParameters;

void setup()
{
   SerialX::begin();
   arduino.begin();
   capacitorSensor.begin();

   size_t caseIndex = 0;
   for (size_t rateIndex = 0; rateIndex < TARGET_EFFECTIVE_RATE_COUNT; rateIndex++)
   {
      float targetRate = TARGET_EFFECTIVE_RATES[rateIndex];

      for (size_t delayIndex = 0; delayIndex < TEST_DISCHARGE_DELAY_COUNT; delayIndex++)
      {
         uint16_t delayMicros = TEST_DISCHARGE_DELAYS[delayIndex];

         testParameters.cases[caseIndex].targetEffectiveRate = targetRate;
         testParameters.cases[caseIndex].dischargeDelayMicros = delayMicros;
         caseIndex++;
      }
   }

   testParameters.count = caseIndex;
}

/// <summary>Find the result with the lowest StdDev % for a given target rate.</summary>
/// <param name="results">Array of all test results</param>
/// <param name="count">Number of results in the array</param>
/// <param name="targetRate">Target effective rate to match</param>
/// <returns>Pointer to the best result, or nullptr if none found</returns>
const TestRunResult* findBestResult(TestRunResult* results, size_t count, float targetRate)
{
   const TestRunResult* best = nullptr;
   float bestPct = NAN;

   for (size_t i = 0; i < count; i++)
   {
      const TestRunResult& r = results[i];
      if (!isfinite(r.targetEffectiveRate) || fabs(r.targetEffectiveRate - targetRate) >= 0.01f)
         continue;
      if (!isfinite(r.chargeStdDevMicros) || !isfinite(r.averageChargeTimeMicros) || r.averageChargeTimeMicros == 0.0f)
         continue;

      float pct = (r.chargeStdDevMicros / r.averageChargeTimeMicros) * 100.0f;
      if (best == nullptr || pct < bestPct)
      {
         best = &results[i];
         bestPct = pct;
      }
   }

   return best;
}

/// <summary>Print the top 3 configurations per target rate ranked by lowest StdDev %.</summary>
/// <param name="results">Array of all test results</param>
/// <param name="count">Number of results in the array</param>
void printBestConfigurations(TestRunResult* results, size_t count)
{
   Serial.println();
   Serial.println("------- Best Configurations (Top 3 Lowest StdDev %) -------");

   for (size_t rateIndex = 0; rateIndex < TARGET_EFFECTIVE_RATE_COUNT; rateIndex++)
   {
      float targetRate = TARGET_EFFECTIVE_RATES[rateIndex];

      // Collect indices of results matching this target rate
      size_t indices[RESISTOR_OPTION_COUNT * TEST_DISCHARGE_DELAY_COUNT];
      size_t indexCount = 0;
      for (size_t i = 0; i < count; i++)
      {
         if (isfinite(results[i].targetEffectiveRate) &&
             fabs(results[i].targetEffectiveRate - targetRate) < 0.01f)
         {
            indices[indexCount++] = i;
         }
      }

      // Sort by stddev % ascending (selection sort)
      for (size_t a = 0; a < indexCount; a++)
      {
         for (size_t b = a + 1; b < indexCount; b++)
         {
            const TestRunResult& ra = results[indices[a]];
            const TestRunResult& rb = results[indices[b]];
            float pctA = NAN, pctB = NAN;
            if (isfinite(ra.chargeStdDevMicros) && isfinite(ra.averageChargeTimeMicros) && ra.averageChargeTimeMicros != 0.0f)
               pctA = (ra.chargeStdDevMicros / ra.averageChargeTimeMicros) * 100.0f;
            if (isfinite(rb.chargeStdDevMicros) && isfinite(rb.averageChargeTimeMicros) && rb.averageChargeTimeMicros != 0.0f)
               pctB = (rb.chargeStdDevMicros / rb.averageChargeTimeMicros) * 100.0f;

            bool shouldSwap = (!isfinite(pctA) && isfinite(pctB)) ||
                              (isfinite(pctA) && isfinite(pctB) && pctB < pctA);
            if (shouldSwap)
            {
               size_t tmp = indices[a];
               indices[a] = indices[b];
               indices[b] = tmp;
            }
         }
      }

      Serial.println();
      Serial.print("  Target: ");
      Serial.print((int)round(targetRate));
      Serial.println("/s");

      const SerialTable::Column columns[] = {
         { "Rank", 8 },
         { "Resistor (Pin)", 18 },
         { "Buffer", 8 },
         { "Discharge", 12 },
         { "StdDev %", 10 },
      };
      SerialTable table("Best Configurations", columns, sizeof(columns) / sizeof(columns[0]));
      table.printHeader();

      size_t printCount = (indexCount < 3) ? indexCount : 3;
      for (size_t rank = 0; rank < printCount; rank++)
      {
         const TestRunResult& r = results[indices[rank]];
         uint8_t resistorPin = chargePinFromResistorLabel(r.resistorLabel);
         String stdDevPct = "----";
         if (isfinite(r.chargeStdDevMicros) && isfinite(r.averageChargeTimeMicros) && r.averageChargeTimeMicros != 0.0f)
         {
            float pct = (r.chargeStdDevMicros / r.averageChargeTimeMicros) * 100.0f;
            stdDevPct = String(pct, 2) + " %";
         }

         table.printRow(
            (unsigned long)(rank + 1),
            String(r.resistorLabel ? r.resistorLabel : "?") + " (" + String((unsigned long)resistorPin) + ")",
            (unsigned long)r.bufferSize,
            String((unsigned long)r.dischargeDelayMicros) + " us",
            stdDevPct);
      }

      Serial.println();
   }
}

/// <summary>Prints a single aggregate statistics row if any samples were collected.</summary>
/// <param name="table">Table to print the row to</param>
/// <param name="label">Row label</param>
/// <param name="avgStats">Aggregated average-charge-time statistics</param>
/// <param name="stdDevStats">Aggregated StdDev statistics</param>
void printAggregateRow(SerialTable& table, const char* label, const Stats& avgStats, const Stats& stdDevStats)
{
   if (avgStats.count() == 0)
   {
      return;
   }

   float rangeMicros = avgStats.max() - avgStats.min();
   table.printRow(
      label,
      SerialTable::fixed(avgStats.get(), 3),
      SerialTable::fixed(stdDevStats.get(), 3),
      SerialTable::fixed(rangeMicros, 3));
}

/// <summary>Prints aggregate charge-time statistics grouped by resistor value.</summary>
/// <param name="results">Array of all test results</param>
/// <param name="count">Number of results in the array</param>
void printAggregateByResistor(const TestRunResult* results, size_t count)
{
   const SerialTable::Column columns[] = {
      { "Value", 14 },
      { "Avg(us)", 12 },
      { "StdDev(us)", 12 },
      { "Range(us)", 12 },
   };
   SerialTable table("By Resistor", columns, sizeof(columns) / sizeof(columns[0]));
   table.printHeader();

   for (size_t resistorIndex = 0; resistorIndex < RESISTOR_OPTION_COUNT; resistorIndex++)
   {
      Stats avgStats;
      Stats stdDevStats;
      for (size_t i = 0; i < count; i++)
      {
         const TestRunResult& r = results[i];
         if (r.resistorLabel == nullptr || strcmp(r.resistorLabel, RESISTOR_OPTIONS[resistorIndex].label) != 0)
         {
            continue;
         }

         if (isfinite(r.averageChargeTimeMicros))
         {
            avgStats.add(r.averageChargeTimeMicros);
         }
         if (isfinite(r.chargeStdDevMicros))
         {
            stdDevStats.add(r.chargeStdDevMicros);
         }
      }

      printAggregateRow(table, RESISTOR_OPTIONS[resistorIndex].label, avgStats, stdDevStats);
   }

   Serial.println();
}

/// <summary>Prints aggregate charge-time statistics grouped by target effective rate.</summary>
/// <param name="results">Array of all test results</param>
/// <param name="count">Number of results in the array</param>
void printAggregateByTargetRate(const TestRunResult* results, size_t count)
{
   const SerialTable::Column columns[] = {
      { "Value", 14 },
      { "Avg(us)", 12 },
      { "StdDev(us)", 12 },
      { "Range(us)", 12 },
   };
   SerialTable table("By Target Rate", columns, sizeof(columns) / sizeof(columns[0]));
   table.printHeader();

   for (size_t rateIndex = 0; rateIndex < TARGET_EFFECTIVE_RATE_COUNT; rateIndex++)
   {
      float targetRate = TARGET_EFFECTIVE_RATES[rateIndex];
      Stats avgStats;
      Stats stdDevStats;

      for (size_t i = 0; i < count; i++)
      {
         const TestRunResult& r = results[i];
         if (!isfinite(r.targetEffectiveRate) || fabs(r.targetEffectiveRate - targetRate) >= 0.01f)
         {
            continue;
         }

         if (isfinite(r.averageChargeTimeMicros))
         {
            avgStats.add(r.averageChargeTimeMicros);
         }
         if (isfinite(r.chargeStdDevMicros))
         {
            stdDevStats.add(r.chargeStdDevMicros);
         }
      }

      char label[16] = "";
      snprintf(label, sizeof(label), "%d/s", (int)round(targetRate));
      printAggregateRow(table, label, avgStats, stdDevStats);
   }

   Serial.println();
}

/// <summary>Prints aggregate charge-time statistics grouped into buffer-size buckets.</summary>
/// <param name="results">Array of all test results</param>
/// <param name="count">Number of results in the array</param>
void printAggregateByBufferSize(const TestRunResult* results, size_t count)
{
   const SerialTable::Column columns[] = {
      { "Value", 14 },
      { "N", 6 },
      { "Avg(us)", 12 },
      { "StdDev(us)", 12 },
      { "Range(us)", 12 },
   };
   SerialTable table("By Buffer Size", columns, sizeof(columns) / sizeof(columns[0]));
   table.printHeader();

   if (count == 0)
   {
      Serial.println();
      return;
   }

   size_t minBufferSize = results[0].bufferSize;
   size_t maxBufferSize = results[0].bufferSize;
   for (size_t i = 1; i < count; i++)
   {
      if (results[i].bufferSize < minBufferSize)
      {
         minBufferSize = results[i].bufferSize;
      }
      if (results[i].bufferSize > maxBufferSize)
      {
         maxBufferSize = results[i].bufferSize;
      }
   }

   const size_t BUCKET_COUNT = 5;
   size_t span = (maxBufferSize - minBufferSize) + 1;
   for (size_t bucket = 0; bucket < BUCKET_COUNT; bucket++)
   {
      size_t start = minBufferSize + ((span * bucket) / BUCKET_COUNT);
      size_t end = minBufferSize + ((span * (bucket + 1)) / BUCKET_COUNT) - 1;
      if (bucket == BUCKET_COUNT - 1)
      {
         end = maxBufferSize;
      }
      if (end < start)
      {
         end = start;
      }

      Stats avgStats;
      Stats stdDevStats;
      size_t n = 0;

      for (size_t i = 0; i < count; i++)
      {
         const TestRunResult& r = results[i];
         if (r.bufferSize < start || r.bufferSize > end)
         {
            continue;
         }

         n++;
         if (isfinite(r.averageChargeTimeMicros))
         {
            avgStats.add(r.averageChargeTimeMicros);
         }
         if (isfinite(r.chargeStdDevMicros))
         {
            stdDevStats.add(r.chargeStdDevMicros);
         }
      }

      if (n == 0)
      {
         continue;
      }

      float rangeMicros = avgStats.max() - avgStats.min();
      char label[16] = "";
      snprintf(label, sizeof(label), "%lu-%lu", (unsigned long)start, (unsigned long)end);
      table.printRow(
         label,
         (unsigned long)n,
         SerialTable::fixed(avgStats.get(), 3),
         SerialTable::fixed(stdDevStats.get(), 3),
         SerialTable::fixed(rangeMicros, 3));
   }

   Serial.println();
}

/// <summary>Prints aggregate charge-time statistics grouped by discharge delay.</summary>
/// <param name="results">Array of all test results</param>
/// <param name="count">Number of results in the array</param>
void printAggregateByDischargeDelay(const TestRunResult* results, size_t count)
{
   const SerialTable::Column columns[] = {
      { "Value", 14 },
      { "Avg(us)", 12 },
      { "StdDev(us)", 12 },
      { "Range(us)", 12 },
   };
   SerialTable table("By Discharge Delay", columns, sizeof(columns) / sizeof(columns[0]));
   table.printHeader();

   for (size_t delayIndex = 0; delayIndex < TEST_DISCHARGE_DELAY_COUNT; delayIndex++)
   {
      uint16_t delayMicros = TEST_DISCHARGE_DELAYS[delayIndex];
      Stats avgStats;
      Stats stdDevStats;

      for (size_t i = 0; i < count; i++)
      {
         const TestRunResult& r = results[i];
         if (r.dischargeDelayMicros != delayMicros)
         {
            continue;
         }

         if (isfinite(r.averageChargeTimeMicros))
         {
            avgStats.add(r.averageChargeTimeMicros);
         }
         if (isfinite(r.chargeStdDevMicros))
         {
            stdDevStats.add(r.chargeStdDevMicros);
         }
      }

      char label[16] = "";
      snprintf(label, sizeof(label), "%lu us", (unsigned long)delayMicros);
      printAggregateRow(table, label, avgStats, stdDevStats);
   }

   Serial.println();
}

/// <summary>Prints all aggregate statistics groupings (by resistor, target rate, buffer size, and discharge delay).</summary>
/// <param name="results">Array of all test results</param>
/// <param name="count">Number of results in the array</param>
void printAggregateStatistics(const TestRunResult* results, size_t count)
{
   Serial.println();
   Serial.println("------- Aggregate Statistics -------");

   printAggregateByResistor(results, count);
   printAggregateByTargetRate(results, count);
   printAggregateByBufferSize(results, count);
   printAggregateByDischargeDelay(results, count);
}

void loop()
{
   if (Serial.available() > 0)
   {
      char command = (char)Serial.read();
      if (command == 'L' || command == 'l')
      {
         loadBestConfiguration();
      }
   }

   // Button A pressed: execute all tests
   if (arduino.buttonA.wasPressed())
   {
      Serial.println();
      Serial.println("Starting tests...");
      Serial.println();

      TestRunResult allResults[MAX_TEST_RESULTS];
      size_t allResultCount = 0;

      const SerialTable::Column resultColumns[] = {
         { "Resistor", 10 },
         { "Target", 10 },
         { "Buffer", 8 },
         { "Discharge", 12 },
         { "Avg(us)", 12 },
         { "Range(us)", 12 },
         { "StdDev %", 10 },
         { "Raw", 10 },
         { "Effective", 11 },
      };
      SerialTable resultsTable("Test Results", resultColumns, sizeof(resultColumns) / sizeof(resultColumns[0]));
      resultsTable.printHeader();

      const size_t totalTests = RESISTOR_OPTION_COUNT * testParameters.count;
      arduino.clearDisplay();

      // Outer loop: test every resistor value (charge pin)
      for (size_t resistorIndex = 0; resistorIndex < RESISTOR_OPTION_COUNT; resistorIndex++)
      {
         capacitorSensor.setChargePin(RESISTOR_OPTIONS[resistorIndex].chargePin);

         // Execute all test cases for this resistor value
         for (size_t i = 0; i < testParameters.count; i++)
         {
            size_t currentTest = resistorIndex * testParameters.count + i + 1;

            // Update display with current test parameters and progress
            arduino.setCursor(0, 0);
            arduino.setTextSize(2);
            arduino.println("Running Tests...", Color::HEADING);
            arduino.moveCursorY(-2);
            arduino.println("    Resistor: ", RESISTOR_OPTIONS[resistorIndex].label, Color::VALUE);
            arduino.moveCursorY(-2);
            arduino.println("       Delay: ", testParameters.cases[i].dischargeDelayMicros, chargeFormat, Color::VALUE);
            arduino.moveCursorY(-2);
            arduino.println("      Target: ", String((int)round(testParameters.cases[i].targetEffectiveRate)) + "/s", Color::VALUE);
            arduino.moveCursorY(-2);
            arduino.println("        Test: ", String(currentTest) + "/" + String(totalTests), Color::VALUE2);
            arduino.moveCursorY(-2);

            TestRun testRun(capacitorSensor);
            TestRunResult result = testRun.run(
               testParameters.cases[i].targetEffectiveRate,
               testParameters.cases[i].dischargeDelayMicros);
            result.resistorLabel = RESISTOR_OPTIONS[resistorIndex].label;
            allResults[allResultCount++] = result;

            // Print result immediately after test completes
            String avgText = isfinite(result.averageChargeTimeMicros) ? String(result.averageChargeTimeMicros, 1) + " us" : "----";
            String rangeText = isfinite(result.chargeTimeVariationMicros) ? String(result.chargeTimeVariationMicros, 2) + " us" : "----";
            String stdDevPctText = "----";
            if (isfinite(result.chargeStdDevMicros) && isfinite(result.averageChargeTimeMicros) && result.averageChargeTimeMicros != 0.0f)
            {
               float chargeStdDevPercent = (result.chargeStdDevMicros / result.averageChargeTimeMicros) * 100.0f;
               stdDevPctText = String(chargeStdDevPercent, 2) + " %";
            }

            resultsTable.printRow(
               RESISTOR_OPTIONS[resistorIndex].label,
               String((int)round(result.targetEffectiveRate)) + "/s",
               (unsigned long)result.bufferSize,
               String((unsigned long)result.dischargeDelayMicros) + " us",
               avgText,
               rangeText,
               stdDevPctText,
               String((int)round(result.rawRateHz)) + "/s",
               String((int)round(result.effectiveRateHz)) + "/s");

            // Yield to allow display and other updates
            delay(10);
         }
      }

      printAggregateStatistics(allResults, allResultCount);
      printBestConfigurations(allResults, allResultCount);
      Serial.println();
      Serial.println("Testing Complete");

      // Apply and persist the best configuration for the primary target rate
      const TestRunResult* best = findBestResult(allResults, allResultCount, PRIMARY_TARGET_EFFECTIVE_RATE);
      if (best != nullptr)
      {
         uint8_t bestChargePin = chargePinFromResistorLabel(best->resistorLabel);
         applyConfiguration(
            bestChargePin,
            best->dischargeDelayMicros,
            best->deferredProcessingPeriodMicros,
            best->bufferSize);
         saveBestConfiguration(
            bestChargePin,
            best->dischargeDelayMicros,
            best->bufferSize);
      }
      else
      {
         applyConfiguration(
            RESISTOR_OPTIONS[0].chargePin,
            CapacitorSensor::DEFAULT_DISCHARGE_DELAY_MICROS,
            FIXED_DEFERRED_PROCESSING_PERIOD_MICROS,
            CapacitorSensor::DEFAULT_BUFFER_SIZE);
      }
   }

   if (displayTimer.ready())
   {
      arduino.setCursor(0, 0);
      arduino.setTextSize(3);
      arduino.println("Capacitor Tuning", Color::HEADING);
      arduino.moveCursorY(-2);

      arduino.setTextSize(2);
      arduino.println("       Resistor: ", resistorLabel(capacitorSensor.chargePin()), Color::VALUE);
      arduino.moveCursorY(-2);
      arduino.println(" Discharge Time: ", capacitorSensor.dischargeDelayMicros(), chargeFormat, Color::VALUE);
      arduino.moveCursorY(-2);
      arduino.println("    Buffer Size: ", (int)capacitorSensor.bufferSize(), Color::VALUE);
      arduino.moveCursorY(-2);

      float rawRate = capacitorSensor.rate();
      float effectiveRate = (capacitorSensor.bufferSize() > 0) ? (rawRate / (float)capacitorSensor.bufferSize()) : 0;

      arduino.println("Avg Charge Time: ", capacitorSensor.chargeTimeMicros(), chargeFormat, Color::VALUE3);
      arduino.moveCursorY(-2);
      arduino.println("    Sample Rate: ", (int)round(rawRate), sampleRateFormat, Color::VALUE2);
      arduino.moveCursorY(-2);
      arduino.println(" Effective Rate: ", effectiveRate, effectiveRateFormat, Color::VALUE2);
      arduino.moveCursorY(-2);
   }
}

