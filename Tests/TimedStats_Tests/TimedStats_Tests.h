#pragma once

#include <AUnit.h>
#include "TimedStats.h"

namespace TimedStatsTests
{
unsigned long timedStatsTestTicks;

unsigned long getTimedStatsTestTicks()
{
   return timedStatsTestTicks;
}

using TimedStatsMock = TimedStatsBase<getTimedStatsTestTicks>;

void setupTimedStatsTest()
{
   timedStatsTestTicks = 0;
}

test(TimedStatsTest, timedStatsRangeShouldIgnoreNearlyExpiredSingleSampleBucket)
{
   setupTimedStatsTest();
   TimedStatsMock stats(1000, 10);

   timedStatsTestTicks = 0;
   stats.set(2000.0f);

   timedStatsTestTicks = 100;
   stats.set(60.0f);
   timedStatsTestTicks = 200;
   stats.set(60.0f);
   timedStatsTestTicks = 300;
   stats.set(60.0f);
   timedStatsTestTicks = 400;
   stats.set(60.0f);
   timedStatsTestTicks = 500;
   stats.set(60.0f);
   timedStatsTestTicks = 600;
   stats.set(60.0f);
   timedStatsTestTicks = 700;
   stats.set(60.0f);
   timedStatsTestTicks = 800;
   stats.set(60.0f);
   timedStatsTestTicks = 900;
   stats.set(60.0f);

   timedStatsTestTicks = 999;

   assertEqual(60.0f, stats.average());
   assertEqual(0.0f, stats.range());
   assertEqual(0.0f, stats.stdDev());
}

test(TimedStatsTest, timedStatsCountShouldMatchExpectedWindowSampleCount)
{
   setupTimedStatsTest();
   TimedStatsMock stats(1000, 10);

   for (uint16_t i = 0; i <= 110; i++)
   {
      timedStatsTestTicks = i * 10;
      stats.set(60.0f);
   }

   timedStatsTestTicks = 1100;

   size_t count = stats.count();
   assertEqual(static_cast<size_t>(100), count);
}

test(TimedStatsTest, timedStatsRangeShouldIgnorePartiallyIncludedOutlierBucket)
{
   setupTimedStatsTest();
   TimedStatsMock stats(1000, 10);

   for (uint16_t t = 0; t < 100; t += 10)
   {
      timedStatsTestTicks = t;
      stats.set(1900.0f);
   }

   for (uint16_t t = 100; t <= 1090; t += 10)
   {
      timedStatsTestTicks = t;
      stats.set(60.0f);
   }

   timedStatsTestTicks = 1099;

   assertEqual(60.0f, stats.average());
   assertEqual(0.0f, stats.range());
   assertEqual(0.0f, stats.stdDev());
}

test(TimedStatsTest, timedStatsRangeShouldStayBoundedAcrossBoundaryTransitions)
{
   setupTimedStatsTest();
   TimedStatsMock stats(1000, 10);

   // Seed one outlier sample, then continue with stable values.
   timedStatsTestTicks = 0;
   stats.set(1800.0f);

   for (uint16_t t = 10; t <= 3000; t += 10)
   {
      timedStatsTestTicks = t;
      stats.set(59.5f);

      // Once the outlier is fully out of the 1s window, range should remain exactly zero.
      if (t >= 1200)
      {
         assertEqual(0.0f, stats.range());
      }
   }
}

} // namespace TimedStatsTests
