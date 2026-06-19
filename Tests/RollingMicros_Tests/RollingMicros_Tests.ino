
#include <AUnit.h>
#include <RollingMicros.h>

unsigned long ticks;
unsigned long getTicks()
{
   return ticks;
}

test(shouldStartEmpty)
{
   RollingMicros::tickFunc = getTicks;
   RollingMicros rm(5);
   RollingMicros::tickFunc = nullptr;

   assertEqual((uint16_t)0, rm.getCount());
   assertEqual((unsigned long)0, rm.getLastMicros());
   assertEqual((unsigned long)0, rm.getElapsedMicros());
}

test(shouldTrackTicksAndElapsedMicros)
{
   RollingMicros::tickFunc = getTicks;
   RollingMicros rm(5);
   RollingMicros::tickFunc = nullptr;

   ticks = 100;
   rm.tick();
   ticks = 1100;
   rm.tick();

   assertEqual((uint16_t)2, rm.getCount());
   assertEqual((unsigned long)1000, rm.getElapsedMicros());
}

test(shouldWrapCountAtWindowSize)
{
   RollingMicros::tickFunc = getTicks;
   RollingMicros rm(3);
   RollingMicros::tickFunc = nullptr;

   ticks = 0;
   rm.tick();
   ticks = 10;
   rm.tick();
   ticks = 20;
   rm.tick();
   ticks = 30;
   rm.tick();

   assertEqual((uint16_t)3, rm.getCount());
}

test(resetShouldClearState)
{
   RollingMicros::tickFunc = getTicks;
   RollingMicros rm(4);
   RollingMicros::tickFunc = nullptr;

   ticks = 500;
   rm.tick();
   ticks = 700;
   rm.tick();

   assertEqual((uint16_t)2, rm.getCount());

   rm.reset();

   assertEqual((uint16_t)0, rm.getCount());
   assertEqual((unsigned long)0, rm.getLastMicros());
   assertEqual((unsigned long)0, rm.getElapsedMicros());
}

test(shouldExcludeTimeBetweenPauseAndResume)
{
   RollingMicros::tickFunc = getTicks;
   RollingMicros rm(5);
   RollingMicros::tickFunc = nullptr;

   ticks = 1000;
   rm.tick();

   ticks = 2000;
   rm.pause();

   ticks = 7000; // 5000 micros excluded
   rm.resume();

   ticks = 9000;
   rm.tick();

   // effective second tick is 9000 - 5000 = 4000
   // elapsed = 4000 - 1000 = 3000
   assertEqual((unsigned long)3000, rm.getElapsedMicros());
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
