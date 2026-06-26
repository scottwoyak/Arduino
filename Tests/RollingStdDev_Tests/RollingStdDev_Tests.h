#pragma once

#include <AUnit.h>
#include "RollingAverage.h"

test(RollingStatsTest, RollingAverage_shouldStartEmpty)
{
   RollingAverage average(3);

   assertTrue(isnan(average.get()));
   assertEqual((size_t)0, average.count());
   assertTrue(isnan(average.last()));
}

test(RollingStatsTest, shouldTrackBasicAverage)
{
   RollingAverage average(3);

   average.set(1.0f);
   average.set(2.0f);
   average.set(3.0f);

   assertNear(2.0f, average.get(), 0.0001f);
   assertEqual((size_t)3, average.count());
   assertNear(3.0f, average.last(), 0.0001f);
}

test(RollingStatsTest, shouldRollWindow)
{
   RollingAverage average(3);

   average.set(1.0f);
   average.set(2.0f);
   average.set(3.0f);
   average.set(4.0f); // window now [4,2,3]

   assertNear(3.0f, average.get(), 0.0001f);
   assertEqual((size_t)3, average.count());
   assertNear(4.0f, average.last(), 0.0001f);
}

test(RollingStatsTest, RollingAverage_shouldIgnoreNonFiniteValues)
{
   RollingAverage average(3);

   average.set(1.0f);
   average.set(INFINITY);
   average.set(NAN);

   assertNear(1.0f, average.get(), 0.0001f);
   assertEqual((size_t)1, average.count());
   assertTrue(isnan(average.last()));
}

test(RollingStatsTest, rollingAverageShouldResetState)
{
   RollingAverage average(3);

   average.set(1.0f);
   average.set(2.0f);
   average.reset();

   assertEqual((size_t)0, average.count());
   assertTrue(isnan(average.get()));
   assertTrue(isnan(average.last()));
}
