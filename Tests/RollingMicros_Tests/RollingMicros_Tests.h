#pragma once

#include <AUnit.h>
#include "RollingMicros.h"

namespace RollingMicrosTests
{

unsigned long rollingMicrosTestTicks;
unsigned long getRollingMicrosTestTicks()
{
   return rollingMicrosTestTicks;
}

test(RollingMicrosTest, RollingMicros_shouldStartEmpty)
{
   RollingMicros::tickFunc = getRollingMicrosTestTicks;
   RollingMicros rm(5);
   RollingMicros::tickFunc = nullptr;

   assertEqual((uint16_t)0, rm.getCount());
   assertEqual((unsigned long)0, rm.getLastMicros());
   assertEqual((unsigned long)0, rm.getElapsedMicros());
}

test(RollingMicrosTest, shouldTrackTicksAndElapsedMicros)
{
   RollingMicros::tickFunc = getRollingMicrosTestTicks;
   RollingMicros rm(5);
   RollingMicros::tickFunc = nullptr;

   rollingMicrosTestTicks = 100;
   rm.tick();
   rollingMicrosTestTicks = 1100;
   rm.tick();

   assertEqual((uint16_t)2, rm.getCount());
   assertEqual((unsigned long)1000, rm.getElapsedMicros());
}

test(RollingMicrosTest, shouldWrapCountAtWindowSize)
{
   RollingMicros::tickFunc = getRollingMicrosTestTicks;
   RollingMicros rm(3);
   RollingMicros::tickFunc = nullptr;

   rollingMicrosTestTicks = 0;
   rm.tick();
   rollingMicrosTestTicks = 10;
   rm.tick();
   rollingMicrosTestTicks = 20;
   rm.tick();
   rollingMicrosTestTicks = 30;
   rm.tick();

   assertEqual((uint16_t)3, rm.getCount());
}

test(RollingMicrosTest, resetShouldClearState)
{
   RollingMicros::tickFunc = getRollingMicrosTestTicks;
   RollingMicros rm(4);
   RollingMicros::tickFunc = nullptr;

   rollingMicrosTestTicks = 500;
   rm.tick();
   rollingMicrosTestTicks = 700;
   rm.tick();

   assertEqual((uint16_t)2, rm.getCount());

   rm.reset();

   assertEqual((uint16_t)0, rm.getCount());
   assertEqual((unsigned long)0, rm.getLastMicros());
   assertEqual((unsigned long)0, rm.getElapsedMicros());
}

test(RollingMicrosTest, shouldExcludeTimeBetweenPauseAndResume)
{
   RollingMicros::tickFunc = getRollingMicrosTestTicks;
   RollingMicros rm(5);
   RollingMicros::tickFunc = nullptr;

   rollingMicrosTestTicks = 1000;
   rm.tick();

   rollingMicrosTestTicks = 2000;
   rm.pause();

   rollingMicrosTestTicks = 7000; // 5000 micros excluded
   rm.resume();

   rollingMicrosTestTicks = 9000;
   rm.tick();

   // effective second tick is 9000 - 5000 = 4000
   // elapsed = 4000 - 1000 = 3000
   assertEqual((unsigned long)3000, rm.getElapsedMicros());
}

test(RollingMicrosTest, shouldExposeFirstAndLastMicros)
{
   RollingMicros::tickFunc = getRollingMicrosTestTicks;
   RollingMicros rm(5);
   RollingMicros::tickFunc = nullptr;

   rollingMicrosTestTicks = 100;
   rm.tick();
   rollingMicrosTestTicks = 250;
   rm.tick();

   assertEqual((unsigned long)100, rm.getFirstMicros());
   assertEqual((unsigned long)250, rm.getLastMicros());
   assertEqual((unsigned long)150, rm.getElapsedMicros());
}

} // namespace RollingMicrosTests
