#pragma once

#include <AUnit.h>
#include "TimedValues.h"

namespace TimedValuesTests
{

unsigned long timedValuesTestTicks;

unsigned long getTimedValuesTestTicks()
{
   return timedValuesTestTicks;
}

using TimedValuesMock = TimedValuesBase<float, getTimedValuesTestTicks>;

void setupTimedValuesTest()
{
   timedValuesTestTicks = 0;
}

test(TimedValuesTest, shouldStoreNewestFirst)
{
   setupTimedValuesTest();
   TimedValuesMock values(1000, 4);

   timedValuesTestTicks = 10;
   values.set(1.0f);
   timedValuesTestTicks = 20;
   values.set(2.0f);
   timedValuesTestTicks = 30;
   values.set(3.0f);

   assertEqual(static_cast<size_t>(3), values.count());
   assertEqual(3.0f, values.get(0));
   assertEqual(2.0f, values.get(1));
   assertEqual(1.0f, values.get(2));
}

test(TimedValuesTest, shouldExpireOldValues)
{
   setupTimedValuesTest();
   TimedValuesMock values(100, 4);

   timedValuesTestTicks = 0;
   values.set(10.0f);
   timedValuesTestTicks = 40;
   values.set(20.0f);
   timedValuesTestTicks = 80;
   values.set(30.0f);

   timedValuesTestTicks = 120;
   assertEqual(static_cast<size_t>(2), values.count());
   assertEqual(30.0f, values.get(0));
   assertEqual(20.0f, values.get(1));
}

test(TimedValuesTest, snapshotShouldReturnNewestFirstWithAges)
{
   setupTimedValuesTest();
   TimedValuesMock values(500, 8);

   timedValuesTestTicks = 100;
   values.set(1.0f);
   timedValuesTestTicks = 200;
   values.set(2.0f);
   timedValuesTestTicks = 250;
   values.set(3.0f);

   timedValuesTestTicks = 300;

   float snapshotValues[4] = { 0 };
   unsigned long snapshotAges[4] = { 0 };

   size_t copied = values.snapshot(snapshotValues, snapshotAges, 4);

   assertEqual(static_cast<size_t>(3), copied);
   assertEqual(3.0f, snapshotValues[0]);
   assertEqual(2.0f, snapshotValues[1]);
   assertEqual(1.0f, snapshotValues[2]);
   assertEqual(static_cast<unsigned long>(50), snapshotAges[0]);
   assertEqual(static_cast<unsigned long>(100), snapshotAges[1]);
   assertEqual(static_cast<unsigned long>(200), snapshotAges[2]);
}

test(TimedValuesTest, shouldResetAndChangeDuration)
{
   setupTimedValuesTest();
   TimedValuesMock values(1000, 4);

   timedValuesTestTicks = 1;
   values.set(10.0f);
   timedValuesTestTicks = 2;
   values.set(20.0f);

   assertEqual(static_cast<size_t>(2), values.count());

   values.reset();
   assertEqual(static_cast<size_t>(0), values.count());

   timedValuesTestTicks = 10;
   values.set(50.0f);
   values.setDurationMs(25);

   assertEqual(static_cast<unsigned long>(25), values.durationMs());
   assertEqual(static_cast<size_t>(0), values.count());
}

test(TimedValuesTest, getOutOfRangeShouldReturnDefault)
{
   setupTimedValuesTest();
   TimedValuesMock values(1000, 2);

   timedValuesTestTicks = 10;
   values.set(42.0f);

   assertEqual(0.0f, values.get(1));
   assertEqual(0.0f, values.get(100));
}

test(TimedValuesTest, ageOutOfRangeShouldReturnDuration)
{
   setupTimedValuesTest();
   TimedValuesMock values(250, 2);

   timedValuesTestTicks = 10;
   values.set(1.0f);
   timedValuesTestTicks = 20;

   assertEqual(static_cast<unsigned long>(10), values.ageMs(0));
   assertEqual(static_cast<unsigned long>(250), values.ageMs(1));
}

test(TimedValuesTest, snapshotShouldHonorMaxCount)
{
   setupTimedValuesTest();
   TimedValuesMock values(1000, 4);

   timedValuesTestTicks = 10;
   values.set(1.0f);
   timedValuesTestTicks = 20;
   values.set(2.0f);
   timedValuesTestTicks = 30;
   values.set(3.0f);

   float snapshotValues[2] = { 0 };
   unsigned long snapshotAges[2] = { 0 };

   timedValuesTestTicks = 40;
   size_t copied = values.snapshot(snapshotValues, snapshotAges, 2);

   assertEqual(static_cast<size_t>(2), copied);
   assertEqual(3.0f, snapshotValues[0]);
   assertEqual(2.0f, snapshotValues[1]);
   assertEqual(static_cast<unsigned long>(10), snapshotAges[0]);
   assertEqual(static_cast<unsigned long>(20), snapshotAges[1]);
}

test(TimedValuesTest, shouldGrowCapacityWhenFull)
{
   setupTimedValuesTest();
   TimedValuesMock values(1000, 1);

   timedValuesTestTicks = 1;
   values.set(10.0f);
   timedValuesTestTicks = 2;
   values.set(20.0f);

   assertTrue(values.size() >= static_cast<size_t>(2));
   assertEqual(static_cast<size_t>(2), values.count());
   assertEqual(20.0f, values.get(0));
   assertEqual(10.0f, values.get(1));
}

test(TimedValuesTest, constructorAndDurationShouldClampToAtLeastOneMs)
{
   setupTimedValuesTest();

   TimedValuesMock values(0, 1);
   assertEqual(static_cast<unsigned long>(1), values.durationMs());

   values.setDurationMs(0);
   assertEqual(static_cast<unsigned long>(1), values.durationMs());
}

} // namespace TimedValuesTests
