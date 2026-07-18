       #pragma once

#include <AUnit.h>
#include "TimedAverage.h"

namespace TimedAverageTests
{

unsigned long timedAverageTestTicks;

unsigned long getTimedAverageTestTicks()
{
   return timedAverageTestTicks;
}

using TimedAverageMock = TimedAverageBase<getTimedAverageTestTicks>;

void setupTimedAverageTest()
{
   timedAverageTestTicks = 0;
}

test(TimedAverageTest, shouldStartEmpty)
{
   setupTimedAverageTest();
   TimedAverageMock average(1000);

   assertTrue(isnan(average.average()));
   assertEqual((size_t)0, average.count());
}

test(TimedAverageTest, shouldTrackAverageWithinWindow)
{
   setupTimedAverageTest();
   TimedAverageMock average(1000);

   timedAverageTestTicks = 100;
   average.set(1.0f);
   timedAverageTestTicks = 200;
   average.set(2.0f);
   timedAverageTestTicks = 300;
   average.set(3.0f);

   assertEqual(2.0f, average.average());
   assertEqual((size_t)3, average.count());
}

test(TimedAverageTest, shouldExpireOldSamplesByTime)
{
   setupTimedAverageTest();
   TimedAverageMock average(1000);

   timedAverageTestTicks = 0;
   average.set(1.0f);
   timedAverageTestTicks = 500;
   average.set(2.0f);
   timedAverageTestTicks = 1000;
   average.set(3.0f);

   timedAverageTestTicks = 1400;

   assertEqual(2.5f, average.average());
   assertEqual((size_t)2, average.count());
}

test(TimedAverageTest, shouldIgnoreNonFiniteValues)
{
   setupTimedAverageTest();
   TimedAverageMock average(1000);

   timedAverageTestTicks = 100;
   average.set(1.0f);
   timedAverageTestTicks = 200;
   average.set(INFINITY);
   timedAverageTestTicks = 300;
   average.set(NAN);

   assertEqual(1.0f, average.average());
   assertEqual((size_t)1, average.count());
}

test(TimedAverageTest, shouldAccuratelyAverageDataLessThanWindow)
{
   setupTimedAverageTest();
   // 2000ms window, but we'll only have data for first 500ms
   TimedAverageMock average(2000);

   timedAverageTestTicks = 0;
   average.set(10.0f);
   timedAverageTestTicks = 250;
   average.set(20.0f);
   timedAverageTestTicks = 500;
   average.set(30.0f);

   // At 500ms with values 10, 20, 30, the average should be 20, not something lower
   // The average should reflect the actual data present, not be suppressed by the unfilled window
   assertEqual(20.0f, average.average());
   assertEqual((size_t)3, average.count());
}

test(TimedAverageTest, shouldAccuratelyAverageConstantValuesLessThanWindow)
{
   setupTimedAverageTest();
   // 2000ms window with constant values for first second
   TimedAverageMock average(2000);

   for (int i = 0; i < 10; i++)
   {
      timedAverageTestTicks = i * 100;
      average.set(5.0f);
   }
   // timedAverageTestTicks is now 900ms

   // Should be exactly 5.0, not something lower
   assertEqual(5.0f, average.average());
   assertEqual((size_t)10, average.count());
}

test(TimedAverageTest, shouldReturnNanAndZeroCountWhenOnlyNonFiniteValuesSet)
{
   setupTimedAverageTest();
   TimedAverageMock average(1000);

   timedAverageTestTicks = 100;
   average.set(INFINITY);
   timedAverageTestTicks = 200;
   average.set(-INFINITY);
   timedAverageTestTicks = 300;
   average.set(NAN);

   assertTrue(isnan(average.average()));
   assertEqual((size_t)0, average.count());
}

test(TimedAverageTest, shouldHandleSingleValueInWindow)
{
   setupTimedAverageTest();
   TimedAverageMock average(1000);

   timedAverageTestTicks = 50;
   average.set(7.0f);

   assertEqual(7.0f, average.average());
   assertEqual((size_t)1, average.count());
}

test(TimedAverageTest, shouldHoldExactValueWhenFullWindowIsConstant)
{
   setupTimedAverageTest();
   TimedAverageMock average(1000, 10);

   // Fill well past the window with a constant value so every bucket (including the
   // blending bucket) has data, then verify the reconstructed average is exact.
   for (unsigned long t = 0; t <= 2500; t += 50)
   {
      timedAverageTestTicks = t;
      average.set(3.0f);
   }

   assertTrue(average.isFull());
   assertEqual(3.0f, average.average());
}

test(TimedAverageTest, shouldReconstructExactMidpointValueForLinearTrendOverFullWindow)
{
   setupTimedAverageTest();
   // A perfectly linear trend, sampled densely, should average to the value at the
   // window's temporal midpoint once the window is completely full - this is the
   // property the trapezoidal/slope-extrapolated reconstruction is designed to preserve
   // (a flat rectangular bucket-weighting scheme would be biased away from this).
   TimedAverageMock average(1000, 20);

   for (unsigned long t = 0; t <= 3000; t += 10)
   {
      timedAverageTestTicks = t;
      average.set(static_cast<float>(t));
   }

   assertTrue(average.isFull());
   // Window covers [now-1000, now]; midpoint value is now - 500 = 2500.
   assertNear(2500.0f, average.average(), 5.0f);
}

test(TimedAverageTest, shouldExpireAllSamplesWhenGapExceedsDuration)
{
   setupTimedAverageTest();
   TimedAverageMock average(1000);

   timedAverageTestTicks = 0;
   average.set(1.0f);
   timedAverageTestTicks = 100;
   average.set(2.0f);

   // Jump far beyond the window in one step; every bucket should be cleared.
   timedAverageTestTicks = 100000;

   assertTrue(isnan(average.average()));
   assertEqual((size_t)0, average.count());
}

test(TimedAverageTest, shouldExpireSampleExactlyAtBucketBoundary)
{
   setupTimedAverageTest();
   // 1000ms window with a single bucket (plus the blending bucket) => bucketMs = 1000,
   // and 2 total buckets, so a sample fully expires once both buckets have rotated past it.
   TimedAverageMock average(1000, 1);

   timedAverageTestTicks = 0;
   average.set(1.0f);

   // Advance to exactly the point where both buckets have rotated once each; the sample
   // should still be considered current since _advance() only rotates once elapsed
   // strictly exceeds the bucket width.
   timedAverageTestTicks = 2000;
   assertEqual((size_t)1, average.count());

   // Advance just past that point; the bucket holding the sample should now have rotated
   // away and been cleared.
   timedAverageTestTicks = 2001;
   assertEqual((size_t)0, average.count());
}

test(TimedAverageTest, shouldSupportSingleBucketConfiguration)
{
   setupTimedAverageTest();
   TimedAverageMock average(1000, 1);

   timedAverageTestTicks = 0;
   average.set(2.0f);
   timedAverageTestTicks = 100;
   average.set(4.0f);

   assertEqual((size_t)2, average.count());
   assertEqual(3.0f, average.average());
}

test(TimedAverageTest, shouldSupportManyBucketsWithoutOverflowingInternalScratchLimits)
{
   setupTimedAverageTest();
   // Historically the reconstruction used a small fixed-size local buffer; verify a bucket
   // count well beyond any such limit still works correctly once fully populated.
   TimedAverageMock average(1000, 200);

   for (unsigned long t = 0; t <= 3000; t += 5)
   {
      timedAverageTestTicks = t;
      average.set(1.0f);
   }

   assertTrue(average.isFull());
   assertEqual(1.0f, average.average());
}

test(TimedAverageTest, shouldHandleValuesClusteredAtVeryStartAndEndOfWindow)
{
   setupTimedAverageTest();
   TimedAverageMock average(1000, 10);

   // One value right as the window opens, one right as it's about to close, nothing in
   // between, exercising the edge-extrapolation logic with minimal interior data.
   timedAverageTestTicks = 0;
   average.set(0.0f);
   timedAverageTestTicks = 999;
   average.set(100.0f);

   assertEqual((size_t)2, average.count());
   assertFalse(isnan(average.average()));
}

test(TimedAverageTest, shouldTreatMultipleSetsInSameBucketAsTheirMean)
{
   setupTimedAverageTest();
   TimedAverageMock average(1000, 10);

   timedAverageTestTicks = 0;
   average.set(0.0f);
   average.set(10.0f);
   average.set(20.0f);

   // All three sets land in the same (first) bucket; average() during startup should be
   // the simple arithmetic mean of the raw values.
   assertEqual((size_t)3, average.count());
   assertEqual(10.0f, average.average());
}

test(TimedAverageTest, shouldNotChangeStateWhenAverageIsCalledRepeatedlyWithoutNewData)
{
   setupTimedAverageTest();
   TimedAverageMock average(1000);

   timedAverageTestTicks = 100;
   average.set(5.0f);
   timedAverageTestTicks = 200;
   average.set(15.0f);

   float first = average.average();
   float second = average.average();
   float third = average.average();

   assertEqual(first, second);
   assertEqual(second, third);
   assertEqual((size_t)2, average.count());
}

test(TimedAverageTest, shouldHandleZeroAndNegativeValues)
{
   setupTimedAverageTest();
   TimedAverageMock average(1000);

   timedAverageTestTicks = 0;
   average.set(-10.0f);
   timedAverageTestTicks = 100;
   average.set(0.0f);
   timedAverageTestTicks = 200;
   average.set(10.0f);

   assertEqual((size_t)3, average.count());
   assertEqual(0.0f, average.average());
}

test(TimedAverageTest, shouldClampNumBucketsParameterToAtLeastOne)
{
   setupTimedAverageTest();
   TimedAverageMock average(1000, 0);

   // nBuckets=0 should be normalized to 1 (plus the blending bucket), never zero/negative.
   assertMore(average.numBuckets(), (uint)0);
}

// Real (timestampMs, rawValue) pairs captured from a device's 5-minute averaging field via
// serial "5mDebug" logging. This replay previously exposed a sawtooth discontinuity of ~0.3
// degrees at every bucket-rotation boundary under the old trapezoidal/edge-extrapolated
// reconstruction; the continuous partial-overlap weighting scheme keeps consecutive
// reconstructed averages within normal sample-to-sample variation instead.
struct TimedAverageSample
{
   unsigned long timeMs;
   float rawValue;
};

const TimedAverageSample REAL_5MIN_DATASET[] = {
   { 12005, 54.9906f }, { 12151, 54.9666f }, { 12591, 54.9666f }, { 13034, 54.9474f },
   { 13579, 54.9474f }, { 14021, 54.9522f }, { 14565, 54.9474f }, { 15769, 54.9474f },
   { 16015, 55.0050f }, { 16553, 54.9954f }, { 17093, 54.9858f }, { 17538, 54.9906f },
   { 18077, 54.9618f }, { 18523, 54.9666f }, { 19070, 54.9522f }, { 19518, 55.0291f },
   { 20068, 54.9714f }, { 20512, 55.0146f }, { 21053, 55.0002f }, { 21597, 54.9954f },
   { 22040, 54.9714f }, { 22582, 54.9858f }, { 23026, 54.9954f }, { 23573, 55.0050f },
   { 24018, 54.9762f }, { 24564, 54.9810f }, { 25014, 54.9762f }, { 25556, 55.0050f },
   { 26111, 54.9762f }, { 26561, 54.9858f }, { 27111, 55.0050f }, { 27561, 54.9810f },
   { 28106, 55.0195f }, { 28555, 55.0339f }, { 29104, 55.0002f }, { 29552, 55.0002f },
   { 30816, 54.9810f }, { 31064, 55.0146f }, { 31512, 55.0195f }, { 32063, 55.0146f },
   { 32605, 55.0195f }, { 33049, 55.0146f }, { 33597, 54.9954f }, { 34044, 54.9666f },
   { 34595, 55.0050f }, { 35045, 55.0098f }, { 35593, 54.9954f }, { 36043, 55.0146f },
   { 36590, 55.0050f }, { 37037, 55.0050f }, { 37582, 55.0531f }, { 38026, 55.0195f },
   { 38572, 54.9906f }, { 39020, 55.0050f }, { 39571, 55.0387f }, { 40024, 55.0195f },
   { 40575, 55.0483f }, { 41022, 55.0146f }, { 41569, 55.0483f }, { 42020, 55.0339f },
   { 42564, 55.0291f }, { 43114, 55.0243f }, { 43560, 54.9954f }, { 44107, 55.0339f },
   { 44554, 55.0339f }, { 45763, 55.0579f }, { 46115, 55.0339f }, { 46565, 55.0243f },
   { 47018, 55.0627f }, { 47571, 55.0531f }, { 48023, 55.0339f }, { 48575, 55.0291f },
   { 49023, 55.0579f }, { 49577, 55.0339f }, { 50032, 55.0387f }, { 50582, 55.0387f },
   { 51030, 55.0675f }, { 51578, 55.0339f }, { 52029, 55.0675f }, { 52577, 55.0531f },
   { 53034, 55.0675f }, { 53586, 55.0339f }, { 54031, 55.0627f }, { 54575, 55.0627f },
   { 55023, 55.0627f }, { 55570, 55.0387f }, { 56016, 55.0627f }, { 56561, 55.0435f },
   { 57103, 55.0916f }, { 57548, 55.0435f }, { 58100, 54.9954f }, { 58550, 55.0579f },
   { 59099, 55.0675f }, { 59551, 55.0723f }, { 60817, 55.0675f }, { 61064, 55.0964f },
   { 61609, 55.0675f }, { 62055, 55.0819f }, { 62598, 55.0579f }, { 63041, 55.0723f },
   { 63590, 55.0675f }, { 64035, 55.0627f }, { 64580, 55.0531f }, { 65025, 55.0627f },
   { 65571, 55.0627f }, { 66019, 55.0867f }, { 66573, 55.0627f }, { 67019, 55.0819f },
   { 67572, 55.0531f }, { 68021, 55.0916f }, { 68571, 55.0819f }, { 69019, 55.0675f },
   { 69567, 55.0675f }, { 70018, 55.0819f }, { 70569, 55.1060f }, { 71016, 55.0964f },
   { 71569, 55.1060f }, { 72017, 55.0819f }, { 72566, 55.0867f }, { 73014, 55.0723f },
   { 73563, 55.0675f }, { 74113, 55.1012f }, { 74561, 55.0819f }, { 75765, 55.0819f },
   { 76013, 55.0627f }, { 76563, 55.1348f }, { 77113, 55.1012f }, { 77560, 55.0916f },
   { 78108, 55.0819f }, { 78556, 55.1012f }, { 79106, 55.1012f }, { 79552, 55.0964f },
   { 80105, 55.1060f }, { 80554, 55.1108f }, { 81103, 55.1444f }, { 81555, 55.0964f },
   { 82101, 55.0964f }, { 82546, 55.0819f }, { 83092, 55.1060f }, { 83535, 55.0916f },
   { 84082, 55.0964f }, { 84528, 55.1204f }, { 85076, 55.1300f }, { 85525, 55.0916f },
   { 86071, 55.1012f }, { 86519, 55.1252f }, { 87067, 55.1204f }, { 87611, 55.1204f },
   { 88056, 55.1396f }, { 88602, 55.1492f }, { 89044, 55.1012f }, { 89588, 55.1637f },
   { 90728, 55.1060f }, { 91076, 55.1012f }, { 91519, 55.1540f }, { 92061, 55.1156f },
   { 92604, 55.1348f }, { 93047, 55.1588f }, { 93588, 55.1348f }, { 94035, 55.1588f },
   { 94579, 55.1252f }, { 95024, 55.1588f }, { 95570, 55.1300f }, { 96013, 55.1300f },
   { 96559, 55.1012f }, { 97103, 55.1156f }, { 97547, 55.1492f }, { 98091, 55.1204f },
   { 98534, 55.1781f }, { 99081, 55.1492f }, { 99528, 55.1204f }, { 100078, 55.1060f },
   { 100525, 55.1540f }, { 101073, 55.1685f }, { 101521, 55.1156f }, { 102065, 55.1829f },
   { 102606, 55.1540f }, { 103049, 55.1396f }, { 103594, 55.1829f }, { 104041, 55.1396f },
   { 104585, 55.1829f }, { 105667, 55.1588f }, { 106013, 55.1396f }, { 106559, 55.1396f },
   { 107104, 55.1300f }, { 107552, 55.1733f }, { 108095, 55.1781f }, { 108538, 55.1588f },
   { 109081, 55.1300f }, { 109525, 55.1588f }, { 110072, 55.1492f }, { 110520, 55.1348f },
   { 111061, 55.1444f }, { 111603, 55.1492f }, { 112047, 55.1829f }, { 112606, 55.2117f },
   { 113051, 55.1925f }, { 113591, 55.1396f }, { 114029, 55.1829f }, { 114569, 55.1685f },
   { 115013, 55.2021f }, { 115555, 55.2069f }, { 116095, 55.1492f }, { 116535, 55.1877f },
   { 117074, 55.1685f }, { 117523, 55.1540f }, { 118063, 55.1492f }, { 118604, 55.1685f },
   { 119048, 55.1973f }, { 119585, 55.1588f }, { 120723, 55.1492f }, { 121066, 55.2021f },
   { 121609, 55.1829f }, { 122053, 55.1877f }, { 122595, 55.2117f }, { 123034, 55.2021f },
   { 123574, 55.2117f }, { 124018, 55.1877f }, { 124559, 55.1733f }, { 125102, 55.1877f },
   { 125548, 55.1829f }, { 126092, 55.2069f }, { 126537, 55.1973f }, { 127084, 55.2165f },
   { 127532, 55.2069f }, { 128079, 55.2021f }, { 128526, 55.1973f }, { 129071, 55.2069f },
   { 129517, 55.1973f }, { 130066, 55.1781f }, { 130515, 55.1973f }, { 131064, 55.2021f },
   { 131513, 55.2598f }, { 132060, 55.1973f }, { 132605, 55.2021f }, { 133053, 55.2117f },
   { 133601, 55.1829f }, { 134050, 55.1877f }, { 134595, 55.1829f }, { 135676, 55.1973f },
   { 136024, 55.1925f }, { 136574, 55.2069f }, { 137021, 55.2358f }, { 137566, 55.2358f },
   { 138109, 55.1781f }, { 138552, 55.2502f }, { 139099, 55.2069f }, { 139542, 55.2550f },
   { 140094, 55.2165f }, { 140539, 55.2165f }, { 141083, 55.2358f }, { 141528, 55.2021f },
   { 142070, 55.2117f }, { 142605, 55.2358f }, { 143048, 55.2406f }, { 143588, 55.2117f },
   { 144029, 55.2117f }, { 144568, 55.2550f }, { 145111, 55.1829f }, { 145553, 55.2069f },
   { 146093, 55.2309f }, { 146537, 55.2358f }, { 147080, 55.2261f }, { 147520, 55.2358f },
   { 148059, 55.2358f }, { 148599, 55.2598f }, { 149038, 55.2454f }, { 149582, 55.2358f },
   { 150728, 55.2261f }, { 151075, 55.2550f }, { 151518, 55.2358f }, { 152060, 55.2598f },
   { 152599, 55.2598f }, { 153037, 55.2646f }, { 153576, 55.2790f }, { 154019, 55.2598f },
   { 154560, 55.2598f }, { 155099, 55.2742f }, { 155542, 55.2406f }, { 156083, 55.2838f },
   { 156524, 55.2598f }, { 157063, 55.2358f }, { 157602, 55.2646f }, { 158040, 55.2742f },
   { 158577, 55.2454f }, { 159016, 55.2502f }, { 159563, 55.2550f }, { 160107, 55.2358f },
   { 160551, 55.2646f }, { 161095, 55.2646f }, { 161537, 55.2646f }, { 162080, 55.2694f },
   { 162519, 55.2790f }, { 163057, 55.2742f }, { 163596, 55.2598f }, { 164038, 55.2694f },
   { 164578, 55.2598f }, { 165688, 55.2934f }, { 166031, 55.2886f }, { 166574, 55.2550f },
   { 167017, 55.2982f }, { 167564, 55.2742f }, { 168105, 55.2838f }, { 168547, 55.2646f },
   { 169090, 55.2838f }, { 169532, 55.3078f }, { 170080, 55.2646f }, { 170526, 55.2550f },
   { 171068, 55.2934f }, { 171513, 55.2742f }, { 172055, 55.2838f }, { 172593, 55.3078f },
   { 173044, 55.3175f }, { 173588, 55.2934f }, { 174031, 55.3078f }, { 174572, 55.2790f },
   { 175015, 55.2886f }, { 175560, 55.2838f }, { 176104, 55.3078f }, { 176548, 55.3030f },
   { 177090, 55.3078f }, { 177533, 55.2934f }, { 178087, 55.3319f }, { 178535, 55.3127f },
   { 179081, 55.3127f }, { 179526, 55.2886f }, { 180932, 55.3223f }, { 181080, 55.3127f },
   { 181525, 55.3030f }, { 182069, 55.3030f }, { 182611, 55.3030f }, { 183050, 55.3367f },
   { 183592, 55.3078f }, { 184036, 55.3030f }, { 184577, 55.3078f }, { 185020, 55.3559f },
   { 185565, 55.3463f }, { 186108, 55.3319f }, { 186550, 55.3078f },
};
constexpr size_t REAL_5MIN_DATASET_COUNT = sizeof(REAL_5MIN_DATASET) / sizeof(REAL_5MIN_DATASET[0]);

test(TimedAverageTest, shouldStayContinuousWhenReplayingRealFiveMinuteDataset)
{
   setupTimedAverageTest();
   // Matches how InfluxTimeAveragedField constructs its 5-minute window (300s * 1000, default
   // bucket count) in libraries/Woyak/Influx.h.
   TimedAverageMock average(300UL * 1000UL);

   float previousAverage = NAN;
   float maxJump = 0.0f;

   for (size_t i = 0; i < REAL_5MIN_DATASET_COUNT; i++)
   {
      timedAverageTestTicks = REAL_5MIN_DATASET[i].timeMs;
      average.set(REAL_5MIN_DATASET[i].rawValue);

      float currentAverage = average.average();
      assertFalse(isnan(currentAverage));

      if (!isnan(previousAverage))
      {
         float jump = fabsf(currentAverage - previousAverage);
         maxJump = std::max(maxJump, jump);
      }

      previousAverage = currentAverage;
   }

   // The old trapezoidal/edge-extrapolated reconstruction produced ~0.3 degree jumps at
   // bucket-rotation boundaries on this exact dataset. The continuous partial-overlap
   // weighting scheme should keep every step-to-step change within normal sample-scale
   // variation instead.
   assertLess(maxJump, 0.1f);
}

test(TimedAverageTest, shouldTrackLinearRampWithHalfWindowLagOnceFull)
{
   setupTimedAverageTest();

   // 2-minute window, sampled every second, run for 2x the window duration so we can
   // observe both the startup (partially-filled) phase and the steady-state phase.
   const unsigned long durationMs = 120UL * 1000UL;
   const unsigned long sampleIntervalMs = 1000UL;
   const float rampRatePerMs = 0.01f; // value = rampRatePerMs * timeMs

   TimedAverageMock average(durationMs);

   for (unsigned long t = 0; t <= 2 * durationMs; t += sampleIntervalMs)
   {
      timedAverageTestTicks = t;
      float value = rampRatePerMs * static_cast<float>(t);
      average.set(value);

      float currentAverage = average.average();
      assertFalse(isnan(currentAverage));

      if (average.isFull())
      {
         // Once the window is fully populated, a linear ramp's time-weighted average over
         // the trailing window equals the ramp's value at the window's midpoint in time,
         // i.e. "now" minus half the window duration. Allow a slightly larger tolerance
         // right at this boundary: the handful of samples gathered before auto-sizing
         // finalized are approximated as centered within their bucket (their exact
         // centroid isn't preserved across finalization), which causes a brief, tiny
         // transient error until those samples age out of the window.
         float expected = rampRatePerMs * static_cast<float>(t - durationMs / 2);
         assertNear(expected, currentAverage, 1.0f);
      }
      else
      {
         // During startup, the average should still just be the arithmetic mean of the
         // ramp values seen so far, which for a uniformly-sampled linear ramp from 0 to t
         // is the value at the midpoint of [0, t].
         float expected = rampRatePerMs * static_cast<float>(t) / 2.0f;
         assertNear(expected, currentAverage, 0.05f);
      }
   }
}

/// <summary>
/// Generates a uniformly-distributed pseudo-random value in [-amplitude, amplitude] using a
/// simple linear congruential generator seeded deterministically, so tests are reproducible.
/// </summary>
uint32_t timedAverageTestRngState = 0;

float nextTimedAverageTestRandom(float amplitude)
{
   // Constants from Numerical Recipes' LCG.
   timedAverageTestRngState = timedAverageTestRngState * 1664525u + 1013904223u;
   float unit = static_cast<float>(timedAverageTestRngState) / static_cast<float>(UINT32_MAX);
   return (unit * 2.0f - 1.0f) * amplitude;
}

/// <summary>
/// Generates a standard-normal-ish pseudo-random value via the sum of several uniform draws
/// (an Irwin-Hall approximation to a Gaussian), using the same deterministic LCG.
/// </summary>
float nextTimedAverageTestGaussian()
{
   float sum = 0.0f;
   for (uint8_t i = 0; i < 12; i++)
   {
      timedAverageTestRngState = timedAverageTestRngState * 1664525u + 1013904223u;
      sum += static_cast<float>(timedAverageTestRngState) / static_cast<float>(UINT32_MAX);
   }
   // Sum of 12 U(0,1) draws has mean 6 and variance 1; subtracting 6 centers it at 0.
   return sum - 6.0f;
}

test(TimedAverageTest, shouldTrackConstantValueDataSource)
{
   setupTimedAverageTest();
   // A constant data source should settle to exactly that constant once the window fills,
   // regardless of sampling density.
   const unsigned long durationMs = 10000UL;
   const unsigned long sampleIntervalMs = 100UL;
   const float constantValue = 42.0f;

   TimedAverageMock average(durationMs, 20);

   for (unsigned long t = 0; t <= 2 * durationMs; t += sampleIntervalMs)
   {
      timedAverageTestTicks = t;
      average.set(constantValue);
   }

   assertTrue(average.isFull());
   assertNear(constantValue, average.average(), 0.01f);
}

test(TimedAverageTest, shouldTrackLinearlyIncreasingDataSource)
{
   setupTimedAverageTest();
   // A linearly increasing data source should average to the value at the window's
   // temporal midpoint once the window is full.
   const unsigned long durationMs = 10000UL;
   const unsigned long sampleIntervalMs = 100UL;
   const float ratePerMs = 0.5f;

   TimedAverageMock average(durationMs, 20);

   for (unsigned long t = 0; t <= 2 * durationMs; t += sampleIntervalMs)
   {
      timedAverageTestTicks = t;
      average.set(ratePerMs * static_cast<float>(t));
   }

   assertTrue(average.isFull());
   float expected = ratePerMs * static_cast<float>(2 * durationMs - durationMs / 2);
   assertNear(expected, average.average(), 1.0f);
}

test(TimedAverageTest, shouldTrackRandomUniformDataSourceNearItsMean)
{
   setupTimedAverageTest();
   // A zero-mean uniformly-distributed random data source, sampled densely, should average
   // close to zero once the window is full (the reconstructed average tracks the
   // underlying signal's mean, with residual noise shrinking as more samples accumulate).
   const unsigned long durationMs = 10000UL;
   const unsigned long sampleIntervalMs = 20UL;
   const float amplitude = 10.0f;

   timedAverageTestRngState = 12345u;
   TimedAverageMock average(durationMs, 20);

   for (unsigned long t = 0; t <= 2 * durationMs; t += sampleIntervalMs)
   {
      timedAverageTestTicks = t;
      average.set(nextTimedAverageTestRandom(amplitude));
   }

   assertTrue(average.isFull());
   assertNear(0.0f, average.average(), 1.0f);
}

test(TimedAverageTest, shouldTrackGaussianDataSourceNearItsMean)
{
   setupTimedAverageTest();
   // A zero-mean Gaussian-ish random data source, sampled densely, should average close to
   // zero once the window is full.
   const unsigned long durationMs = 10000UL;
   const unsigned long sampleIntervalMs = 20UL;

   timedAverageTestRngState = 54321u;
   TimedAverageMock average(durationMs, 20);

   for (unsigned long t = 0; t <= 2 * durationMs; t += sampleIntervalMs)
   {
      timedAverageTestTicks = t;
      average.set(nextTimedAverageTestGaussian());
   }

   assertTrue(average.isFull());
   assertNear(0.0f, average.average(), 0.5f);
}

test(TimedAverageTest, shouldTrackSineWaveDataSourceAcrossFullPeriods)
{
   setupTimedAverageTest();
   // A sine wave whose period evenly divides the window duration should average to
   // (approximately) zero once the window is full, since it always spans whole periods.
   const unsigned long durationMs = 10000UL;
   const unsigned long sampleIntervalMs = 20UL;
   const float periodMs = 1000.0f; // 10 full periods per window
   const float amplitude = 5.0f;

   TimedAverageMock average(durationMs, 20);

   for (unsigned long t = 0; t <= 2 * durationMs; t += sampleIntervalMs)
   {
      timedAverageTestTicks = t;
      float value = amplitude * sinf(2.0f * 3.14159265358979323846f * static_cast<float>(t) / periodMs);
      average.set(value);
   }

   assertTrue(average.isFull());
   assertNear(0.0f, average.average(), 0.5f);
}

} // namespace TimedAverageTests
