#pragma once

#include <math.h>
#include "LGX_ST7796S.h"
#include <AUnit.h>
#include "ScatterPlotSeries.h"
#include "TimedScatterPlotSeries.h"

namespace ScatterPlotSeriesTests
{

// ----------- Growing mode (default, no fixed range / rolling count)

test(ScatterPlotSeriesTest, shouldStartEmpty)
{
   ScatterPlotSeries series(10);

   assertEqual((size_t)0, series.pointCount());
}

test(ScatterPlotSeriesTest, shouldAppendAutoIncrementingXValues)
{
   ScatterPlotSeries series(10);

   series.add(10.0f);
   series.add(20.0f);
   series.add(30.0f);

   assertEqual((size_t)3, series.pointCount());
   assertEqual(0.0f, series.xValues()[0]);
   assertEqual(1.0f, series.xValues()[1]);
   assertEqual(2.0f, series.xValues()[2]);
   assertEqual(10.0f, series.yValues()[0]);
   assertEqual(20.0f, series.yValues()[1]);
   assertEqual(30.0f, series.yValues()[2]);
}

test(ScatterPlotSeriesTest, shouldAppendExplicitXYPairs)
{
   ScatterPlotSeries series(10);

   series.add(5.0f, 100.0f);
   series.add(15.0f, 200.0f);

   assertEqual((size_t)2, series.pointCount());
   assertEqual(5.0f, series.xValues()[0]);
   assertEqual(15.0f, series.xValues()[1]);
   assertEqual(100.0f, series.yValues()[0]);
   assertEqual(200.0f, series.yValues()[1]);
}

test(ScatterPlotSeriesTest, shouldReportRawRangeAcrossAddedPoints)
{
   ScatterPlotSeries series(10);

   series.add(1.0f, 5.0f);
   series.add(2.0f, -3.0f);
   series.add(3.0f, 10.0f);

   float xMin, xMax, yMin, yMax;
   series.getRawRange(&xMin, &xMax, &yMin, &yMax);

   assertEqual(1.0f, xMin);
   assertEqual(3.0f, xMax);
   assertEqual(-3.0f, yMin);
   assertEqual(10.0f, yMax);
}

test(ScatterPlotSeriesTest, shouldClearBackToEmpty)
{
   ScatterPlotSeries series(10);

   series.add(1.0f);
   series.add(2.0f);
   series.clear();

   assertEqual((size_t)0, series.pointCount());
}

// ----------- Rolling-count mode

test(ScatterPlotSeriesTest, shouldFillRollingBufferBeforeScrolling)
{
   ScatterPlotSeries series;
   series.setRollingCount(3);

   series.add(1.0f);
   series.add(2.0f);

   assertEqual((size_t)2, series.pointCount());
   assertEqual(1.0f, series.yValues()[0]);
   assertEqual(2.0f, series.yValues()[1]);
}

test(ScatterPlotSeriesTest, shouldScrollOldestSampleOutOnceRollingBufferIsFull)
{
   ScatterPlotSeries series;
   series.setRollingCount(3);

   series.add(1.0f);
   series.add(2.0f);
   series.add(3.0f);
   series.add(4.0f);

   assertEqual((size_t)3, series.pointCount());
   assertEqual(2.0f, series.yValues()[0]);
   assertEqual(3.0f, series.yValues()[1]);
   assertEqual(4.0f, series.yValues()[2]);
}

test(ScatterPlotSeriesTest, shouldKeepFixedXSlotsInRollingMode)
{
   ScatterPlotSeries series;
   series.setRollingCount(3);

   series.add(1.0f);
   series.add(2.0f);
   series.add(3.0f);
   series.add(4.0f);

   assertEqual(1.0f, series.xValues()[0]);
   assertEqual(2.0f, series.xValues()[1]);
   assertEqual(3.0f, series.xValues()[2]);
}

// ----------- Fixed-range bin mode

test(ScatterPlotSeriesTest, shouldAverageSamplesFallingInSameBin)
{
   ScatterPlotSeries series;
   series.setFixedXRange(0.0f, 10.0f, 5);

   // bin width is 2, so both samples fall in bin covering [0, 2)
   series.add(0.5f, 10.0f);
   series.add(1.5f, 20.0f);
   series.prepareForRender();

   assertEqual((size_t)5, series.pointCount());
   assertEqual(15.0f, series.yValues()[0]);
}

test(ScatterPlotSeriesTest, shouldReportNanForEmptyBinsInFixedRangeMode)
{
   ScatterPlotSeries series;
   series.setFixedXRange(0.0f, 10.0f, 5);

   series.add(0.5f, 10.0f);
   series.prepareForRender();

   assertTrue(isnan(series.yValues()[1]));
}

// ----------- Moving average

test(ScatterPlotSeriesTest, shouldComputeMovingAverageOverWindow)
{
   ScatterPlotSeries series(10);
   series.movingSampleSize = 2.0f;
   series.finalized = true;

   series.add(1.0f, 10.0f);
   series.add(2.0f, 20.0f);
   series.add(3.0f, 30.0f);

   series.prepareForRender();
   const float* movingAvg = series.movingAverageValues();

   assertNotEqual((const float*)nullptr, movingAvg);
   // window of +/-1.0 around x=2.0 covers all three points
   assertNear(20.0f, movingAvg[1], 0.001f);
}

test(ScatterPlotSeriesTest, shouldNotEmitMovingAverageBeyondMaxSampleXWhenNotFinalized)
{
   ScatterPlotSeries series(10);
   series.movingSampleSize = 10.0f;
   series.finalized = false;

   series.add(1.0f, 10.0f);
   series.add(2.0f, 20.0f);

   series.prepareForRender();

   // With a wide window and unfinalized series, no vertex should be ready yet since
   // future samples could still shift the trailing window's average.
   assertEqual((size_t)0, series.getMovingAverageReadyCount());
}

// ----------- Std dev band

test(ScatterPlotSeriesTest, shouldComputeStdDevBandOverWindow)
{
   ScatterPlotSeries series(10);
   series.movingSampleSize = 4.0f;
   series.finalized = true;

   series.add(1.0f, 10.0f);
   series.add(2.0f, 10.0f);
   series.add(3.0f, 10.0f);

   series.prepareForRender();
   const float* low = series.stdDevLowValues();
   const float* high = series.stdDevHighValues();

   assertNotEqual((const float*)nullptr, low);
   assertNotEqual((const float*)nullptr, high);
   // constant values -> zero stddev -> low == high == mean
   assertNear(10.0f, low[1], 0.001f);
   assertNear(10.0f, high[1], 0.001f);
}

test(ScatterPlotSeriesTest, shouldWidenStdDevBandWithVariance)
{
   ScatterPlotSeries series(10);
   series.movingSampleSize = 10.0f;
   series.finalized = true;

   series.add(0.0f, 0.0f);
   series.add(1.0f, 10.0f);
   series.add(2.0f, 20.0f);

   series.prepareForRender();
   const float* low = series.stdDevLowValues();
   const float* high = series.stdDevHighValues();

   assertTrue(low[1] < 10.0f);
   assertTrue(high[1] > 10.0f);
}

// This test reproduces a real bug found by visually inspecting a live serial dump: with
// float-precision sum/sumSquares accumulators, the stddev band's variance formula
// (E[x^2] - E[x]^2) suffered catastrophic cancellation at realistic sensor magnitudes
// (~71.0) with tiny real variance (~0.02), causing the computed variance to go slightly
// negative, get clamped to zero, and collapse the band to low == high == mean even
// though the raw samples were clearly noisy. All the existing tests above used small
// magnitudes (10s and 20s) with comparatively large variance, so they never exercised
// this precision floor and kept passing throughout.
test(ScatterPlotSeriesTest, shouldWidenStdDevBandWithVarianceAtRealisticSensorMagnitude)
{
   ScatterPlotSeries series(10);
   series.movingSampleSize = 10.0f;
   series.finalized = true;

   // Small, realistic sensor noise (+/- 0.02 or so) riding on top of a large offset
   // (~71.0), the same magnitude/variance ratio that exposed the cancellation bug.
   series.add(0.0f, 71.00f);
   series.add(1.0f, 71.02f);
   series.add(2.0f, 71.00f);
   series.add(3.0f, 71.03f);
   series.add(4.0f, 71.01f);

   series.prepareForRender();
   const float* low = series.stdDevLowValues();
   const float* high = series.stdDevHighValues();

   assertNotEqual((const float*)nullptr, low);
   assertNotEqual((const float*)nullptr, high);

   // The band must actually widen around the noisy data instead of collapsing to a
   // false zero-width band (low == high == mean) from float rounding error.
   assertTrue(high[2] > low[2]);
   assertTrue((high[2] - low[2]) > 0.005f);
}

// Reproduces the same cancellation bug but across a long run of samples (mirroring the
// ~35-sample-wide collapse observed in the live dump), checking that no point in a
// noisy, realistic-magnitude series ever reports a fully collapsed (zero-width) band.
test(ScatterPlotSeriesTest, shouldNotCollapseStdDevBandAcrossManySamplesAtRealisticMagnitude)
{
   ScatterPlotSeries series(64);
   series.movingSampleSize = 6.0f;
   series.finalized = true;

   // Alternate between two close-but-distinct values around a large offset, similar in
   // shape to the noisy live sensor data that exposed the bug.
   constexpr size_t NUM_SAMPLES = 40;
   for (size_t i = 0; i < NUM_SAMPLES; i++)
   {
      float value = 71.10f + ((i % 2 == 0) ? 0.02f : -0.02f);
      series.add(static_cast<float>(i), value);
   }

   series.prepareForRender();
   const float* low = series.stdDevLowValues();
   const float* high = series.stdDevHighValues();
   size_t readyCount = series.getStdDevReadyCount();

   assertNotEqual((const float*)nullptr, low);
   assertNotEqual((const float*)nullptr, high);

   for (size_t i = 0; i < readyCount; i++)
   {
      if (isnan(low[i]) || isnan(high[i]))
      {
         continue;
      }

      assertTrue((high[i] - low[i]) > 0.005f);
   }
}

// ----------- Rolling overlay reuse (scroll, don't recompute)

test(ScatterPlotSeriesTest, shouldPreserveMovingAverageAsRollingBufferScrolls)
{
   ScatterPlotSeries series;
   series.setRollingCount(4);
   series.movingSampleSize = 2.0f;
   series.finalized = true;

   series.add(10.0f);
   series.add(10.0f);
   series.add(10.0f);
   series.add(10.0f);
   series.prepareForRender();
   float firstReadyValue = series.movingAverageValues()[0];

   // Scroll the buffer; earlier computed values for still-present slots should be
   // unaffected since the underlying data (constant 10s) has not changed.
   series.add(10.0f);
   series.prepareForRender();

   assertNear(firstReadyValue, series.movingAverageValues()[0], 0.001f);
}

// ----------- TimedScatterPlotSeries

test(ScatterPlotSeriesTest, timedSeriesShouldStartEmpty)
{
   TimedScatterPlotSeries series(1000, 5);

   assertEqual((size_t)0, series.pointCount());
}

test(ScatterPlotSeriesTest, timedSeriesShouldReportFixedXRangeFromHistoryMsAndBins)
{
   TimedScatterPlotSeries series(1000, 5);

   float xMin, xMax;
   bool hasRange = series.getFixedXRange(&xMin, &xMax);

   assertTrue(hasRange);
   assertEqual(-1000.0f, xMin);
   assertEqual(0.0f, xMax);
}

test(ScatterPlotSeriesTest, timedSeriesShouldReflectAddedSampleInCurrentBin)
{
   TimedScatterPlotSeries series(1000, 5);

   series.add(42.0f);
   series.prepareForRender();

   // The current (still-open) bin is intentionally reported as NAN since it can still
   // receive more samples before it closes - see TimedScatterPlotSeries::_refreshTimeSnapshot().
   assertEqual((size_t)1, series.pointCount());
   assertTrue(isnan(series.yValues()[0]));
   assertNear(0.0f, series.xValues()[0], 0.001f);
}

test(ScatterPlotSeriesTest, timedSeriesShouldClearBackToEmpty)
{
   TimedScatterPlotSeries series(1000, 5);

   series.add(1.0f);
   series.clear();

   assertEqual((size_t)0, series.pointCount());
}

} // namespace ScatterPlotSeriesTests
