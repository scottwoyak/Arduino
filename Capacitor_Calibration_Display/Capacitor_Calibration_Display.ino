//
// Capacitor sensor tuner: displays real-time measurements and runs calibration tests on Button A.
//
// Detailed behavior:
// - This sketch combines live monitoring with an automated calibration sweep for CapacitorSensor.
// - During normal runtime, the display shows active resistor selection, discharge delay, deferred
//   processing period, buffer size, average charge time, sample rate, and effective output rate.
// - Pressing Button A starts a full test matrix sweep and prints per-test rows to Serial in a
//   fixed-width table for easier comparison and copy/paste analysis.
//
// Calibration sweep dimensions:
// - Resistor / charge pin options (e.g., 1M, 470K, 100K, 47K).
// - Discharge delay values (microseconds).
// - Deferred processing period values (microseconds).
// - Target effective output rates (samples per second).
//
// For each test case:
// - The sensor is configured (discharge delay + deferred period + buffer size).
// - A fixed number of samples is captured.
// - Stats are computed: average charge time, variation, StdDev, raw rate, and effective rate.
// - Results are printed immediately, then ranked later by lowest StdDev % per target rate.
//
// Post-run behavior:
// - A "best configurations" summary is printed for each target rate.
// - The best configuration for 30/s is automatically applied for continued live viewing.
//
// Typical usage:
// - Let the live display stabilize.
// - Press Button A to run calibration.
// - Use serial rankings to choose resistor/delay/deferred-period combinations for your deployment.
//
#include <Arduino.h>
#include "CapacitorSensor.h"
#include "Feather.h"
#include "RollingStats.h"
#include "SerialX.h"
#include "Timer.h"

// ----------- Display
Feather feather;
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

constexpr uint32_t TEST_DEFERRED_PROCESSING_PERIODS[] = { 500, 750, 1000 };
constexpr size_t TEST_DEFERRED_PROCESSING_PERIOD_COUNT = sizeof(TEST_DEFERRED_PROCESSING_PERIODS) / sizeof(TEST_DEFERRED_PROCESSING_PERIODS[0]);

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

CapacitorSensor capacitorSensor(RESISTOR_OPTIONS[0].chargePin, SENSE_PIN);

/// <summary>Parameters for a single test case.</summary>
struct TestCase
{
   /// <summary>Target effective rate for this test.</summary>
   float targetEffectiveRate = NAN;
   /// <summary>Discharge delay in microseconds for this test.</summary>
   uint16_t dischargeDelayMicros = 0;
   /// <summary>Deferred processing period in microseconds for this test.</summary>
   uint32_t deferredProcessingPeriodMicros = 0;
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
   /// <param name="deferredProcessingPeriodMicros">Deferred processing period in microseconds</param>
   /// <returns>Test result data structure</returns>
   TestRunResult run(float targetEffectiveRate, uint16_t dischargeDelayMicros, uint32_t deferredProcessingPeriodMicros)
   {
      TestRunResult result;
      result.targetEffectiveRate = targetEffectiveRate;
      result.dischargeDelayMicros = dischargeDelayMicros;
      result.deferredProcessingPeriodMicros = deferredProcessingPeriodMicros;

      // Stabilize the sensor at the new discharge delay, then read the rate
      _sensor.setDischargeDelayMicros(dischargeDelayMicros);
      _sensor.setDeferredProcessingPeriodMicros(deferredProcessingPeriodMicros);
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
      _sensor.setDeferredProcessingPeriodMicros(CapacitorSensor::DEFAULT_DEFERRED_PROCESSING_PERIOD_MICROS);

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
   TestCase cases[TEST_DISCHARGE_DELAY_COUNT * TEST_DEFERRED_PROCESSING_PERIOD_COUNT * TARGET_EFFECTIVE_RATE_COUNT];
   /// <summary>Number of test cases.</summary>
   size_t count = 0;
};

TestCaseParameters testParameters;

void setup()
{
   SerialX::begin();
   feather.begin();
   capacitorSensor.begin();

   size_t caseIndex = 0;
   for (size_t rateIndex = 0; rateIndex < TARGET_EFFECTIVE_RATE_COUNT; rateIndex++)
   {
      float targetRate = TARGET_EFFECTIVE_RATES[rateIndex];

      for (size_t delayIndex = 0; delayIndex < TEST_DISCHARGE_DELAY_COUNT; delayIndex++)
      {
         uint16_t delayMicros = TEST_DISCHARGE_DELAYS[delayIndex];

         for (size_t deferredIndex = 0; deferredIndex < TEST_DEFERRED_PROCESSING_PERIOD_COUNT; deferredIndex++)
         {
            uint32_t deferredProcessingPeriodMicros = TEST_DEFERRED_PROCESSING_PERIODS[deferredIndex];

            testParameters.cases[caseIndex].targetEffectiveRate = targetRate;
            testParameters.cases[caseIndex].dischargeDelayMicros = delayMicros;
            testParameters.cases[caseIndex].deferredProcessingPeriodMicros = deferredProcessingPeriodMicros;
            caseIndex++;
         }
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
      size_t indices[RESISTOR_OPTION_COUNT * TEST_DISCHARGE_DELAY_COUNT * TEST_DEFERRED_PROCESSING_PERIOD_COUNT];
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

      SerialX::print("Rank", 8);
      SerialX::print("Resistor", 10);
      SerialX::print("Buffer", 8);
      SerialX::print("Discharge", 12);
      SerialX::print("Deferred", 12);
      SerialX::println("StdDev %", 10);

      SerialX::print("----", 8);
      SerialX::print("--------", 10);
      SerialX::print("------", 8);
      SerialX::print("----------", 12);
      SerialX::print("----------", 12);
      SerialX::println("--------", 10);

      size_t printCount = (indexCount < 3) ? indexCount : 3;
      for (size_t rank = 0; rank < printCount; rank++)
      {
         const TestRunResult& r = results[indices[rank]];
         SerialX::print((unsigned long)(rank + 1), 8);
         SerialX::print(r.resistorLabel ? r.resistorLabel : "?", 10);
         SerialX::print((unsigned long)r.bufferSize, 8);
         SerialX::print(String((unsigned long)r.dischargeDelayMicros) + " us", 12);
         SerialX::print(String((unsigned long)r.deferredProcessingPeriodMicros) + " us", 12);
         if (isfinite(r.chargeStdDevMicros) && isfinite(r.averageChargeTimeMicros) && r.averageChargeTimeMicros != 0.0f)
         {
            float pct = (r.chargeStdDevMicros / r.averageChargeTimeMicros) * 100.0f;
            SerialX::println(String(pct, 2) + " %", 10);
         }
         else
         {
            SerialX::println("----", 10);
         }
      }
   }
}

void loop()
{
   // Button A pressed: execute all tests
   if (feather.buttonA.wasPressed())
   {
      Serial.println();
      Serial.println("Starting tests...");
      Serial.println();

      const size_t MAX_RESULTS = RESISTOR_OPTION_COUNT * TEST_DISCHARGE_DELAY_COUNT * TEST_DEFERRED_PROCESSING_PERIOD_COUNT * TARGET_EFFECTIVE_RATE_COUNT;
      TestRunResult allResults[MAX_RESULTS];
      size_t allResultCount = 0;

      // Print results header once
      SerialX::print("Resistor", 10);
      SerialX::print("Target", 10);
      SerialX::print("Buffer", 8);
      SerialX::print("Discharge", 12);
      SerialX::print("Deferred", 12);
      SerialX::print("Avg", 12);
      SerialX::print("StdDev", 12);
      SerialX::print("StdDev", 10);
      SerialX::print("Sample", 10);
      SerialX::print("Raw", 10);
      SerialX::println("Effective", 11);

      SerialX::print("Value", 10);
      SerialX::print("Rate", 10);
      SerialX::print("Size", 8);
      SerialX::print("Delay", 12);
      SerialX::print("Period", 12);
      SerialX::print("Charge", 12);
      SerialX::print("Charge", 12);
      SerialX::print("%", 10);
      SerialX::print("Rate", 10);
      SerialX::print("Rate", 10);
      SerialX::println("Rate", 11);

      SerialX::print("--------", 10);
      SerialX::print("--------", 10);
      SerialX::print("------", 8);
      SerialX::print("----------", 12);
      SerialX::print("----------", 12);
      SerialX::print("----------", 12);
      SerialX::print("----------", 12);
      SerialX::print("--------", 10);
      SerialX::print("--------", 10);
      SerialX::print("--------", 10);
      SerialX::println("---------", 11);

      const size_t totalTests = RESISTOR_OPTION_COUNT * testParameters.count;
      feather.clearDisplay();

      // Outer loop: test every resistor value (charge pin)
      for (size_t resistorIndex = 0; resistorIndex < RESISTOR_OPTION_COUNT; resistorIndex++)
      {
         const ResistorOption& resistor = RESISTOR_OPTIONS[resistorIndex];
         capacitorSensor.setChargePin(resistor.chargePin);

         // Execute all test cases for this resistor value
         for (size_t i = 0; i < testParameters.count; i++)
         {
            const TestCase& testCase = testParameters.cases[i];
            size_t currentTest = resistorIndex * testParameters.count + i + 1;

            // Update display with current test parameters and progress
            feather.setCursor(0, 0);
            feather.setTextSize(2);
            feather.println("Running Tests...", Color::HEADING);
            feather.moveCursorY(-2);
            feather.println("    Resistor: ", resistor.label, Color::VALUE);
            feather.moveCursorY(-2);
            feather.println("       Delay: ", testCase.dischargeDelayMicros, chargeFormat, Color::VALUE);
            feather.moveCursorY(-2);
            feather.println("    Deferred: ", testCase.deferredProcessingPeriodMicros, deferredFormat, Color::VALUE);
            feather.moveCursorY(-2);
            feather.println("      Target: ", String((int)round(testCase.targetEffectiveRate)) + "/s", Color::VALUE);
            feather.moveCursorY(-2);
            feather.println("        Test: ", String(currentTest) + "/" + String(totalTests), Color::VALUE);
            feather.moveCursorY(-2);

            // Run the test
            TestRun testRun(capacitorSensor);
            TestRunResult result = testRun.run(testCase.targetEffectiveRate, testCase.dischargeDelayMicros, testCase.deferredProcessingPeriodMicros);
            result.resistorLabel = resistor.label;
            allResults[allResultCount++] = result;

            // Print result immediately after test completes
            SerialX::print(resistor.label, 10);
            SerialX::print(String((int)round(result.targetEffectiveRate)) + "/s", 10);
            SerialX::print((unsigned long)result.bufferSize, 8);
            SerialX::print(String((unsigned long)result.dischargeDelayMicros) + " us", 12);
            SerialX::print(String((unsigned long)result.deferredProcessingPeriodMicros) + " us", 12);
            if (isfinite(result.averageChargeTimeMicros))
            {
               SerialX::print(String(result.averageChargeTimeMicros, 1) + " us", 12);
            }
            else
            {
               SerialX::print("----", 12);
            }
            if (isfinite(result.chargeTimeVariationMicros))
            {
               SerialX::print(String(result.chargeTimeVariationMicros, 2) + " us", 12);
            }
            else
            {
               SerialX::print("----", 12);
            }
            if (isfinite(result.chargeStdDevMicros) && isfinite(result.averageChargeTimeMicros) && result.averageChargeTimeMicros != 0.0f)
            {
               float chargeStdDevPercent = (result.chargeStdDevMicros / result.averageChargeTimeMicros) * 100.0f;
               SerialX::print(String(chargeStdDevPercent, 2) + " %", 10);
            }
            else
            {
               SerialX::print("----", 10);
            }
            SerialX::print(String((int)round(result.rawRateHz)) + "/s", 10);
            SerialX::print(String((int)round(capacitorSensor.rate())) + "/s", 10);
            SerialX::println(String((int)round(result.effectiveRateHz)) + "/s", 11);

            // Yield to allow display and other updates
            delay(10);
         }
      }

      printBestConfigurations(allResults, allResultCount);
      Serial.println();
      Serial.println("Testing Complete");

      // Apply the best configuration for the primary target rate
      const TestRunResult* best = findBestResult(allResults, allResultCount, PRIMARY_TARGET_EFFECTIVE_RATE);
      if (best != nullptr)
      {
         for (size_t i = 0; i < RESISTOR_OPTION_COUNT; i++)
         {
            if (RESISTOR_OPTIONS[i].label == best->resistorLabel)
            {
               capacitorSensor.setChargePin(RESISTOR_OPTIONS[i].chargePin);
               break;
            }
         }
         capacitorSensor.setDischargeDelayMicros(best->dischargeDelayMicros);
         capacitorSensor.setDeferredProcessingPeriodMicros(best->deferredProcessingPeriodMicros);
         capacitorSensor.setBufferSize(best->bufferSize);
      }
      else
      {
         capacitorSensor.setChargePin(RESISTOR_OPTIONS[0].chargePin);
         capacitorSensor.setDeferredProcessingPeriodMicros(CapacitorSensor::DEFAULT_DEFERRED_PROCESSING_PERIOD_MICROS);
      }
   }

   if (displayTimer.ready())
   {
      feather.setCursor(0, 0);
      feather.setTextSize(3);
      feather.println("Capacitor Tuning", Color::HEADING);
      feather.moveCursorY(-2);

      feather.setTextSize(2);
      feather.println("       Resistor: ", resistorLabel(capacitorSensor.chargePin()), Color::VALUE);
      feather.moveCursorY(-2);
      feather.println(" Discharge Time: ", capacitorSensor.dischargeDelayMicros(), chargeFormat, Color::VALUE);
      feather.moveCursorY(-2);
      feather.println("      Deferred: ", capacitorSensor.deferredProcessingPeriodMicros(), deferredFormat, Color::VALUE);
      feather.moveCursorY(-2);
      feather.println("    Buffer Size: ", (int)capacitorSensor.bufferSize(), Color::VALUE);
      feather.moveCursorY(-2);

      float rawRate = capacitorSensor.rate();
      float effectiveRate = (capacitorSensor.bufferSize() > 0) ? (rawRate / (float)capacitorSensor.bufferSize()) : 0;

      feather.println("Avg Charge Time: ", capacitorSensor.chargeTimeMicros(), chargeFormat, Color::VALUE3);
      feather.moveCursorY(-2);
      feather.println("    Sample Rate: ", (int)round(rawRate), sampleRateFormat, Color::VALUE2);
      feather.moveCursorY(-2);
      feather.println(" Effective Rate: ", effectiveRate, effectiveRateFormat, Color::VALUE2);
      feather.moveCursorY(-2);
   }
}

