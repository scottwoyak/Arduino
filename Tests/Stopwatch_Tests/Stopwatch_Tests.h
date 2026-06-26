#pragma once

#include <AUnit.h>
#include "Stopwatch.h"

// mock timing for Stopwatch
uint32_t stopwatchTestTicks;
uint32_t getStopwatchTestTicks() {
   return stopwatchTestTicks;
}

test(StopwatchTest, shouldBeRunningWhenInstantiated) {
   Stopwatch sw;
   assertEqual(sw.isRunning(), true);
}

test(StopwatchTest, shouldBeAbleInstantiateStopped) {
   Stopwatch sw(false);
   assertEqual(sw.isRunning(), false);
   assertEqual(sw.elapsedMicros(), 0UL);
}

test(StopwatchTest, defaultPrecisionShouldBeMicros) {
   Stopwatch sw;
   assertEqual(sw.getPrecision(), StopwatchPrecision::Micros);
}

test(StopwatchTest, shouldTrackTimeMicros) {
   StopwatchT<getStopwatchTestTicks> sw(false);

   stopwatchTestTicks = 0;
   sw.start();
   stopwatchTestTicks = 1234;

   assertEqual(sw.elapsedMicros(), (unsigned long)1234);
   assertEqual(sw.elapsedMillis(), 1.234);
   assertEqual(sw.elapsedSecs(), 0.001234);
}

test(StopwatchTest, shouldTrackTimeMillis) {
   StopwatchT<getStopwatchTestTicks> sw(false, StopwatchPrecision::Millis);

   stopwatchTestTicks = 0;
   sw.start();
   stopwatchTestTicks = 1234;

   assertEqual(1234000UL, sw.elapsedMicros());
   assertEqual(sw.elapsedMillis(), 1234.0);
   assertEqual(sw.elapsedSecs(), 1.234);
}

test(StopwatchTest, shouldBeAbleToStop) {
   Stopwatch sw;
   assertEqual(sw.isRunning(), true);
   unsigned long t1 = sw.elapsedMicros();
   delay(1);
   sw.stop();
   unsigned long t2 = sw.elapsedMicros();
   assertEqual(sw.isRunning(), false);
   assertLess(0UL, t1);
   assertLess(t1, t2);
   assertEqual(sw.elapsedMicros(), t2);
}

test(StopwatchTest, shouldBeAbleToResume) {
   Stopwatch sw;
   assertEqual(sw.isRunning(), true);
   sw.stop();
   unsigned long t = sw.elapsedMicros();
   sw.start();
   delay(1);
   assertEqual(sw.isRunning(), true);
   assertLess(t, sw.elapsedMicros());
}

test(StopwatchTest, shouldBeAbleToReset) {
   Stopwatch sw;
   delay(1);
   unsigned long t = sw.elapsedMicros();
   assertLess(0UL, t);
   sw.reset();
   assertLess(sw.elapsedMicros(), t);
}

test(StopwatchTest, shouldBeAbleToResetToNonZeroValueMillis) {
   Stopwatch sw;
   delay(10);
   sw.stop();
   sw.reset(1234);
   assertEqual(sw.elapsedMillis(), 1234.0);
}

test(StopwatchTest, shouldBeAbleToResetToNonZeroValueMicros) {
   Stopwatch sw(StopwatchPrecision::Millis);
   delay(10);
   sw.stop();
   sw.reset(1234);
   assertEqual(sw.elapsedMillis(), 1234.0);
}

test(StopwatchTest, shouldBeRunningIfResetWhileRunning) {
   Stopwatch sw;
   sw.reset();
   assertEqual(sw.isRunning(), true);
}

test(StopwatchTest, shouldBeStoppedIfResetWhileStopped) {
   Stopwatch sw(false);
   sw.reset();
   assertEqual(sw.isRunning(), false);
}

test(StopwatchTest, shouldIgnoreRepeatedStartCallsWhileRunning) {
   StopwatchT<getStopwatchTestTicks> sw(false);

   stopwatchTestTicks = 0;
   sw.start();
   stopwatchTestTicks = 1000;
   sw.start();
   stopwatchTestTicks = 2000;

   assertEqual((unsigned long)2000, sw.elapsedMicros());
}
