#pragma once

#include <AUnit.h>
#include "TimedMinMax.h"

namespace TimedMinMaxTests
{
unsigned long timedMinMaxTestTicks;

unsigned long getTimedMinMaxTestTicks()
{
   return timedMinMaxTestTicks;
}

using TimedMinMaxMock = TimedMinMaxBase<getTimedMinMaxTestTicks>;

void setupTimedMinMaxTest()
{
   timedMinMaxTestTicks = 0;
}

test(TimedMinMaxTest, shouldStartEmpty)
{
   setupTimedMinMaxTest();
   TimedMinMaxMock minMax(1000);

   assertTrue(isnan(minMax.min()));
   assertTrue(isnan(minMax.max()));
   assertTrue(isnan(minMax.range()));
}

test(TimedMinMaxTest, shouldTrackMinAndMaxWithinWindow)
{
   setupTimedMinMaxTest();
   TimedMinMaxMock minMax(1000);

   timedMinMaxTestTicks = 100;
   minMax.set(5.0f);
   timedMinMaxTestTicks = 200;
   minMax.set(1.0f);
   timedMinMaxTestTicks = 300;
   minMax.set(9.0f);
   timedMinMaxTestTicks = 400;
   minMax.set(3.0f);

   assertEqual(1.0f, minMax.min());
   assertEqual(9.0f, minMax.max());
   assertEqual(8.0f, minMax.range());
}

test(TimedMinMaxTest, shouldExpireOldExtremaByTime)
{
   setupTimedMinMaxTest();
   TimedMinMaxMock minMax(1000);

   timedMinMaxTestTicks = 0;
   minMax.set(1.0f); // overall min, will expire first
   timedMinMaxTestTicks = 100;
   minMax.set(50.0f); // overall max
   timedMinMaxTestTicks = 200;
   minMax.set(40.0f);

   timedMinMaxTestTicks = 1050;

   assertEqual(40.0f, minMax.min());
   assertEqual(50.0f, minMax.max());
}

test(TimedMinMaxTest, shouldDropObsoleteCandidatesOnPush)
{
   setupTimedMinMaxTest();
   TimedMinMaxMock minMax(1000);

   // Descending sequence: each new value obsoletes the previous min candidates but
   // never the max candidates, so max() must keep reporting the first (largest) value.
   timedMinMaxTestTicks = 0;
   minMax.set(100.0f);
   timedMinMaxTestTicks = 100;
   minMax.set(90.0f);
   timedMinMaxTestTicks = 200;
   minMax.set(80.0f);

   assertEqual(100.0f, minMax.max());
   assertEqual(80.0f, minMax.min());
}

test(TimedMinMaxTest, shouldTrackCorrectlyWhenGrowingBeyondInitialCapacity)
{
   setupTimedMinMaxTest();
   TimedMinMaxMock minMax(1000, 1);

   // Strictly increasing values are all retained as min candidates, forcing the
   // deque to grow beyond its initial capacity of 1 without losing data.
   for (uint16_t i = 0; i < 8; i++)
   {
	  timedMinMaxTestTicks = i * 10;
	  minMax.set(static_cast<float>(i));
   }

   assertEqual(0.0f, minMax.min());
   assertEqual(7.0f, minMax.max());
}

test(TimedMinMaxTest, shouldSupportBackfillingWithExplicitTicks)
{
   setupTimedMinMaxTest();
   TimedMinMaxMock minMax(1000);

   // Values may be backfilled with historical ticks (e.g. reconstructed sample
   // ages), as long as ticks are supplied in non-decreasing order.
   minMax.set(20.0f, 100);
   minMax.set(5.0f, 200);
   minMax.set(15.0f, 300);

   timedMinMaxTestTicks = 300;

   assertEqual(5.0f, minMax.min());
   assertEqual(20.0f, minMax.max());
}

test(TimedMinMaxTest, shouldResetAndChangeDuration)
{
   setupTimedMinMaxTest();
   TimedMinMaxMock minMax(1000);

   timedMinMaxTestTicks = 10;
   minMax.set(5.0f);
   timedMinMaxTestTicks = 20;
   minMax.set(15.0f);

   assertEqual(5.0f, minMax.min());

   minMax.reset();
   assertTrue(isnan(minMax.min()));
   assertTrue(isnan(minMax.max()));

   minMax.setDurationMs(2000);
   assertEqual(2000UL, minMax.durationMs());
   assertTrue(isnan(minMax.min()));
}

test(TimedMinMaxTest, constructorAndDurationShouldClampToAtLeastOneMs)
{
   setupTimedMinMaxTest();

   TimedMinMaxMock minMax(0);
   assertEqual(1UL, minMax.durationMs());

   minMax.setDurationMs(0);
   assertEqual(1UL, minMax.durationMs());
}

test(TimedMinMaxTest, ensureCapacityShouldPreallocateWithoutLosingData)
{
   setupTimedMinMaxTest();
   TimedMinMaxMock minMax(1000, 1);

   assertTrue(minMax.ensureCapacity(8));

   timedMinMaxTestTicks = 10;
   minMax.set(5.0f);
   timedMinMaxTestTicks = 20;
   minMax.set(3.0f);

   assertEqual(3.0f, minMax.min());
   assertEqual(5.0f, minMax.max());
}

} // namespace TimedMinMaxTests
