//
// Capacitor sensor tuner: displays real-time measurements and runs calibration tests on Button A.
//
// Monitors capacitor charge time and sample rate in real-time. Button A executes a full
// calibration sweep across every resistor value (charge pin), and for each one sweeps the
// discharge delays and target effective rates, outputting results to serial for analysis.
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
Format effectiveRateFormat("###/s");
Format sampleRateFormat("######/s");
constexpr size_t DISPLAY_INTERVAL_MS = 100;
Timer displayTimer(DISPLAY_INTERVAL_MS);

// ----------- Test Sweep Parameters
constexpr size_t STATS_SAMPLE_COUNT = 1000;
constexpr float TARGET_EFFECTIVE_RATES[] = { 10.0f, 20.0f, 30.0f };
constexpr size_t TARGET_EFFECTIVE_RATE_COUNT = sizeof(TARGET_EFFECTIVE_RATES) / sizeof(TARGET_EFFECTIVE_RATES[0]);

constexpr uint16_t TEST_DISCHARGE_DELAYS[] = { 100, 200, 500, 1000 };
constexpr size_t TEST_DISCHARGE_DELAY_COUNT = sizeof(TEST_DISCHARGE_DELAYS) / sizeof(TEST_DISCHARGE_DELAYS[0]);

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

      // Stabilize the sensor at the new discharge delay, then read the rate
      _sensor.setDischargeDelayMicros(dischargeDelayMicros);
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

      float lastChargeTime = _sensor.chargeTimeMicros();
      Timer sampleTimer(1);
      while (sampleCount < STATS_SAMPLE_COUNT)
      {
         if (sampleTimer.ready())
         {
            float chargeTime = _sensor.chargeTimeMicros();
            if (chargeTime != lastChargeTime && isfinite(chargeTime))
            {
               lastChargeTime = chargeTime;
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
   feather.begin();
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
      Serial.println(" hz");

      SerialX::print("Rank", 8);
      SerialX::print("Resistor", 10);
      SerialX::print("Buffer", 8);
      SerialX::print("Discharge", 12);
      SerialX::println("StdDev %", 10);

      SerialX::print("----", 8);
      SerialX::print("--------", 10);
      SerialX::print("------", 8);
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

      const size_t MAX_RESULTS = RESISTOR_OPTION_COUNT * TEST_DISCHARGE_DELAY_COUNT * TARGET_EFFECTIVE_RATE_COUNT;
      TestRunResult allResults[MAX_RESULTS];
      size_t allResultCount = 0;

      // Print results header once
      SerialX::print("Resistor", 10);
      SerialX::print("Target", 10);
      SerialX::print("Buffer", 8);
      SerialX::print("Discharge", 12);
      SerialX::print("Avg", 12);
      SerialX::print("StdDev", 12);
      SerialX::print("StdDev", 10);
      SerialX::print("Sample", 10);
      SerialX::println("Effective", 11);

      SerialX::print("Value", 10);
      SerialX::print("Rate", 10);
      SerialX::print("Size", 8);
      SerialX::print("Delay", 12);
      SerialX::print("Charge", 12);
      SerialX::print("Charge", 12);
      SerialX::print("%", 10);
      SerialX::print("Rate", 10);
      SerialX::println("Rate", 11);

      SerialX::print("--------", 10);
      SerialX::print("--------", 10);
      SerialX::print("------", 8);
      SerialX::print("----------", 12);
      SerialX::print("----------", 12);
      SerialX::print("----------", 12);
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
            feather.println("    Resistor: ", resistor.label, Color::VALUE);
            feather.println("       Delay: ", testCase.dischargeDelayMicros, chargeFormat, Color::VALUE);
            feather.println("      Target: ", String((int)round(testCase.targetEffectiveRate)) + "/s", Color::VALUE);
            feather.println("        Test: ", String(currentTest) + "/" + String(totalTests), Color::VALUE);

            // Run the test
            TestRun testRun(capacitorSensor);
            TestRunResult result = testRun.run(testCase.targetEffectiveRate, testCase.dischargeDelayMicros);
            result.resistorLabel = resistor.label;
            allResults[allResultCount++] = result;

            // Print result immediately after test completes
            SerialX::print(resistor.label, 10);
            SerialX::print(String((int)round(result.targetEffectiveRate)) + " hz", 10);
            SerialX::print((unsigned long)result.bufferSize, 8);
            SerialX::print(String((unsigned long)result.dischargeDelayMicros) + " us", 12);
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
            SerialX::println(String((int)round(result.effectiveRateHz)) + "/s", 11);

            // Yield to allow display and other updates
            delay(10);
         }
      }

      printBestConfigurations(allResults, allResultCount);
      Serial.println();
      Serial.println("Testing Complete");

      // Apply the best configuration for the primary target rate
      const TestRunResult* best = findBestResult(allResults, allResultCount, 30.0f);
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
         capacitorSensor.setBufferSize(best->bufferSize);
      }
      else
      {
         capacitorSensor.setChargePin(RESISTOR_OPTIONS[0].chargePin);
      }
   }

   if (displayTimer.ready())
   {
      feather.setCursor(0, 0);
      feather.setTextSize(3);
      feather.println("Capacitor Tuning", Color::HEADING);

      feather.setTextSize(2);
      feather.println("       Resistor: ", resistorLabel(capacitorSensor.chargePin()), Color::VALUE);
      feather.println(" Discharge Time: ", capacitorSensor.dischargeDelayMicros(), chargeFormat, Color::VALUE);
      feather.println("    Buffer Size: ", (int)capacitorSensor.bufferSize(), Color::VALUE);

      float rawRate = capacitorSensor.rate();
      float effectiveRate = (capacitorSensor.bufferSize() > 0) ? (rawRate / (float)capacitorSensor.bufferSize()) : 0;

      feather.println("Avg Charge Time: ", capacitorSensor.chargeTimeMicros(), chargeFormat, Color::VALUE3);
      feather.println("    Sample Rate: ", (int)round(rawRate), sampleRateFormat, Color::VALUE2);
      feather.println(" Effective Rate: ", effectiveRate, effectiveRateFormat, Color::VALUE2);
   }
}

