#pragma once

#include <AUnit.h>
#include "Timer.h"
#include "Util.h"

unsigned long timerTestTicks = 0;

unsigned long timerTestTime()
{
   return timerTestTicks;
}

template<unsigned long(*TimeFunc)()>
class TimerSecsMock : public TimerBase<TimeFunc>
{
public:
   explicit TimerSecsMock(float durationSecs)
      : TimerBase<TimeFunc>((unsigned long)(durationSecs * 1000.0f))
   {
   }

   float remaining() const
   {
      return TimerBase<TimeFunc>::remaining() / 1000.0f;
   }
};

void setupTimerTest()
{
   timerTestTicks = 0;
}

test(TimerTest, Timer_CanInstantiate)
{
   setupTimerTest();
   TimerBase<timerTestTime> timer(100);
   assertTrue(true);
}

test(TimerTest, Timer_ShouldNotBeReadyInitially)
{
   setupTimerTest();
   TimerBase<timerTestTime> timer(60000);
   assertEqual(false, timer.ready());
}

test(TimerTest, Timer_RemainingAtStartIsDuration)
{
   setupTimerTest();
   unsigned long duration = 1000;
   TimerBase<timerTestTime> timer(duration);

   assertEqual(duration, timer.remaining());
}

test(TimerTest, Timer_RemainingDecreases)
{
   setupTimerTest();
   unsigned long duration = 1000;
   TimerBase<timerTestTime> timer(duration);

   constexpr auto elapsed = 50;
   timerTestTicks += elapsed;
   assertEqual(duration - elapsed, timer.remaining());
   assertFalse(timer.ready());
}

test(TimerTest, Timer_ShouldBeReadyAfterDurationElapses)
{
   setupTimerTest();
   unsigned long duration = 1000;
   TimerBase<timerTestTime> timer(duration);

   timerTestTicks += duration;
   assertTrue(timer.ready());
}

test(TimerTest, Timer_ShouldAutomaticallyRestart)
{
   setupTimerTest();
   unsigned long duration = 1000;
   TimerBase<timerTestTime> timer(duration);

   timerTestTicks += duration;
   assertTrue(timer.ready());
   assertFalse(timer.ready());

   timerTestTicks += duration;
   assertTrue(timer.ready());
}

test(TimerTest, TimeMicros_ShouldSupportMicros)
{
   setupTimerTest();
   constexpr auto duration = 1000;
   TimerBase<timerTestTime> timer(duration);

   timerTestTicks += duration / 2;

   assertFalse(timer.ready());
   assertEqual((unsigned long)(duration / 2), timer.remaining());

   timerTestTicks += duration / 2;
   assertTrue(timer.ready());
}

test(TimerTest, TimeMillis_ShouldSupportMillis)
{
   setupTimerTest();
   constexpr auto duration = 100;
   TimerBase<timerTestTime> timer(duration);

   timerTestTicks += duration / 2;

   assertFalse(timer.ready());
   assertEqual((unsigned long)(duration / 2), timer.remaining());

   timerTestTicks += duration / 2;
   assertTrue(timer.ready());
}

test(TimerTest, TimeSecs_ShouldSupportMillis)
{
   setupTimerTest();
   constexpr auto duration = 0.5f;
   TimerSecsMock<timerTestTime> timer(duration);

   timerTestTicks += 250;

   assertFalse(timer.ready());
   assertNear(0.25f, timer.remaining(), 0.0001f);

   timerTestTicks += 250;
   assertTrue(timer.ready());
}
