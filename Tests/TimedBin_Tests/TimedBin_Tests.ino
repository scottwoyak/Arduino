
#include <AUnit.h>
#include <TimedBin.h>

// mock timing for TimedBin
unsigned long _mockMicros;
static unsigned long getMockMicros()
{
   return _mockMicros;
}

void loadEntries(TimedBin* bin, uint microsForEntries[], uint numEntries)
{
   for (int i = 0; i < numEntries; i++)
   {
	  _mockMicros = microsForEntries[i];
	  bin->add();
   }
}

test(shouldComputeTotals)
{
   _mockMicros = 0;

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

   loadEntries(&bin, microsForEntries, numEntries);

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
   _mockMicros = 0;

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

   loadEntries(&bin, microsForEntries, numEntries);

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
   _mockMicros = 0;

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

   loadEntries(&bin, microsForEntries, numEntries);

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
   _mockMicros = 0;

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
   loadEntries(&bin, microsForEntries, numEntries);

   assertEqual(5u, bin.getCount());
   assertEqual(1u, bin.getBinCount(0));
   assertEqual(1u, bin.getBinCount(1));
   assertEqual(1u, bin.getBinCount(2));
   assertEqual(1u, bin.getBinCount(3));
   assertEqual(1u, bin.getBinCount(4));
   assertEqual(0u, bin.getBinCount(-1));

   // advance long enough to expire the first bin
   constexpr auto BIN_DURATION_MS = DURATION_MS / NUM_BINS;
   _mockMicros = (DURATION_MS + 1) * 1000;
   assertEqual(5u, bin.getCount());
   assertEqual(0u, bin.getBinCount(0));
   assertEqual(1u, bin.getBinCount(1));
   assertEqual(1u, bin.getBinCount(2));
   assertEqual(1u, bin.getBinCount(3));
   assertEqual(1u, bin.getBinCount(4));
   assertEqual(1u, bin.getBinCount(-1));

   // expire all bins - should then be zero
   _mockMicros = (2 * DURATION_MS + 1) * 1000;
   assertEqual(0u, bin.getCount());
}

// should smoothly transition between bins as time advances
test(shouldSmoothlyTransitionBins)
{
   _mockMicros = 0;

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
   loadEntries(&bin, microsForEntries, numEntries);

   assertEqual(5u, bin.getCount());
   assertEqual(5u, bin.getBinCount(0));
   assertEqual(0u, bin.getBinCount(1));
   assertEqual(0u, bin.getBinCount(2));
   assertEqual(0u, bin.getBinCount(3));
   assertEqual(0u, bin.getBinCount(4));
   assertEqual(0u, bin.getBinCount(-1));

   // gradually expire the first bin
   constexpr auto BIN_DURATION_MS = DURATION_MS / NUM_BINS;
   _mockMicros = (DURATION_MS + 0.21*BIN_DURATION_MS) * 1000;
   assertEqual(4u, bin.getCount());

   _mockMicros = (DURATION_MS + 0.41 * BIN_DURATION_MS) * 1000;
   assertEqual(3u, bin.getCount());

   _mockMicros = (DURATION_MS + 0.61 * BIN_DURATION_MS) * 1000;
   assertEqual(2u, bin.getCount());

   _mockMicros = (DURATION_MS + 0.81 * BIN_DURATION_MS) * 1000;
   assertEqual(1u, bin.getCount());

   _mockMicros = (DURATION_MS + 1.01 * BIN_DURATION_MS) * 1000;
   assertEqual(0u, bin.getCount());
}

void setup()
{
   Serial.begin(115200);
   while (!Serial);

   TimedBin::microsFunc = getMockMicros;
}

void loop()
{
   aunit::TestRunner::run();
}
