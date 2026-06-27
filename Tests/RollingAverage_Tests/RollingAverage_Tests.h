#pragma once

#include <AUnit.h>
#include "RollingAverage.h"

namespace RollingAverageTests
{

test(RollingAverageTest, shouldStartEmpty)
{
   RollingAverage average(3);

   assertTrue(isnan(average.get()));
   assertEqual((size_t)0, average.count());
   assertTrue(isnan(average.last()));
}

test(RollingAverageTest, shouldTrackAverage)
{
   RollingAverage average(3);

   average.set(1.0f);
   average.set(2.0f);
   average.set(3.0f);

   assertNear(2.0f, average.get(), 0.0001f);
   assertEqual((size_t)3, average.count());
   assertNear(3.0f, average.last(), 0.0001f);
}

test(RollingAverageTest, shouldUseRollingWindowAfterEviction)
{
   RollingAverage average(3);

   average.set(1000.0f);
   average.set(1.0f);
   average.set(1.0f);
   assertTrue(average.get() > 300.0f);

   average.set(1.0f); // window now [1,1,1]

   assertNear(1.0f, average.get(), 0.0001f);
   assertEqual((size_t)3, average.count());
   assertNear(1.0f, average.last(), 0.0001f);
}

test(RollingAverageTest, shouldIgnoreNonFiniteValues)
{
   RollingAverage average(3);

   average.set(1.0f);
   average.set(INFINITY);
   average.set(NAN);

   assertNear(1.0f, average.get(), 0.0001f);
   assertEqual((size_t)1, average.count());
   assertTrue(isnan(average.last()));
}

test(RollingAverageTest, shouldResetState)
{
   RollingAverage average(3);

   average.set(1.0f);
   average.set(2.0f);
   average.reset();

   assertEqual((size_t)0, average.count());
   assertTrue(isnan(average.get()));
   assertTrue(isnan(average.last()));
}

test(RollingAverageTest, shouldSupportResizeOnReset)
{
   RollingAverage average(3);

   average.set(1.0f);
   average.set(2.0f);
   average.reset(5);

   assertEqual((size_t)5, average.size());
   assertEqual((size_t)0, average.count());
   assertTrue(isnan(average.get()));
}

test(RollingAverageTest, shouldReturnFalseWhenSizeIsZero)
{
   RollingAverage average(0);

   assertFalse(average.set(1.0f));
   assertEqual((size_t)0, average.count());
   assertTrue(isnan(average.get()));
}

} // namespace RollingAverageTests
