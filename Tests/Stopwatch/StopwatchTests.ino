#include <AUnit.h>
#include <Stopwatch.h>

// mock timing for Stopwatch
unsigned long ticks;
unsigned long getTicks() {
   return ticks;
}

test(shouldBeRunningWhenInstantiated) {
   Stopwatch sw;
   assertEqual(sw.isRunning(), true);
}

test(shouldBeAbleInstantiateStopped) {
   Stopwatch sw(false);
   assertEqual(sw.isRunning(), false);
   assertEqual(sw.elapsedMicros(), 0UL);
}

test(defaultPrecisionShouldBeMicros) {
   Stopwatch sw;
   assertEqual(sw.getPrecision(), StopwatchPrecision::Micros);
}

test(shouldTrackTimeMicros) {
   Stopwatch::tickFunc = getTicks;
   Stopwatch sw(false);
   Stopwatch::tickFunc = nullptr;

   ticks = 0;
   sw.start();
   ticks = 1234;

   assertEqual(sw.elapsedMicros(), (unsigned long)1234);
   assertEqual(sw.elapsedMillis(), 1.234);
   assertEqual(sw.elapsedSecs(), 0.001234);
}

test(shouldTrackTimeMillis) {
   Stopwatch::tickFunc = getTicks;
   Stopwatch sw(false, StopwatchPrecision::Millis);
   Stopwatch::tickFunc = nullptr;

   ticks = 0;
   sw.start();
   ticks = 1234;

   assertEqual(1234000UL, sw.elapsedMicros());
   assertEqual(sw.elapsedMillis(), 1234.0);
   assertEqual(sw.elapsedSecs(), 1.234);
}

test(shouldBeAbleToStop) {
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

test(shouldBeAbleToResume) {
   Stopwatch sw;
   assertEqual(sw.isRunning(), true);
   sw.stop();
   unsigned long t = sw.elapsedMicros();
   sw.start();
   delay(1);
   assertEqual(sw.isRunning(), true);
   assertLess(t, sw.elapsedMicros());
}

test(shouldBeAbleToReset) {
   Stopwatch sw;
   delay(1);
   unsigned long t = sw.elapsedMicros();
   assertLess(0UL, t);
   sw.reset();
   assertLess(sw.elapsedMicros(), t);
}

test(shouldBeAbleToResetToNonZeroValueMillis) {
   Stopwatch sw;
   delay(10);
   sw.stop();
   sw.reset(1234);
   assertEqual(sw.elapsedMillis(), 1234.0);
}

test(shouldBeAbleToResetToNonZeroValueMicros) {
   Stopwatch sw(StopwatchPrecision::Millis);
   delay(10);
   sw.stop();
   sw.reset(1234);
   assertEqual(sw.elapsedMillis(), 1234.0);
}

test(shouldBeRunningIfResetWhileRunning) {
   Stopwatch sw;
   sw.reset();
   assertEqual(sw.isRunning(), true);
}

test(shouldBeStoppedIfResetWhileStopped) {
   Stopwatch sw(false);
   sw.reset();
   assertEqual(sw.isRunning(), false);
}


void setup() {
   Serial.begin(115200);
   while (!Serial);
}

void loop() {
   aunit::TestRunner::run();
}
