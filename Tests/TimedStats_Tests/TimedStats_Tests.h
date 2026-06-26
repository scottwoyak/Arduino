#pragma once

#include <AUnit.h>
#include "TimedAverage.h"

unsigned long timedStatsTestTicks;

unsigned long getTimedStatsTestTicks()
{
   return timedStatsTestTicks;
}

using TimedAverageMock = TimedAverageBase<getTimedStatsTestTicks>;

void setupTimedStatsTest()
{
   timedStatsTestTicks = 0;
}

test(TimedStatsTest, TimedAverage_shouldStartEmpty)
{
   setupTimedStatsTest();
   TimedAverageMock average(1000);

   assertTrue(isnan(average.average()));
   assertEqual((size_t)0, average.count());
}

test(TimedStatsTest, shouldTrackAverageWithinWindow)
{
   setupTimedStatsTest();
   TimedAverageMock average(1000);

   timedStatsTestTicks = 100;
   average.set(1.0f);
   timedStatsTestTicks = 200;
   average.set(2.0f);
   timedStatsTestTicks = 300;
   average.set(3.0f);

   assertNear(2.0f, average.average(), 0.0001f);
   assertEqual((size_t)3, average.count());
}

test(TimedStatsTest, shouldExpireOldSamplesByTime)
{
   setupTimedStatsTest();
   TimedAverageMock average(1000);

   timedStatsTestTicks = 0;
   average.set(1.0f);
   timedStatsTestTicks = 500;
   average.set(2.0f);
   timedStatsTestTicks = 1000;
   average.set(3.0f);

   timedStatsTestTicks = 1400;

   assertNear(2.5f, average.average(), 0.0001f);
   assertEqual((size_t)2, average.count());
}

test(TimedStatsTest, TimedAverage_shouldIgnoreNonFiniteValues)
{
   setupTimedStatsTest();
   TimedAverageMock average(1000);

   timedStatsTestTicks = 100;
   average.set(1.0f);
   timedStatsTestTicks = 200;
   average.set(INFINITY);
   timedStatsTestTicks = 300;
   average.set(NAN);

   assertNear(1.0f, average.average(), 0.0001f);
   assertEqual((size_t)1, average.count());
}

test(TimedStatsTest, shouldResetWhenDurationChanges)
{
   setupTimedStatsTest();
   TimedAverageMock average(1000);

   timedStatsTestTicks = 100;
   average.set(1.0f);
   timedStatsTestTicks = 200;
   average.set(2.0f);

   assertEqual((size_t)2, average.count());

   average.setDurationMs(2000);

   assertEqual((unsigned long)2000, average.durationMs());
   assertEqual((size_t)0, average.count());
   assertTrue(isnan(average.average()));
}

test(TimedStatsTest, shouldClampDurationToAtLeastOneMillisecond)
{
   setupTimedStatsTest();
   TimedAverageMock average(1000);

   average.setDurationMs(0);

   assertEqual((unsigned long)1, average.durationMs());
}

test(TimedStatsTest, shouldResetClearsStateWithoutChangingDuration)
{
   setupTimedStatsTest();
   TimedAverageMock average(1000);

   timedStatsTestTicks = 100;
   average.set(1.0f);
   timedStatsTestTicks = 200;
   average.set(2.0f);

   average.reset();

   assertEqual((unsigned long)1000, average.durationMs());
   assertEqual((size_t)0, average.count());
   assertTrue(isnan(average.average()));
}
