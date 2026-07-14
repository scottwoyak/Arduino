#pragma once

#include <AUnit.h>
#include "TimedBin.h"

namespace TimedBinTests
{

// mock timing for TimedBin
unsigned long timedBinTestMockMicros;
static unsigned long getTimedBinTestMockMicros()
{
   return timedBinTestMockMicros;
}

using TimedBinMock = TimedBinBase<getTimedBinTestMockMicros>;

void setupTimedBinTest()
{
   timedBinTestMockMicros = 0;
}

void loadTimedBinTestEntries(TimedBinMock* bin, uint microsForEntries[], uint numEntries)
{
   for (int i = 0; i < numEntries; i++)
   {
      timedBinTestMockMicros = microsForEntries[i];
      bin->add();
   }
}

test(TimedBinTest, shouldComputeTotals)
{
   setupTimedBinTest();

   // all entries are in the first bin
   uint microsForEntries[] =
   {
      5,
      6,
      7,
      8,
      9,
   };
   constexpr auto numEntries = sizeof(microsForEntries) / sizeof(uint);

   constexpr auto DURATION_MS = 50;
   constexpr auto NUM_BINS = 5;
   TimedBinMock bin(DURATION_MS, NUM_BINS);

   loadTimedBinTestEntries(&bin, microsForEntries, numEntries);

   assertEqual((uint16_t)5, bin.getCount());
   assertEqual((uint16_t)5, bin.getBinCount(0));
   assertEqual((uint16_t)0, bin.getBinCount(1));
   assertEqual((uint16_t)0, bin.getBinCount(2));
   assertEqual((uint16_t)0, bin.getBinCount(3));
   assertEqual((uint16_t)0, bin.getBinCount(4));
   assertEqual((uint16_t)0, bin.getBinCount(-1));

}

test(TimedBinTest, shouldUtilizeMultipleBins)
{
   setupTimedBinTest();

   // one entry in each bin
   uint microsForEntries[] =
   {
      5 * 1000,
      15 * 1000,
      25 * 1000,
      35 * 1000,
      45 * 1000,
   };
   constexpr auto numEntries = sizeof(microsForEntries) / sizeof(uint);

   constexpr auto DURATION_MS = 50;
   constexpr auto NUM_BINS = 5;
   TimedBinMock bin(DURATION_MS, NUM_BINS);

   loadTimedBinTestEntries(&bin, microsForEntries, numEntries);

   assertEqual((uint16_t)5, bin.getCount());
   assertEqual((uint16_t)1, bin.getBinCount(0));
   assertEqual((uint16_t)1, bin.getBinCount(1));
   assertEqual((uint16_t)1, bin.getBinCount(2));
   assertEqual((uint16_t)1, bin.getBinCount(3));
   assertEqual((uint16_t)1, bin.getBinCount(4));
   assertEqual((uint16_t)0, bin.getBinCount(-1));
}

test(TimedBinTest, shouldAdvanceBins)
{
   setupTimedBinTest();

   // skip bins between entries
   uint microsForEntries[] =
   {
      5 * 1000,
      45 * 1000,
   };
   constexpr auto numEntries = sizeof(microsForEntries) / sizeof(uint);

   constexpr auto DURATION_MS = 50;
   constexpr auto NUM_BINS = 5;
   TimedBinMock bin(DURATION_MS, NUM_BINS);

   loadTimedBinTestEntries(&bin, microsForEntries, numEntries);

   assertEqual((uint16_t)2, bin.getCount());
   assertEqual((uint16_t)1, bin.getBinCount(0));
   assertEqual((uint16_t)0, bin.getBinCount(1));
   assertEqual((uint16_t)0, bin.getBinCount(2));
   assertEqual((uint16_t)0, bin.getBinCount(3));
   assertEqual((uint16_t)1, bin.getBinCount(4));
   assertEqual((uint16_t)0, bin.getBinCount(-1));
}

// should zero out bins as they transition
test(TimedBinTest, shouldZeroOutBins)
{
   setupTimedBinTest();

   // one entry in each bin
   uint microsForEntries[] =
   {
      5 * 1000,
      15 * 1000,
      25 * 1000,
      35 * 1000,
      45 * 1000,
   };
   constexpr auto numEntries = sizeof(microsForEntries) / sizeof(uint);

   constexpr auto DURATION_MS = 50;
   constexpr auto NUM_BINS = 5;
   TimedBinMock bin(DURATION_MS, NUM_BINS);

   // first fill each bin
   loadTimedBinTestEntries(&bin, microsForEntries, numEntries);

   assertEqual((uint16_t)5, bin.getCount());
   assertEqual((uint16_t)1, bin.getBinCount(0));
   assertEqual((uint16_t)1, bin.getBinCount(1));
   assertEqual((uint16_t)1, bin.getBinCount(2));
   assertEqual((uint16_t)1, bin.getBinCount(3));
   assertEqual((uint16_t)1, bin.getBinCount(4));
   assertEqual((uint16_t)0, bin.getBinCount(-1));

   // advance long enough to expire the first bin
   timedBinTestMockMicros = (DURATION_MS + 1) * 1000;
   assertEqual((uint16_t)5, bin.getCount());
   assertEqual((uint16_t)0, bin.getBinCount(0));
   assertEqual((uint16_t)1, bin.getBinCount(1));
   assertEqual((uint16_t)1, bin.getBinCount(2));
   assertEqual((uint16_t)1, bin.getBinCount(3));
   assertEqual((uint16_t)1, bin.getBinCount(4));
   assertEqual((uint16_t)1, bin.getBinCount(-1));

   // expire all bins - should then be zero
   timedBinTestMockMicros = (2 * DURATION_MS + 1) * 1000;
   assertEqual((uint16_t)0, bin.getCount());
}

// should smoothly transition between bins as time advances
test(TimedBinTest, shouldSmoothlyTransitionBins)
{
   setupTimedBinTest();

   // 5 entries in the first bin
   uint microsForEntries[] =
   {
      1,
      2,
      3,
      4,
      5
   };
   constexpr auto numEntries = sizeof(microsForEntries) / sizeof(uint);

   constexpr auto DURATION_MS = 50;
   constexpr auto NUM_BINS = 5;
   TimedBinMock bin(DURATION_MS, NUM_BINS);

   // fill bins
   loadTimedBinTestEntries(&bin, microsForEntries, numEntries);

   assertEqual((uint16_t)5, bin.getCount());
   assertEqual((uint16_t)5, bin.getBinCount(0));
   assertEqual((uint16_t)0, bin.getBinCount(1));
   assertEqual((uint16_t)0, bin.getBinCount(2));
   assertEqual((uint16_t)0, bin.getBinCount(3));
   assertEqual((uint16_t)0, bin.getBinCount(4));
   assertEqual((uint16_t)0, bin.getBinCount(-1));

   // gradually expire the first bin
   constexpr auto BIN_DURATION_MS = DURATION_MS / NUM_BINS;
   timedBinTestMockMicros = (DURATION_MS + 0.21*BIN_DURATION_MS) * 1000;
   assertEqual((uint16_t)4, bin.getCount());

   timedBinTestMockMicros = (DURATION_MS + 0.41 * BIN_DURATION_MS) * 1000;
   assertEqual((uint16_t)3, bin.getCount());

   timedBinTestMockMicros = (DURATION_MS + 0.61 * BIN_DURATION_MS) * 1000;
   assertEqual((uint16_t)2, bin.getCount());

   timedBinTestMockMicros = (DURATION_MS + 0.81 * BIN_DURATION_MS) * 1000;
   assertEqual((uint16_t)1, bin.getCount());

   timedBinTestMockMicros = (DURATION_MS + 1.01 * BIN_DURATION_MS) * 1000;
   assertEqual((uint16_t)0, bin.getCount());
}

test(TimedBinTest, shouldHonorBeginAsNewStartTime)
{
   setupTimedBinTest();

   constexpr auto DURATION_MS = 50;
   constexpr auto NUM_BINS = 5;
   TimedBinMock bin(DURATION_MS, NUM_BINS);

   timedBinTestMockMicros = 0;
   bin.add();

   timedBinTestMockMicros = 60000;
   bin.begin();
   bin.add();

   assertEqual((uint16_t)2, bin.getCount());
   assertEqual((uint16_t)2, bin.getBinCount(0));
   assertEqual((uint16_t)0, bin.getBinCount(1));
   assertEqual((uint16_t)0, bin.getBinCount(2));
   assertEqual((uint16_t)0, bin.getBinCount(3));
   assertEqual((uint16_t)0, bin.getBinCount(4));
}

} // namespace TimedBinTests
