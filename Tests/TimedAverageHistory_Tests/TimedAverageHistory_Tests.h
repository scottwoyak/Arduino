#pragma once

#include <AUnit.h>
#include "TimedAverageHistory.h"

namespace TimedAverageHistoryTests
{

unsigned long timedAverageHistoryTestTicks;

unsigned long getTimedAverageHistoryTestTicks()
{
   return timedAverageHistoryTestTicks;
}

using TimedAverageHistoryMock = TimedAverageHistoryBase<getTimedAverageHistoryTestTicks>;

void setupTimedAverageHistoryTest()
{
   timedAverageHistoryTestTicks = 0;
}

test(TimedAverageHistoryTest, shouldStartEmpty)
{
   setupTimedAverageHistoryTest();
   TimedAverageHistoryMock history(1000, 5);

   assertEqual((size_t)5, history.numBins());
   assertEqual((size_t)0, history.filledBins());
   assertEqual((unsigned long)0, history.rotationCount());
}

test(TimedAverageHistoryTest, shouldAccumulateWithinBin)
{
   setupTimedAverageHistoryTest();
   TimedAverageHistoryMock history(1000, 5);

   timedAverageHistoryTestTicks = 0;
   history.add(1.0f);
   timedAverageHistoryTestTicks = 50;
   history.add(3.0f);

   float values[5];
   unsigned long ages[5];
   size_t count = history.snapshot(values, ages);

   assertEqual((size_t)1, count);
   assertEqual(2.0f, values[0]);
   assertEqual((unsigned long)0, ages[0]);
}

test(TimedAverageHistoryTest, shouldRotateBinsAsTimeAdvances)
{
   setupTimedAverageHistoryTest();
   // 500ms total duration split into 5 bins => 100ms per bin
   TimedAverageHistoryMock history(500, 5);

   timedAverageHistoryTestTicks = 0;
   history.add(1.0f);
   timedAverageHistoryTestTicks = 100;
   history.add(2.0f);
   timedAverageHistoryTestTicks = 200;
   history.add(3.0f);

   assertEqual((size_t)3, history.filledBins());
   assertEqual((unsigned long)2, history.rotationCount());

   float values[5];
   unsigned long ages[5];
   size_t count = history.snapshot(values, ages);

   assertEqual((size_t)3, count);
   assertEqual(3.0f, values[0]);
   assertEqual(2.0f, values[1]);
   assertEqual(1.0f, values[2]);
   assertEqual((unsigned long)0, ages[0]);
   assertEqual((unsigned long)100, ages[1]);
   assertEqual((unsigned long)200, ages[2]);
}

test(TimedAverageHistoryTest, shouldCapFilledBinsAtNumBins)
{
   setupTimedAverageHistoryTest();
   TimedAverageHistoryMock history(300, 3); // 100ms per bin

   for (int i = 0; i < 6; i++)
   {
      timedAverageHistoryTestTicks = i * 100;
      history.add((float)i);
   }

   assertEqual((size_t)3, history.numBins());
   assertEqual((size_t)3, history.filledBins());
   assertEqual((unsigned long)5, history.rotationCount());
}

test(TimedAverageHistoryTest, shouldReportNanForEmptyBins)
{
   setupTimedAverageHistoryTest();
   TimedAverageHistoryMock history(300, 3); // 100ms per bin

   timedAverageHistoryTestTicks = 0;
   history.add(5.0f);
   timedAverageHistoryTestTicks = 250;
   history.refresh();

   float values[3];
   unsigned long ages[3];
   size_t count = history.snapshot(values, ages);

   assertEqual((size_t)3, count);
   assertTrue(isnan(values[0]));
   assertTrue(isnan(values[1]));
   assertEqual(5.0f, values[2]);
}

test(TimedAverageHistoryTest, shouldResetClearsAllState)
{
   setupTimedAverageHistoryTest();
   TimedAverageHistoryMock history(300, 3);

   timedAverageHistoryTestTicks = 0;
   history.add(1.0f);
   timedAverageHistoryTestTicks = 100;
   history.add(2.0f);

   history.reset();

   assertEqual((size_t)0, history.filledBins());
   assertEqual((unsigned long)0, history.rotationCount());
}

} // namespace TimedAverageHistoryTests
