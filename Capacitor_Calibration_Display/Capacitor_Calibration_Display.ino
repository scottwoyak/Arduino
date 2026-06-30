//
// Capacitor sensor tuner: displays real-time measurements and runs calibration tests on Button A.
//
// Monitors capacitor charge time and raw sampling rate in real-time. Button A executes a full
// calibration sweep across different discharge delays and target effective rates, outputting
// results to serial for analysis.
//
#include <Arduino.h>
#include "CapacitorSensor.h"
#include "Feather.h"
#include "RollingStats.h"
#include "SerialX.h"
#include "Timer.h"

Feather feather;
Format chargeFormat("####.# us");
Format effectiveRateFormat("###/s");
Format rawRateFormat("######/s");

constexpr size_t DISPLAY_INTERVAL_MS = 100;

constexpr size_t DEFAULT_ROLLING_BUFFER_SIZE = 300;
constexpr uint16_t DEFAULT_DISCHARGE_DELAY_MICROS = 200;

// ----------- Test Sweep Parameters
constexpr unsigned long TEST_DURATION_MS = 2000;
constexpr float TARGET_EFFECTIVE_RATES[] = { 10.0f, 20.0f, 30.0f };
constexpr size_t TARGET_EFFECTIVE_RATE_COUNT = sizeof(TARGET_EFFECTIVE_RATES) / sizeof(TARGET_EFFECTIVE_RATES[0]);

constexpr uint16_t TEST_DISCHARGE_DELAYS[] = { 100, 200, 500, 1000 };
constexpr size_t TEST_DISCHARGE_DELAY_COUNT = sizeof(TEST_DISCHARGE_DELAYS) / sizeof(TEST_DISCHARGE_DELAYS[0]);

constexpr size_t RAW_RATE_SAMPLE_COUNT = 100;
constexpr size_t MIN_TARGET_BUFFER_SIZE = 1;
constexpr size_t MAX_TARGET_BUFFER_SIZE = 500;

Timer displayTimer(DISPLAY_INTERVAL_MS);

// ----------- Hardware Pin Assignments
constexpr uint8_t SENSE_PIN = 5;
constexpr uint8_t CHARGE_PIN_1M = 6;
constexpr uint8_t CHARGE_PIN_470K = 9;
constexpr uint8_t CHARGE_PIN_100K = 10;
constexpr uint8_t CHARGE_PIN_47K = 11;

constexpr uint8_t CHARGE_PIN = CHARGE_PIN_1M;  // Selected charge pin for this build

CapacitorSensor capacitorSensor(CHARGE_PIN, SENSE_PIN, DEFAULT_DISCHARGE_DELAY_MICROS, DEFAULT_ROLLING_BUFFER_SIZE);

/// <summary>Total number of test cases (delay count × rate count).</summary>
const size_t TOTAL_TESTS = TEST_DISCHARGE_DELAY_COUNT * TARGET_EFFECTIVE_RATE_COUNT;

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
   /// <summary>Target effective rate for this test.</summary>
   float targetEffectiveRate = NAN;
   /// <summary>Discharge delay used for this test.</summary>
   uint16_t dischargeDelayMicros = 0;
   /// <summary>Average charge time measured during test.</summary>
   float averageChargeTimeMicros = NAN;
   /// <summary>Min-to-max variation in charge time during test.</summary>
   float chargeTimeVariationMicros = NAN;
   /// <summary>Buffer size used for this test.</summary>
   size_t bufferSize = 0;
   /// <summary>Raw sensor rate in Hz during this test.</summary>
   float rawRateHz = NAN;
   /// <summary>Effective output rate in Hz during this test.</summary>
   float effectiveRateHz = NAN;
};

/// <summary>Executes a single test run on a capacitor sensor.</summary>
class TestRun
{
private:
   CapacitorSensor& _sensor;
   unsigned long _testStartMs = 0;

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
   /// <param name="testDurationMs">Duration of the measurement phase in milliseconds</param>
   /// <returns>Test result data structure</returns>
   TestRunResult run(float targetEffectiveRate, uint16_t dischargeDelayMicros, unsigned long testDurationMs)
   {
      TestRunResult result;
      result.targetEffectiveRate = targetEffectiveRate;
      result.dischargeDelayMicros = dischargeDelayMicros;

      // Phase 1: Collect raw rate samples
      _sensor.setDischargeDelayMicros(dischargeDelayMicros);
      _testStartMs = millis();

      // Collect RAW_RATE_SAMPLE_COUNT samples to estimate raw sensor rate
      size_t sampleCount = 0;
      while (sampleCount < RAW_RATE_SAMPLE_COUNT)
      {
         if (isfinite(_sensor.chargeTimeMicros()))
         {
            sampleCount++;
         }
         delay(1);
      }

      float rawRate = _sensor.rate();
      result.rawRateHz = rawRate;

      // Calculate target buffer size based on raw rate and target effective rate
      size_t targetBufferSize = _calculateTargetBufferSize(rawRate, targetEffectiveRate);
      result.bufferSize = targetBufferSize;

      // Phase 2: Measure with target buffer size
      RollingStats stats(targetBufferSize);
      _testStartMs = millis();
      float measuredAverageMin = NAN;
      float measuredAverageMax = NAN;

      // Collect measurements for testDurationMs
      while (millis() - _testStartMs < testDurationMs)
      {
         float chargeTime = _sensor.chargeTimeMicros();
         if (isfinite(chargeTime))
         {
            stats.set(chargeTime);

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
         delay(1);
      }

      // Compile results
      result.averageChargeTimeMicros = stats.average();
      if (isfinite(measuredAverageMin) && isfinite(measuredAverageMax))
      {
         result.chargeTimeVariationMicros = measuredAverageMax - measuredAverageMin;
      }

      float finalRawRate = _sensor.rate();
      result.effectiveRateHz = (result.bufferSize > 0) ? (finalRawRate / result.bufferSize) : 0;

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
         return DEFAULT_ROLLING_BUFFER_SIZE;
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
   capacitorSensor.start();

   size_t caseIndex = 0;
   for (size_t rateIndex = 0; rateIndex < TARGET_EFFECTIVE_RATE_COUNT; rateIndex++)
   {
      float targetRate = TARGET_EFFECTIVE_RATES[rateIndex];

      for (size_t delayIndex = 0; delayIndex < TEST_DISCHARGE_DELAY_COUNT; delayIndex++)
      {
         uint16_t delayMicros = TEST_DISCHARGE_DELAYS[delayIndex];

         // Create test case parameter (no execution, just storage)
         testParameters.cases[caseIndex].targetEffectiveRate = targetRate;
         testParameters.cases[caseIndex].dischargeDelayMicros = delayMicros;
         caseIndex++;
      }
   }

   testParameters.count = caseIndex;
}

void loop()
{
   // Button A pressed: execute all tests
   if (feather.buttonA.wasPressed())
   {
      Serial.println();
      Serial.println("Starting tests...");
      Serial.println();

      // Print results header once
      SerialX::print("Target", 10);
      SerialX::print("Discharge", 12);
      SerialX::print("Avg", 12);
      SerialX::print("Variation", 12);
      SerialX::print("Buffer", 8);
      SerialX::print("Raw", 10);
      SerialX::println("Effective", 11);

      SerialX::print("Rate", 10);
      SerialX::print("Delay", 12);
      SerialX::print("Charge", 12);
      SerialX::print("Charge", 12);
      SerialX::print("Size", 8);
      SerialX::print("Rate", 10);
      SerialX::println("Rate", 11);

      SerialX::print("--------", 10);
      SerialX::print("----------", 12);
      SerialX::print("----------", 12);
      SerialX::print("----------", 12);
      SerialX::print("------", 8);
      SerialX::print("--------", 10);
      SerialX::println("---------", 11);

      // Execute all test cases
      for (size_t i = 0; i < testParameters.count; i++)
      {
         const TestCase& testCase = testParameters.cases[i];

         // Run the test
         TestRun testRun(capacitorSensor);
         TestRunResult result = testRun.run(testCase.targetEffectiveRate, testCase.dischargeDelayMicros, TEST_DURATION_MS);

         // Print result immediately after test completes
         SerialX::print(String((int)round(result.targetEffectiveRate)) + " hz", 10);
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
         SerialX::print((unsigned long)result.bufferSize, 8);
         SerialX::print(String((int)round(result.rawRateHz)) + "/s", 10);
         SerialX::println(String((int)round(result.effectiveRateHz)) + "/s", 11);

         // Yield to allow display and other updates
         delay(10);
      }

      feather.clearDisplay();

      Serial.println();
      Serial.println("Testing Complete");
   }

   if (displayTimer.ready())
   {
      feather.setCursor(0, 0);
      feather.setTextSize(2);
      feather.println("Capacitor Sensor Tuner", Color::HEADING);

      feather.setTextSize(2);

      float rawRate = capacitorSensor.rate();
      float effectiveRate = (capacitorSensor.averageMicros() > 0) ? (rawRate / capacitorSensor.averageMicros()) : 0;

      feather.println(" Discharge Time: ", capacitorSensor.dischargeDelayMicros(), chargeFormat, Color::VALUE2);
      feather.println("       Avg Rate: ", (int)round(rawRate), rawRateFormat, Color::VALUE2);

      feather.println("Avg Charge Time: ", capacitorSensor.averageMicros(), chargeFormat);
      feather.println(" Effective Rate: ", effectiveRate, effectiveRateFormat, Color::VALUE3);
   }
}

