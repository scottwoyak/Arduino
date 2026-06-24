#pragma once

#include <AUnit.h>
#include "TimedStats.h"

unsigned long timedStatsTestTicks;

unsigned long getTimedStatsTestTicks()
{
   return timedStatsTestTicks;
}

void setupTimedStatsTest()
{
   timedStatsTestTicks = 0;
   TimedStats::tickFunc = getTimedStatsTestTicks;
}

test(TimedStats_shouldStartEmpty)
{
   setupTimedStatsTest();
   TimedStats stats(1000);

   assertTrue(isnan(stats.min()));
   assertTrue(isnan(stats.max()));
   assertTrue(isnan(stats.average()));
   assertTrue(isnan(stats.range()));
   assertEqual((size_t)0, stats.count());
}

test(shouldTrackStatsWithinWindow)
{
   setupTimedStatsTest();
   TimedStats stats(1000);

   timedStatsTestTicks = 100;
   stats.set(1.0f);
   timedStatsTestTicks = 200;
   stats.set(2.0f);
   timedStatsTestTicks = 300;
   stats.set(3.0f);

   assertNear(1.0f, stats.min(), 0.0001f);
   assertNear(3.0f, stats.max(), 0.0001f);
   assertNear(2.0f, stats.average(), 0.0001f);
   assertNear(2.0f, stats.range(), 0.0001f);
   assertEqual((size_t)3, stats.count());
}

test(shouldExpireOldSamplesByTime)
{
   setupTimedStatsTest();
   TimedStats stats(1000);

   timedStatsTestTicks = 0;
   stats.set(1.0f);
   timedStatsTestTicks = 500;
   stats.set(2.0f);
   timedStatsTestTicks = 1000;
   stats.set(3.0f);

   timedStatsTestTicks = 1400;

   assertNear(2.0f, stats.min(), 0.0001f);
   assertNear(3.0f, stats.max(), 0.0001f);
   assertNear(2.5f, stats.average(), 0.0001f);
   assertNear(1.0f, stats.range(), 0.0001f);
   assertEqual((size_t)2, stats.count());
}

test(TimedStats_shouldIgnoreNonFiniteValues)
{
   setupTimedStatsTest();
   TimedStats stats(1000);

   timedStatsTestTicks = 100;
   stats.set(1.0f);
   timedStatsTestTicks = 200;
   stats.set(INFINITY);
   timedStatsTestTicks = 300;
   stats.set(NAN);

   assertNear(1.0f, stats.min(), 0.0001f);
   assertNear(1.0f, stats.max(), 0.0001f);
   assertNear(1.0f, stats.average(), 0.0001f);
   assertNear(0.0f, stats.range(), 0.0001f);
   assertEqual((size_t)1, stats.count());
}

test(shouldResetWhenDurationChanges)
{
   setupTimedStatsTest();
   TimedStats stats(1000);

   timedStatsTestTicks = 100;
   stats.set(1.0f);
   timedStatsTestTicks = 200;
   stats.set(2.0f);

   assertEqual((size_t)2, stats.count());

   stats.setDurationMs(2000);

   assertEqual((unsigned long)2000, stats.durationMs());
   assertEqual((size_t)0, stats.count());
   assertTrue(isnan(stats.average()));
}

test(shouldClampDurationToAtLeastOneMillisecond)
{
   setupTimedStatsTest();
   TimedStats stats(1000);

   stats.setDurationMs(0);

   assertEqual((unsigned long)1, stats.durationMs());
}

test(shouldResetClearsStateWithoutChangingDuration)
{
   setupTimedStatsTest();
   TimedStats stats(1000);

   timedStatsTestTicks = 100;
   stats.set(1.0f);
   timedStatsTestTicks = 200;
   stats.set(2.0f);

   stats.reset();

   assertEqual((unsigned long)1000, stats.durationMs());
   assertEqual((size_t)0, stats.count());
   assertTrue(isnan(stats.average()));
   assertTrue(isnan(stats.min()));
   assertTrue(isnan(stats.max()));
   assertTrue(isnan(stats.range()));
}
