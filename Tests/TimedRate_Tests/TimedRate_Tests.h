#pragma once

#include <AUnit.h>
#include "TimedRate.h"

namespace TimedRateTests
{

unsigned long timedRateTestTicks;

unsigned long getTimedRateTestTicks()
{
   return timedRateTestTicks;
}

using TimedRateMock = TimedRateBase<getTimedRateTestTicks>;

void setupTimedRateTest()
{
   timedRateTestTicks = 0;
}

test(TimedRateTest, shouldStartEmpty)
{
   setupTimedRateTest();
   TimedRateMock rate(1000);

   assertEqual((uint16_t)0, rate.getCount());
   assertEqual((unsigned long)0, rate.getElapsedMs());
   assertEqual(0.0f, rate.get());
}

test(TimedRateTest, shouldIncreaseCountWithTicks)
{
   setupTimedRateTest();
   TimedRateMock rate(1000);

   timedRateTestTicks = 0;
   rate.tick();
   timedRateTestTicks = 100;
   rate.tick();
   timedRateTestTicks = 200;
   rate.tick();

   assertEqual((uint16_t)3, rate.getCount());
}

test(TimedRateTest, shouldComputeElapsedMs)
{
   setupTimedRateTest();
   TimedRateMock rate(1000);

   timedRateTestTicks = 0;
   rate.tick();
   timedRateTestTicks = 250;
   rate.tick();
   timedRateTestTicks = 500;
   rate.tick();

   assertEqual((unsigned long)500, rate.getElapsedMs());
}

test(TimedRateTest, shouldComputeRate)
{
   setupTimedRateTest();
   TimedRateMock rate(1000);

   // 5 ticks spaced 100ms apart -> 4 intervals spanning 400ms -> 10 ticks/sec
   for (int i = 0; i < 5; i++)
   {
      timedRateTestTicks = i * 100;
      rate.tick();
   }

   assertEqual(10.0f, rate.get());
}

test(TimedRateTest, shouldEvictTicksOutsideWindow)
{
   setupTimedRateTest();
   TimedRateMock rate(500);

   timedRateTestTicks = 0;
   rate.tick();
   timedRateTestTicks = 200;
   rate.tick();
   timedRateTestTicks = 400;
   rate.tick();
   timedRateTestTicks = 900; // evicts ticks at 0 (age 900) and 200 (age 700); only tick at 400 (age 500) remains

   assertEqual((uint16_t)1, rate.getCount());
}

test(TimedRateTest, shouldResetClearsAllTicks)
{
   setupTimedRateTest();
   TimedRateMock rate(1000);

   timedRateTestTicks = 0;
   rate.tick();
   timedRateTestTicks = 100;
   rate.tick();

   rate.reset();

   assertEqual((uint16_t)0, rate.getCount());
   assertEqual(0.0f, rate.get());
}

} // namespace TimedRateTests
