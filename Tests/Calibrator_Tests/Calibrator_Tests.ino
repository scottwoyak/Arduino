#include <AUnit.h>
#include "Calibrator.h"

unsigned long ticks;

unsigned long getTicks()
{
   return ticks;
}

// Mock TimedStats::tickFunc for testing
void setupTest()
{
   ticks = 0;
   TimedStats::tickFunc = getTicks;
}

test(shouldInitializeWithCorrectNumberOfSensors)
{
   setupTest();
   Calibrator calibrator(3, 1000);

   assertEqual(3, calibrator.getNumSensors());
   assertEqual(0, calibrator.getSampleCount());
   assertTrue(isnan(calibrator.getBaseline()));
}

test(shouldStartWithZeroCorrections)
{
   setupTest();
   Calibrator calibrator(2, 1000);

   assertEqual(0.0f, calibrator.getCorrection(0));
   assertEqual(0.0f, calibrator.getCorrection(1));
}

test(shouldAddMeasurementsAndComputeAverageBaseline)
{
   setupTest();
   Calibrator calibrator(2, 1000);

   // Add measurements
   ticks = 100;
   calibrator.set(0, 100.0f);
   calibrator.set(1, 110.0f);

   ticks = 200;
   calibrator.set(0, 101.0f);
   calibrator.set(1, 109.0f);

   // With AVERAGE mode, baseline should be average of both sensors
   float baseline = calibrator.getBaseline();
   assertNear(105.0f, baseline, 0.1f);  // (100+110+101+109)/4 = 105
}

test(shouldComputeCorrectionFactors)
{
   setupTest();
   Calibrator calibrator(2, 1000);

   // Add measurements
   ticks = 100;
   calibrator.set(0, 100.0f);
   calibrator.set(1, 110.0f);

   ticks = 200;
   calibrator.set(0, 101.0f);
   calibrator.set(1, 109.0f);

   // Compute corrections
   float correction0 = calibrator.computeCorrection(0);
   float correction1 = calibrator.computeCorrection(1);

   // baseline = 105, so correction0 = 105 - 100.5 = ~4.5
   // and correction1 = 105 - 109.5 = ~-4.5
   assertNear(4.5f, correction0, 0.1f);
   assertNear(-4.5f, correction1, 0.1f);
}

test(shouldStoreCorrectionFactorsAfterCompute)
{
   setupTest();
   Calibrator calibrator(2, 1000);

   ticks = 100;
   calibrator.set(0, 100.0f);
   calibrator.set(1, 110.0f);

   // Before computeAllCorrections, stored corrections should be 0
   assertEqual(0.0f, calibrator.getCorrection(0));
   assertEqual(0.0f, calibrator.getCorrection(1));

   // Compute and store
   calibrator.computeAllCorrections();

   // Now stored corrections should match computed values
   float stored0 = calibrator.getCorrection(0);
   float stored1 = calibrator.getCorrection(1);

   assertTrue(stored0 > 0);
   assertTrue(stored1 < 0);
}

test(shouldRespectFirstSensorBaselineMode)
{
   setupTest();
   Calibrator calibrator(3, 1000, Calibrator::BaselineMode::FIRST_SENSOR);

   ticks = 100;
   calibrator.set(0, 100.0f);
   calibrator.set(1, 110.0f);
   calibrator.set(2, 120.0f);

   // With FIRST_SENSOR mode, baseline should be sensor 0's value
   float baseline = calibrator.getBaseline();
   assertNear(100.0f, baseline, 0.1f);
}

test(shouldRecordHistoricalSamples)
{
   setupTest();
   Calibrator calibrator(2, 1000);

   // Sample interval is 1000/10 = 100ms
   // Record first sample at t=100
   ticks = 100;
   calibrator.set(0, 100.0f);
   calibrator.set(1, 110.0f);
   assertEqual(1, calibrator.getSampleCount());

   // No new sample at t=150 (not enough time elapsed)
   ticks = 150;
   calibrator.set(0, 101.0f);
   calibrator.set(1, 109.0f);
   assertEqual(1, calibrator.getSampleCount());

   // New sample at t=200 (100ms elapsed)
   ticks = 200;
   calibrator.set(0, 102.0f);
   calibrator.set(1, 108.0f);
   assertEqual(2, calibrator.getSampleCount());
}

test(shouldComputeStatsFromHistoricalSamples)
{
   setupTest();
   Calibrator calibrator(2, 1000);

   // Add measurements at regular intervals
   ticks = 100;
   calibrator.set(0, 100.0f);
   calibrator.set(1, 110.0f);

   ticks = 200;
   calibrator.set(0, 102.0f);
   calibrator.set(1, 108.0f);

   ticks = 300;
   calibrator.set(0, 101.0f);
   calibrator.set(1, 109.0f);

   // Compute corrections
   calibrator.computeAllCorrections();

   // Get stats for sensor 0
   Calibrator::SensorStats stats0 = calibrator.getStats(0);

   assertEqual(3, stats0.sampleCount);
   assertTrue(stats0.minCorrected <= stats0.avgCorrected);
   assertTrue(stats0.avgCorrected <= stats0.maxCorrected);
   assertNear(stats0.rangeCorrected, stats0.maxCorrected - stats0.minCorrected, 0.0001f);
}

test(shouldResetAllData)
{
   setupTest();
   Calibrator calibrator(2, 1000);

   ticks = 100;
   calibrator.set(0, 100.0f);
   calibrator.set(1, 110.0f);

   ticks = 200;
   calibrator.set(0, 101.0f);
   calibrator.set(1, 109.0f);

   calibrator.computeAllCorrections();

   // Verify data exists
   assertEqual(2, calibrator.getSampleCount());
   assertTrue(calibrator.getCorrection(0) != 0);

   // Reset
   calibrator.reset();

   // Verify data is cleared
   assertEqual(0, calibrator.getSampleCount());
   assertEqual(0.0f, calibrator.getCorrection(0));
   assertEqual(0.0f, calibrator.getCorrection(1));
   assertTrue(isnan(calibrator.getBaseline()));
}

test(shouldIgnoreNaNValues)
{
   setupTest();
   Calibrator calibrator(2, 1000);

   ticks = 100;
   calibrator.set(0, 100.0f);
   calibrator.set(1, NAN);  // Invalid value

   ticks = 200;
   calibrator.set(0, 101.0f);
   calibrator.set(1, 109.0f);

   // Should only have valid samples
   Calibrator::SensorStats stats1 = calibrator.getStats(1);
   assertEqual(1, stats1.sampleCount);  // Only one valid sample
}

test(shouldReturnNaNForInvalidSensorIndex)
{
   setupTest();
   Calibrator calibrator(2, 1000);

   ticks = 100;
   calibrator.set(0, 100.0f);
   calibrator.set(1, 110.0f);

   assertTrue(isnan(calibrator.getMeasurement(2)));  // Invalid index
   assertTrue(isnan(calibrator.computeCorrection(2)));  // Invalid index
}

test(shouldStoreUpTo100Samples)
{
   setupTest();
   Calibrator calibrator(1, 10000);

   // Sample interval is 10000/10 = 1000ms
   // Add samples every 1000ms for 101 iterations
   for (int i = 0; i <= 100; i++)
   {
      ticks = i * 1000;
      calibrator.set(0, 100.0f + i);
   }

   // Should have at most 100 samples
   assertTrue(calibrator.getSampleCount() <= 100);
   assertEqual(100, calibrator.getSampleCount());
}

test(shouldComputeCorrectStatsWithCorrections)
{
   setupTest();
   Calibrator calibrator(2, 1000);

   // Create a scenario where sensors have offset
   ticks = 100;
   calibrator.set(0, 100.0f);
   calibrator.set(1, 110.0f);  // Sensor 1 always reads 10 higher

   ticks = 200;
   calibrator.set(0, 100.0f);
   calibrator.set(1, 110.0f);

   // Baseline should be 105, so:
   // correction[0] = 105 - 100 = 5
   // correction[1] = 105 - 110 = -5
   calibrator.computeAllCorrections();

   // After applying corrections, both sensors should read ~105
   Calibrator::SensorStats stats0 = calibrator.getStats(0);
   Calibrator::SensorStats stats1 = calibrator.getStats(1);

   assertNear(105.0f, stats0.avgCorrected, 0.1f);
   assertNear(105.0f, stats1.avgCorrected, 0.1f);
}

void setup()
{
   Serial.begin(115200);
   delay(1000);
}

void loop()
{
   aunit::TestRunner::run();
}
