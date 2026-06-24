#pragma once

#include <AUnit.h>
#include "TimedBin.h"

// mock timing for TimedBin
unsigned long timedBinTestMockMicros;
static unsigned long getTimedBinTestMockMicros()
{
   return timedBinTestMockMicros;
}

void setupTimedBinTest()
{
   timedBinTestMockMicros = 0;
   TimedBin::microsFunc = getTimedBinTestMockMicros;
}

void loadTimedBinTestEntries(TimedBin* bin, uint microsForEntries[], uint numEntries)
{
   for (int i = 0; i < numEntries; i++)
   {
      timedBinTestMockMicros = microsForEntries[i];
      bin->add();
   }
}

test(shouldComputeTotals)
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
   TimedBin bin(DURATION_MS, NUM_BINS);

   loadTimedBinTestEntries(&bin, microsForEntries, numEntries);

   assertEqual(5u, bin.getCount());
   assertEqual(5u, bin.getBinCount(0));
   assertEqual(0u, bin.getBinCount(1));
   assertEqual(0u, bin.getBinCount(2));
   assertEqual(0u, bin.getBinCount(3));
   assertEqual(0u, bin.getBinCount(4));
   assertEqual(0u, bin.getBinCount(-1));

}

test(shouldUtilizeMultipleBins)
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
   TimedBin bin(DURATION_MS, NUM_BINS);

   loadTimedBinTestEntries(&bin, microsForEntries, numEntries);

   assertEqual(5u, bin.getCount());
   assertEqual(1u, bin.getBinCount(0));
   assertEqual(1u, bin.getBinCount(1));
   assertEqual(1u, bin.getBinCount(2));
   assertEqual(1u, bin.getBinCount(3));
   assertEqual(1u, bin.getBinCount(4));
   assertEqual(0u, bin.getBinCount(-1));
}

test(shouldAdvanceBins)
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
   TimedBin bin(DURATION_MS, NUM_BINS);

   loadTimedBinTestEntries(&bin, microsForEntries, numEntries);

   assertEqual(2u, bin.getCount());
   assertEqual(1u, bin.getBinCount(0));
   assertEqual(0u, bin.getBinCount(1));
   assertEqual(0u, bin.getBinCount(2));
   assertEqual(0u, bin.getBinCount(3));
   assertEqual(1u, bin.getBinCount(4));
   assertEqual(0u, bin.getBinCount(-1));
}

// should zero out bins as they transition
test(shouldZeroOutBins)
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
   TimedBin bin(DURATION_MS, NUM_BINS);

   // first fill each bin
   loadTimedBinTestEntries(&bin, microsForEntries, numEntries);

   assertEqual(5u, bin.getCount());
   assertEqual(1u, bin.getBinCount(0));
   assertEqual(1u, bin.getBinCount(1));
   assertEqual(1u, bin.getBinCount(2));
   assertEqual(1u, bin.getBinCount(3));
   assertEqual(1u, bin.getBinCount(4));
   assertEqual(0u, bin.getBinCount(-1));

   // advance long enough to expire the first bin
   timedBinTestMockMicros = (DURATION_MS + 1) * 1000;
   assertEqual(5u, bin.getCount());
   assertEqual(0u, bin.getBinCount(0));
   assertEqual(1u, bin.getBinCount(1));
   assertEqual(1u, bin.getBinCount(2));
   assertEqual(1u, bin.getBinCount(3));
   assertEqual(1u, bin.getBinCount(4));
   assertEqual(1u, bin.getBinCount(-1));

   // expire all bins - should then be zero
   timedBinTestMockMicros = (2 * DURATION_MS + 1) * 1000;
   assertEqual(0u, bin.getCount());
}

// should smoothly transition between bins as time advances
test(shouldSmoothlyTransitionBins)
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
   TimedBin bin(DURATION_MS, NUM_BINS);

   // fill bins
   loadTimedBinTestEntries(&bin, microsForEntries, numEntries);

   assertEqual(5u, bin.getCount());
   assertEqual(5u, bin.getBinCount(0));
   assertEqual(0u, bin.getBinCount(1));
   assertEqual(0u, bin.getBinCount(2));
   assertEqual(0u, bin.getBinCount(3));
   assertEqual(0u, bin.getBinCount(4));
   assertEqual(0u, bin.getBinCount(-1));

   // gradually expire the first bin
   constexpr auto BIN_DURATION_MS = DURATION_MS / NUM_BINS;
   timedBinTestMockMicros = (DURATION_MS + 0.21*BIN_DURATION_MS) * 1000;
   assertEqual(4u, bin.getCount());

   timedBinTestMockMicros = (DURATION_MS + 0.41 * BIN_DURATION_MS) * 1000;
   assertEqual(3u, bin.getCount());

   timedBinTestMockMicros = (DURATION_MS + 0.61 * BIN_DURATION_MS) * 1000;
   assertEqual(2u, bin.getCount());

   timedBinTestMockMicros = (DURATION_MS + 0.81 * BIN_DURATION_MS) * 1000;
   assertEqual(1u, bin.getCount());

   timedBinTestMockMicros = (DURATION_MS + 1.01 * BIN_DURATION_MS) * 1000;
   assertEqual(0u, bin.getCount());
}

test(shouldHonorBeginAsNewStartTime)
{
   setupTimedBinTest();

   constexpr auto DURATION_MS = 50;
   constexpr auto NUM_BINS = 5;
   TimedBin bin(DURATION_MS, NUM_BINS);

   timedBinTestMockMicros = 0;
   bin.add();

   timedBinTestMockMicros = 60000;
   bin.begin();
   bin.add();

   assertEqual(2u, bin.getCount());
   assertEqual(2u, bin.getBinCount(0));
   assertEqual(0u, bin.getBinCount(1));
   assertEqual(0u, bin.getBinCount(2));
   assertEqual(0u, bin.getBinCount(3));
   assertEqual(0u, bin.getBinCount(4));
}
