#pragma once

#include <AUnit.h>
#include "Calibrator.h"

unsigned long calibratorTestTicks;

unsigned long getCalibratorTestTicks()
{
   return calibratorTestTicks;
}

// Mock TimedStats::tickFunc for testing
void setupCalibratorTest()
{
   calibratorTestTicks = 0;
   TimedStats::tickFunc = getCalibratorTestTicks;
}

test(shouldInitializeWithCorrectNumberOfSensors)
{
   setupCalibratorTest();
   Calibrator calibrator(3, 1000);

   assertEqual(3, calibrator.getNumSensors());
   assertTrue(isnan(calibrator.getBaseline()));
}

test(shouldStartWithZeroCorrections)
{
   setupCalibratorTest();
   Calibrator calibrator(2, 1000);

   assertEqual(0.0f, calibrator.getCorrection(0));
   assertEqual(0.0f, calibrator.getCorrection(1));
}

test(shouldAddMeasurementsAndComputeAverageBaseline)
{
   setupCalibratorTest();
   Calibrator calibrator(2, 1000);

   // Add measurements
   calibratorTestTicks = 100;
   calibrator.set(0, 100.0f);
   calibrator.set(1, 110.0f);

   calibratorTestTicks = 200;
   calibrator.set(0, 101.0f);
   calibrator.set(1, 109.0f);

   // With AVERAGE mode, baseline should be average of both sensors
   float baseline = calibrator.getBaseline();
   assertNear(105.0f, baseline, 0.1f);  // (100+110+101+109)/4 = 105
}

test(shouldComputeCorrectionFactors)
{
   setupCalibratorTest();
   Calibrator calibrator(2, 1000);

   // Add measurements
   calibratorTestTicks = 100;
   calibrator.set(0, 100.0f);
   calibrator.set(1, 110.0f);

   calibratorTestTicks = 200;
   calibrator.set(0, 101.0f);
   calibrator.set(1, 109.0f);

   // Read corrections
   float correction0 = calibrator.getCorrection(0);
   float correction1 = calibrator.getCorrection(1);

   // baseline = 105, so correction0 = 105 - 100.5 = ~4.5
   // and correction1 = 105 - 109.5 = ~-4.5
   assertNear(4.5f, correction0, 0.1f);
   assertNear(-4.5f, correction1, 0.1f);
}

test(shouldStoreCorrectionFactorsAfterCompute)
{
   setupCalibratorTest();
   Calibrator calibrator(2, 1000);

   calibratorTestTicks = 100;
   calibrator.set(0, 100.0f);
   calibrator.set(1, 110.0f);

   // Corrections are computed as measurements are added
   float stored0 = calibrator.getCorrection(0);
   float stored1 = calibrator.getCorrection(1);

   assertTrue(stored0 > 0);
   assertTrue(stored1 < 0);
}

test(shouldRespectFirstSensorBaselineMode)
{
   setupCalibratorTest();
   Calibrator calibrator(3, 1000, Calibrator::BaselineMode::FIRST_SENSOR);

   calibratorTestTicks = 100;
   calibrator.set(0, 100.0f);
   calibrator.set(1, 110.0f);
   calibrator.set(2, 120.0f);

   // With FIRST_SENSOR mode, baseline should be sensor 0's value
   float baseline = calibrator.getBaseline();
   assertNear(100.0f, baseline, 0.1f);
}

test(shouldComputeStatsFromHistoricalSamples)
{
   setupCalibratorTest();
   Calibrator calibrator(2, 1000);

   // Add measurements at regular intervals
   calibratorTestTicks = 100;
   calibrator.set(0, 100.0f);
   calibrator.set(1, 110.0f);

   calibratorTestTicks = 200;
   calibrator.set(0, 102.0f);
   calibrator.set(1, 108.0f);

   calibratorTestTicks = 300;
   calibrator.set(0, 101.0f);
   calibrator.set(1, 109.0f);

   // Get timed stats for sensor 0
   TimedStats* stats0 = calibrator.getStats(0);

   assertTrue(stats0 != nullptr);
   float min0 = stats0->min();
   float avg0 = stats0->average();
   float max0 = stats0->max();
   assertTrue(min0 <= avg0);
   assertTrue(avg0 <= max0);
   assertNear(stats0->range(), max0 - min0, 0.0001f);
}

test(shouldResetAllData)
{
   setupCalibratorTest();
   Calibrator calibrator(2, 1000);

   calibratorTestTicks = 100;
   calibrator.set(0, 100.0f);
   calibrator.set(1, 110.0f);

   calibratorTestTicks = 200;
   calibrator.set(0, 101.0f);
   calibrator.set(1, 109.0f);

   // Verify data exists
   TimedStats* stats0 = calibrator.getStats(0);
   assertTrue(stats0 != nullptr);
   assertTrue(stats0->count() > 0);
   assertTrue(calibrator.getCorrection(0) != 0);

   // Reset
   calibrator.reset();

   // Verify data is cleared
   assertEqual(static_cast<size_t>(0), stats0->count());
   assertEqual(0.0f, calibrator.getCorrection(0));
   assertEqual(0.0f, calibrator.getCorrection(1));
   assertTrue(isnan(calibrator.getBaseline()));
}

test(Calibrator_shouldIgnoreNaNValues)
{
   setupCalibratorTest();
   Calibrator calibrator(2, 1000);

   calibratorTestTicks = 100;
   calibrator.set(0, 100.0f);
   calibrator.set(1, NAN);  // Invalid value

   calibratorTestTicks = 200;
   calibrator.set(0, 101.0f);
   calibrator.set(1, 109.0f);

   // Should only have valid samples
   TimedStats* stats1 = calibrator.getStats(1);
   assertTrue(stats1 != nullptr);
   assertEqual(1, static_cast<int>(stats1->count()));  // Only one valid sample
}

test(shouldReturnNaNForInvalidSensorIndex)
{
   setupCalibratorTest();
   Calibrator calibrator(2, 1000);

   calibratorTestTicks = 100;
   calibrator.set(0, 100.0f);
   calibrator.set(1, 110.0f);

   assertTrue(isnan(calibrator.getAverage(2)));  // Invalid index
   assertEqual(0.0f, calibrator.getCorrection(2));  // Invalid index
}

test(shouldComputeCorrectStatsWithCorrections)
{
   setupCalibratorTest();
   Calibrator calibrator(2, 1000);

   // Create a scenario where sensors have offset
   calibratorTestTicks = 100;
   calibrator.set(0, 100.0f);
   calibrator.set(1, 110.0f);  // Sensor 1 always reads 10 higher

   calibratorTestTicks = 200;
   calibrator.set(0, 100.0f);
   calibrator.set(1, 110.0f);

   // Baseline should be 105, so:
   // correction[0] = 105 - 100 = 5
   // correction[1] = 105 - 110 = -5

   // After applying corrections, both sensors should read ~105
   TimedStats* stats0 = calibrator.getStats(0);
   TimedStats* stats1 = calibrator.getStats(1);

   assertTrue(stats0 != nullptr);
   assertTrue(stats1 != nullptr);
   assertNear(105.0f, stats0->average() + calibrator.getCorrection(0), 0.1f);
   assertNear(105.0f, stats1->average() + calibrator.getCorrection(1), 0.1f);
}
