#pragma once

#include <AUnit.h>
#include "RollingStdDev.h"

// Population std dev of [1,2,3]: variance=(1+0+1)/3=2/3, sigma=sqrt(2/3)~0.81650
// Population std dev of [2,3,4]: variance=(1+0+1)/3=2/3, sigma=sqrt(2/3)~0.81650

test(RollingStdDevTest, shouldStartEmpty)
{
   RollingStdDev stdDev(3);

   assertEqual((size_t)3, stdDev.size());
   assertEqual((size_t)0, stdDev.count());
   assertTrue(isnan(stdDev.mean()));
   assertTrue(isnan(stdDev.get()));
   assertTrue(isnan(stdDev.last()));
}

test(RollingStdDevTest, shouldReturnFalseForZeroSize)
{
   RollingStdDev stdDev(0);

   assertFalse(stdDev.set(1.0f));
   assertEqual((size_t)0, stdDev.count());
}

test(RollingStdDevTest, singleValueHasZeroStdDev)
{
   RollingStdDev stdDev(3);

   stdDev.set(5.0f);

   assertEqual((size_t)1, stdDev.count());
   assertNear(5.0f, stdDev.mean(), 0.0001f);
   assertEqual(0.0f, stdDev.get());
   assertEqual(5.0f, stdDev.last());
}

test(RollingStdDevTest, shouldTrackBasicStats)
{
   RollingStdDev stdDev(3);

   stdDev.set(1.0f);
   stdDev.set(2.0f);
   stdDev.set(3.0f);

   assertEqual((size_t)3, stdDev.count());
   assertNear(2.0f, stdDev.mean(), 0.0001f);
   assertNear(0.81650f, stdDev.get(), 0.0001f);
   assertEqual(3.0f, stdDev.last());
}

test(RollingStdDevTest, shouldRollWindow)
{
   RollingStdDev stdDev(3);

   stdDev.set(1.0f);
   stdDev.set(2.0f);
   stdDev.set(3.0f);
   stdDev.set(4.0f); // window now [2,3,4]; 1 is removed

   assertEqual((size_t)3, stdDev.count());
   assertNear(3.0f, stdDev.mean(), 0.0001f);
   assertNear(0.81650f, stdDev.get(), 0.0001f);
   assertEqual(4.0f, stdDev.last());
}

test(RollingStdDevTest, identicalValuesHaveZeroStdDev)
{
   RollingStdDev stdDev(3);

   stdDev.set(7.0f);
   stdDev.set(7.0f);
   stdDev.set(7.0f);

   assertEqual((size_t)3, stdDev.count());
   assertNear(7.0f, stdDev.mean(), 0.0001f);
   assertEqual(0.0f, stdDev.get());
}

test(RollingStdDevTest, shouldIgnoreNonFiniteValues)
{
   RollingStdDev stdDev(3);

   stdDev.set(1.0f);
   stdDev.set(INFINITY);
   stdDev.set(NAN);

   // Only 1.0f is finite; last() reflects the raw last-stored value
   assertEqual((size_t)1, stdDev.count());
   assertNear(1.0f, stdDev.mean(), 0.0001f);
   assertEqual(0.0f, stdDev.get()); // single finite value -> sigma=0
   assertTrue(isnan(stdDev.last()));
}

test(RollingStdDevTest, shouldResetClearsState)
{
   RollingStdDev stdDev(3);

   stdDev.set(1.0f);
   stdDev.set(2.0f);
   stdDev.set(3.0f);

   stdDev.reset();

   assertEqual((size_t)0, stdDev.count());
   assertTrue(isnan(stdDev.mean()));
   assertTrue(isnan(stdDev.get()));
   assertTrue(isnan(stdDev.last()));
}
