#pragma once

#include <AUnit.h>
#include "TempCalibrator.h"

namespace TempCalibratorTests
{
unsigned long calibratorTestTicks;

unsigned long getCalibratorTestTicks()
{
   return calibratorTestTicks;
}

using TempCalibratorMock = CalibratorBase<getCalibratorTestTicks>;
using TimedStatsMock = TimedStatsBase<getCalibratorTestTicks>;

void setupTempCalibratorTest()
{
   calibratorTestTicks = 0;
}

test(TempCalibratorTest, shouldInitializeWithCorrectNumberOfSensors)
{
   setupTempCalibratorTest();
   TempCalibratorMock calibrator(3, 1000);

   assertEqual(3, calibrator.getNumSensors());
   assertTrue(isnan(calibrator.getBaseline()));
}

test(TempCalibratorTest, shouldStartWithZeroCorrections)
{
   setupTempCalibratorTest();
   TempCalibratorMock calibrator(2, 1000);

   assertEqual(0.0f, calibrator.getCorrection(0));
   assertEqual(0.0f, calibrator.getCorrection(1));
}

test(TempCalibratorTest, shouldAddMeasurementsAndComputeAverageBaseline)
{
   setupTempCalibratorTest();
   TempCalibratorMock calibrator(2, 1000);

   // Add measurements
   calibratorTestTicks = 100;
   calibrator.set(0, 100.0f);
   calibrator.set(1, 110.0f);

   calibratorTestTicks = 200;
   calibrator.set(0, 101.0f);
   calibrator.set(1, 109.0f);

   // With AVERAGE mode, baseline should be average of both sensors
   float baseline = calibrator.getBaseline();
   assertEqual(105.0f, baseline);  // (100+110+101+109)/4 = 105
}

test(TempCalibratorTest, shouldComputeCorrectionFactors)
{
   setupTempCalibratorTest();
   TempCalibratorMock calibrator(2, 1000);

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

   // baseline = 105, sensor averages are 100.5 and 109.5, so corrections are +/- 4.5
   assertEqual(4.5f, correction0);
   assertEqual(-4.5f, correction1);
}

test(TempCalibratorTest, shouldStoreCorrectionFactorsAfterCompute)
{
   setupTempCalibratorTest();
   TempCalibratorMock calibrator(2, 1000);

   calibratorTestTicks = 100;
   calibrator.set(0, 100.0f);
   calibrator.set(1, 110.0f);

   // Corrections are computed as measurements are added
   float stored0 = calibrator.getCorrection(0);
   float stored1 = calibrator.getCorrection(1);

   assertEqual(5.0f, stored0);
   assertEqual(-5.0f, stored1);
}

test(TempCalibratorTest, shouldRespectFirstSensorBaselineMode)
{
   setupTempCalibratorTest();
   TempCalibratorMock calibrator(3, 1000, TempCalibratorMock::BaselineMode::FIRST_SENSOR);

   calibratorTestTicks = 100;
   calibrator.set(0, 100.0f);
   calibrator.set(1, 110.0f);
   calibrator.set(2, 120.0f);

   // With FIRST_SENSOR mode, baseline should be sensor 0's value
   float baseline = calibrator.getBaseline();
   assertEqual(100.0f, baseline);
}

test(TempCalibratorTest, shouldComputeStatsFromHistoricalSamples)
{
   setupTempCalibratorTest();
   TempCalibratorMock calibrator(2, 1000);

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
   TimedStatsMock* stats0 = calibrator.getStats(0);

   assertTrue(stats0 != nullptr);
   float min0 = stats0->min();
   float avg0 = stats0->average();
   float max0 = stats0->max();
   assertEqual(100.0f, min0);
   assertEqual(101.0f, avg0);
   assertEqual(102.0f, max0);
   assertEqual(2.0f, stats0->range());
}

test(TempCalibratorTest, shouldResetAllData)
{
   setupTempCalibratorTest();
   TempCalibratorMock calibrator(2, 1000);

   calibratorTestTicks = 100;
   calibrator.set(0, 100.0f);
   calibrator.set(1, 110.0f);

   calibratorTestTicks = 200;
   calibrator.set(0, 101.0f);
   calibrator.set(1, 109.0f);

   // Verify data exists
   TimedStatsMock* stats0 = calibrator.getStats(0);
   assertTrue(stats0 != nullptr);
   assertEqual(static_cast<size_t>(2), stats0->count());
   assertEqual(4.5f, calibrator.getCorrection(0));

   // Reset
   calibrator.reset();

   // Verify data is cleared
   assertEqual(static_cast<size_t>(0), stats0->count());
   assertEqual(0.0f, calibrator.getCorrection(0));
   assertEqual(0.0f, calibrator.getCorrection(1));
   assertTrue(isnan(calibrator.getBaseline()));
}

test(TempCalibratorTest, TempCalibrator_shouldIgnoreNaNValues)
{
   setupTempCalibratorTest();
   TempCalibratorMock calibrator(2, 1000);

   calibratorTestTicks = 100;
   calibrator.set(0, 100.0f);
   calibrator.set(1, NAN);  // Invalid value

   calibratorTestTicks = 200;
   calibrator.set(0, 101.0f);
   calibrator.set(1, 109.0f);

   // Should only have valid samples
   TimedStatsMock* stats1 = calibrator.getStats(1);
   assertTrue(stats1 != nullptr);
   assertEqual(1, static_cast<int>(stats1->count()));  // Only one valid sample
}

test(TempCalibratorTest, shouldReturnNaNForInvalidSensorIndex)
{
   setupTempCalibratorTest();
   TempCalibratorMock calibrator(2, 1000);

   calibratorTestTicks = 100;
   calibrator.set(0, 100.0f);
   calibrator.set(1, 110.0f);

   assertTrue(isnan(calibrator.getAverage(2)));  // Invalid index
   assertEqual(0.0f, calibrator.getCorrection(2));  // Invalid index
}

test(TempCalibratorTest, shouldComputeCorrectStatsWithCorrections)
{
   setupTempCalibratorTest();
   TempCalibratorMock calibrator(2, 1000);

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

   // After applying corrections, both sensors should read exactly 105.
   TimedStatsMock* stats0 = calibrator.getStats(0);
   TimedStatsMock* stats1 = calibrator.getStats(1);

   assertTrue(stats0 != nullptr);
   assertTrue(stats1 != nullptr);
   assertEqual(105.0f, stats0->average() + calibrator.getCorrection(0));
   assertEqual(105.0f, stats1->average() + calibrator.getCorrection(1));
}

} // namespace TempCalibratorTests
