
#include <AUnit.h>
#include <RollingRate.h>

unsigned long ticks;
unsigned long getTicks()
{
   return ticks;
}

test(shouldStartAtZeroRate)
{
   RollingMicros::tickFunc = getTicks;
   RollingRate rate(5);
   RollingMicros::tickFunc = nullptr;

   assertNear(0.0f, rate.get(), 0.0001f);
   assertEqual((uint16_t)0, rate.getCount());
}

test(shouldIncreaseCountWithTicks)
{
   RollingMicros::tickFunc = getTicks;
   RollingRate rate(5);
   RollingMicros::tickFunc = nullptr;

   ticks = 0;
   rate.tick();
   ticks = 1;
   rate.tick();
   ticks = 2;
   rate.tick();

   assertEqual((uint16_t)3, rate.getCount());
}

test(shouldComputeExpectedRate)
{
   RollingMicros::tickFunc = getTicks;
   RollingRate rate(10);
   RollingMicros::tickFunc = nullptr;

   ticks = 0;
   rate.tick();
   ticks = 100000;
   rate.tick();
   ticks = 200000;
   rate.tick();

   assertNear(15.0f, rate.get(), 0.001f); // 3 ticks / 0.2 sec
}

test(resetShouldClearCountAndRate)
{
   RollingMicros::tickFunc = getTicks;
   RollingRate rate(10);
   RollingMicros::tickFunc = nullptr;

   ticks = 0;
   rate.tick();
   ticks = 5000;
   rate.tick();

   assertMore(rate.get(), 0.0f);
   assertEqual((uint16_t)2, rate.getCount());

   rate.reset();

   assertNear(0.0f, rate.get(), 0.0001f);
   assertEqual((uint16_t)0, rate.getCount());
}

test(excludingDisplayTimeShouldIncreaseMeasuredRate)
{
   RollingMicros::tickFunc = getTicks;
   RollingRate baseline(10);
   RollingRate excluded(10);
   RollingMicros::tickFunc = nullptr;

   // Baseline with long middle delay included
   ticks = 0;
   baseline.tick();
   ticks = 30000;
   baseline.tick();
   ticks = 35000;
   baseline.tick();
   float baseRate = baseline.get();

   // Same pattern but exclude a long middle section
   ticks = 0;
   excluded.tick();
   ticks = 5000;
   excluded.pause();
   ticks = 25000;
   excluded.resume();
   ticks = 30000;
   excluded.tick();
   ticks = 35000;
   excluded.tick();
   float excludedRate = excluded.get();

   assertMore(excludedRate, baseRate);
}

void setup()
{
   Serial.begin(115200);
   while (!Serial);
}

void loop() 
{
   aunit::TestRunner::run();
}
