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

test(TimedAverageTest, shouldResetWhenDurationChanges)
{
   setupTimedAverageTest();
   TimedAverageMock average(1000);

   timedAverageTestTicks = 100;
   average.set(1.0f);
   timedAverageTestTicks = 200;
   average.set(2.0f);

   assertEqual((size_t)2, average.count());

   average.setDurationMs(2000);

   assertEqual((unsigned long)2000, average.durationMs());
   assertEqual((size_t)0, average.count());
   assertTrue(isnan(average.average()));
}

test(TimedAverageTest, shouldClampDurationToAtLeastOneMillisecond)
{
   setupTimedAverageTest();
   TimedAverageMock average(1000);

   average.setDurationMs(0);

   assertEqual((unsigned long)1, average.durationMs());
}

test(TimedAverageTest, shouldResetClearsStateWithoutChangingDuration)
{
   setupTimedAverageTest();
   TimedAverageMock average(1000);

   timedAverageTestTicks = 100;
   average.set(1.0f);
   timedAverageTestTicks = 200;
   average.set(2.0f);

   average.reset();

   assertEqual((unsigned long)1000, average.durationMs());
   assertEqual((size_t)0, average.count());
   assertTrue(isnan(average.average()));
}

} // namespace TimedAverageTests
