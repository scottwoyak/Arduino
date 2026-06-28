#pragma once

#include <AUnit.h>
#include "RollingRate.h"

namespace RollingRateTests
{

unsigned long rollingRateTestTicks;
unsigned long getRollingRateTestTicks()
{
   return rollingRateTestTicks;
}

test(RollingRateTest, shouldStartAtZeroRate)
{
   RollingMicros::tickFunc = getRollingRateTestTicks;
   RollingRate rate(5);
   RollingMicros::tickFunc = nullptr;

   assertEqual(0.0f, rate.get());
   assertEqual((uint16_t)0, rate.getCount());
}

test(RollingRateTest, shouldIncreaseCountWithTicks)
{
   RollingMicros::tickFunc = getRollingRateTestTicks;
   RollingRate rate(5);
   RollingMicros::tickFunc = nullptr;

   rollingRateTestTicks = 0;
   rate.tick();
   rollingRateTestTicks = 1;
   rate.tick();
   rollingRateTestTicks = 2;
   rate.tick();

   assertEqual((uint16_t)3, rate.getCount());
}

test(RollingRateTest, shouldComputeExpectedRate)
{
   RollingMicros::tickFunc = getRollingRateTestTicks;
   RollingRate rate(10);
   RollingMicros::tickFunc = nullptr;

   rollingRateTestTicks = 0;
   rate.tick();
   rollingRateTestTicks = 100000;
   rate.tick();
   rollingRateTestTicks = 200000;
   rate.tick();

   assertEqual(15.0f, rate.get()); // 3 ticks / 0.2 sec
}

test(RollingRateTest, resetShouldClearCountAndRate)
{
   RollingMicros::tickFunc = getRollingRateTestTicks;
   RollingRate rate(10);
   RollingMicros::tickFunc = nullptr;

   rollingRateTestTicks = 0;
   rate.tick();
   rollingRateTestTicks = 5000;
   rate.tick();

   assertEqual(400.0f, rate.get());
   assertEqual((uint16_t)2, rate.getCount());

   rate.reset();

   assertEqual(0.0f, rate.get());
   assertEqual((uint16_t)0, rate.getCount());
}

test(RollingRateTest, excludingDisplayTimeShouldIncreaseMeasuredRate)
{
   RollingMicros::tickFunc = getRollingRateTestTicks;
   RollingRate baseline(10);
   RollingRate excluded(10);
   RollingMicros::tickFunc = nullptr;

   // Baseline with long middle delay included
   rollingRateTestTicks = 0;
   baseline.tick();
   rollingRateTestTicks = 30000;
   baseline.tick();
   rollingRateTestTicks = 35000;
   baseline.tick();
   float baseRate = baseline.get();

   // Same pattern but exclude a long middle section
   rollingRateTestTicks = 0;
   excluded.tick();
   rollingRateTestTicks = 5000;
   excluded.pause();
   rollingRateTestTicks = 25000;
   excluded.resume();
   rollingRateTestTicks = 30000;
   excluded.tick();
   rollingRateTestTicks = 35000;
   excluded.tick();
   float excludedRate = excluded.get();

   assertEqual(3.0f / 0.035f, baseRate);
   assertEqual(200.0f, excludedRate);
}

test(RollingRateTest, shouldReturnZeroRateWhenOnlyOneTickHasOccurred)
{
   RollingMicros::tickFunc = getRollingRateTestTicks;
   RollingRate rate(5);
   RollingMicros::tickFunc = nullptr;

   rollingRateTestTicks = 1234;
   rate.tick();

   assertEqual((uint16_t)1, rate.getCount());
   assertEqual(0.0f, rate.get());
}

} // namespace RollingRateTests
