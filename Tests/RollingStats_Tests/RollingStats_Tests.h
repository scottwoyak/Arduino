#pragma once

#include <AUnit.h>
#include "RollingStats.h"

namespace RollingStatsTests
{

test(RollingStatsTest, shouldStartEmpty)
{
   RollingStats stats(3);

   assertTrue(isnan(stats.get()));
   assertTrue(isnan(stats.stdDev()));
   assertEqual((size_t)0, stats.count());
   assertTrue(isnan(stats.last()));
}

test(RollingStatsTest, shouldTrackBasicAverageAndStdDev)
{
   RollingStats stats(3);

   stats.set(1.0f);
   stats.set(2.0f);
   stats.set(3.0f);

   assertNear(2.0f, stats.get(), 0.0001f);
   assertNear(0.8165f, stats.stdDev(), 0.001f);
   assertEqual((size_t)3, stats.count());
   assertNear(3.0f, stats.last(), 0.0001f);
}

test(RollingStatsTest, stdDevShouldUseRollingWindowAfterEviction)
{
   RollingStats stats(3);

   stats.set(1000.0f);
   stats.set(1.0f);
   stats.set(1.0f);
   assertTrue(stats.stdDev() > 400.0f);

   stats.set(1.0f); // window now [1,1,1]

   assertNear(1.0f, stats.get(), 0.0001f);
   assertNear(0.0f, stats.stdDev(), 0.0001f);
   assertNear(1.0f, stats.min(), 0.0001f);
   assertNear(1.0f, stats.max(), 0.0001f);
   assertNear(0.0f, stats.range(), 0.0001f);
}

test(RollingStatsTest, shouldIgnoreNonFiniteValues)
{
   RollingStats stats(3);

   stats.set(1.0f);
   stats.set(INFINITY);
   stats.set(NAN);

   assertNear(1.0f, stats.get(), 0.0001f);
   assertNear(0.0f, stats.stdDev(), 0.0001f);
   assertEqual((size_t)1, stats.count());
   assertTrue(isnan(stats.last()));
}

test(RollingStatsTest, shouldResetState)
{
   RollingStats stats(3);

   stats.set(1.0f);
   stats.set(2.0f);
   stats.reset();

   assertEqual((size_t)0, stats.count());
   assertTrue(isnan(stats.get()));
   assertTrue(isnan(stats.stdDev()));
   assertTrue(isnan(stats.last()));
}

} // namespace RollingStatsTests
