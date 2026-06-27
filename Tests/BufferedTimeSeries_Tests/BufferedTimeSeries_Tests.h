#pragma once

#include <AUnit.h>
#include "BufferedTimeSeries.h"
#include <cmath>

namespace BufferedTimeSeriesTests
{
using namespace aunit;

test(BufferedTimeSeriesTest, shouldInterpolateMidpoint)
{
   // window 200ms, resolution 50ms
   BufferedTimeSeries ts(200, 50);

   // first sample
   ts.set(0.0f);
   // wait 150ms
   delay(150);
   // second sample
   ts.set(30.0f);

   float v = ts.get();
   // expected: target_time is now - 100ms which is 50ms after first sample
   // fraction = 50 / 150 = 1/3 -> expected = 0 + (30 - 0) * 1/3 = 10
   float expected = 10.0f;

   // allow small tolerance for timing jitter
   float diff = fabs(v - expected);
   assertTrue(diff < 2.0f);
}

test(BufferedTimeSeriesTest, shouldReturnNaNWhenTargetOutsideRange)
{
   BufferedTimeSeries ts(200, 50);

   ts.set(5.0f);
   // immediate get: target is now -100ms, which is before the only sample
   float v = ts.get();

   assertTrue(std::isnan(v));
}

// New separate tests: empty buffer should return NaN; expired samples should result in NaN.
test(BufferedTimeSeriesTest, shouldReturnNaNWhenEmpty)
{
   BufferedTimeSeries ts(200, 50);

   float vEmpty = ts.get();
   assertTrue(std::isnan(vEmpty));
}

test(BufferedTimeSeriesTest, shouldReturnNaNAfterExpired)
{
   BufferedTimeSeries ts(200, 50);

   ts.set(5.0f);
   delay(300); // window is 200ms, so 300ms ensures the sample is outside the window

   float vExpired = ts.get();
   assertTrue(std::isnan(vExpired));
}

} // namespace BufferedTimeSeriesTests
