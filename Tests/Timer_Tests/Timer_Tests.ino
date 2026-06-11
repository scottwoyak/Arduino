#include <AUnit.h>
#include <Timer.h>
#include <Util.h>

unsigned int _mockTicks = 0;
unsigned long mockTime()
{
   return _mockTicks;
}


test(Timer_CanInstantiate)
{
   // Simple test to verify Timer can be created
   TimerBase<mockTime> timer(100);

   // Just verify it exists and doesn't crash
   assertTrue(true);
}

test(Timer_ShouldNotBeReadyInitially)
{
   TimerBase<mockTime> timer(60000);  // 60 second interval

   // Immediately check, should not be ready
   bool isReady = timer.ready();
   assertEqual(false, isReady);
}

test(Timer_RemainingAtStartIsDuration)
{
   unsigned long DURATION = 1000;
   TimerBase<mockTime> timer(DURATION);

   assertEqual(DURATION, timer.remaining());
}

test(Timer_RemainingDecreases)
{
   unsigned long DURATION = 1000;
   TimerBase<mockTime> timer(DURATION);

   constexpr auto ELAPSED = 50;
   _mockTicks += ELAPSED;
   assertEqual(DURATION - ELAPSED, timer.remaining());

   assertFalse(timer.ready());
}

test(Timer_ShouldBeReadyAfterDurationElapses)
{
   unsigned long DURATION = 1000;
   TimerBase<mockTime> timer(DURATION);

   // Advance time by the duration
   _mockTicks += DURATION;

   // Timer should now be ready
   assertTrue(timer.ready());
}

test(Timer_ShouldAutomaticallyRestart)
{
   unsigned long DURATION = 1000;
   TimerBase<mockTime> timer(DURATION);

   // Advance time by the duration to trigger ready
   _mockTicks += DURATION;

   // Timer should be ready
   assertTrue(timer.ready());

   // Immediately check again - should not be ready since it should have restarted
   assertFalse(timer.ready());

   // Advance time by another duration
   _mockTicks += DURATION;

   // Timer should be ready again, confirming it restarted
   assertTrue(timer.ready());
}

test(TimeMicros_ShouldSupportMicros)
{
   constexpr auto DURATION = 1000; // 1ms in microseconds
   TimerMicros timer(DURATION);

   // Advance half way
   delayMicroseconds(DURATION / 2);

   assertFalse(timer.ready());
   assertLessOrEqual(timer.remaining(), (unsigned long) (0.5 * DURATION));
   assertMore(timer.remaining(), (unsigned long) (0.4 * DURATION));

   delayMicroseconds(DURATION / 2);
   assertTrue(timer.ready());
}

test(TimeMillis_ShouldSupportMillis)
{
   constexpr auto DURATION = 100;
   TimerMillis timer(DURATION);

   // Advance half way
   delay(DURATION / 2);

   assertFalse(timer.ready());
   assertLessOrEqual(timer.remaining(), (unsigned long) (0.5 * DURATION));
   assertMore(timer.remaining(), (unsigned long) (0.4 * DURATION));

   delay(DURATION / 2);
   assertTrue(timer.ready());
}

test(TimeSecs_ShouldSupportMillis)
{
   constexpr auto DURATION = 0.5;
   TimerSecs timer(DURATION);

   // Advance half way
   delay(1000 * DURATION / 2);

   assertFalse(timer.ready());
   assertLessOrEqual(timer.remaining(), (float) (0.5 * DURATION));
   assertMore(timer.remaining(), (float) (0.4 * DURATION));

   delay(1000 * DURATION / 2);
   assertTrue(timer.ready());
}

void setup()
{
   Serial.begin(115200);
   while (!Serial)
   {
      ; // wait for serial port to connect
   }
}

void loop()
{
   aunit::TestRunner::run();
}
