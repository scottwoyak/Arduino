#include <AUnit.h>
#include <TimedStats.h>

unsigned long ticks;

unsigned long getTicks()
{
   return ticks;
}

test(shouldStartEmpty)
{
   ticks = 0;
   TimedStats stats(1000);

   assertTrue(isnan(stats.min()));
   assertTrue(isnan(stats.max()));
   assertTrue(isnan(stats.get()));
   assertTrue(isnan(stats.range()));
   assertEqual((size_t)0, stats.count());
}

test(shouldTrackStatsWithinWindow)
{
   ticks = 0;
   TimedStats stats(1000);

   ticks = 100;
   stats.set(1.0f);
   ticks = 200;
   stats.set(2.0f);
   ticks = 300;
   stats.set(3.0f);

   assertNear(1.0f, stats.min(), 0.0001f);
   assertNear(3.0f, stats.max(), 0.0001f);
   assertNear(2.0f, stats.get(), 0.0001f);
   assertNear(2.0f, stats.range(), 0.0001f);
   assertEqual((size_t)3, stats.count());
}

test(shouldExpireOldSamplesByTime)
{
   ticks = 0;
   TimedStats stats(1000);

   ticks = 0;
   stats.set(1.0f);
   ticks = 500;
   stats.set(2.0f);
   ticks = 1000;
   stats.set(3.0f);

   ticks = 1400;

   assertNear(2.0f, stats.min(), 0.0001f);
   assertNear(3.0f, stats.max(), 0.0001f);
   assertNear(2.5f, stats.get(), 0.0001f);
   assertNear(1.0f, stats.range(), 0.0001f);
   assertEqual((size_t)2, stats.count());
}

test(shouldIgnoreNonFiniteValues)
{
   ticks = 0;
   TimedStats stats(1000);

   ticks = 100;
   stats.set(1.0f);
   ticks = 200;
   stats.set(INFINITY);
   ticks = 300;
   stats.set(NAN);

   assertNear(1.0f, stats.min(), 0.0001f);
   assertNear(1.0f, stats.max(), 0.0001f);
   assertNear(1.0f, stats.get(), 0.0001f);
   assertNear(0.0f, stats.range(), 0.0001f);
   assertEqual((size_t)1, stats.count());
}

test(shouldResetWhenDurationChanges)
{
   ticks = 0;
   TimedStats stats(1000);

   ticks = 100;
   stats.set(1.0f);
   ticks = 200;
   stats.set(2.0f);

   assertEqual((size_t)2, stats.count());

   stats.setDurationMs(2000);

   assertEqual((unsigned long)2000, stats.durationMs());
   assertEqual((size_t)0, stats.count());
   assertTrue(isnan(stats.get()));
}

test(shouldClampDurationToAtLeastOneMillisecond)
{
   ticks = 0;
   TimedStats stats(1000);

   stats.setDurationMs(0);

   assertEqual((unsigned long)1, stats.durationMs());
}

void setup()
{
   Serial.begin(115200);
   while (!Serial);

   TimedStats::tickFunc = getTicks;
}

void loop()
{
   aunit::TestRunner::run();
}
