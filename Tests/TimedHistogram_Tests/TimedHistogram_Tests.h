#pragma once

#include <AUnit.h>
#include "TimedHistogram.h"
#include "TimedValues.h"

namespace TimedHistogramTests
{
unsigned long timedHistogramTicks;

unsigned long getTimedHistogramTicks()
{
   return timedHistogramTicks;
}

using TimedHistogramMock = TimedHistogramBase<getTimedHistogramTicks>;
using TimedValuesMock = TimedValuesBase<float, getTimedHistogramTicks>;

void setupTimedHistogramTest()
{
   timedHistogramTicks = 0;
}

test(TimedHistogramTest, captureShouldReturnExpectedRangeAndDuration)
{
   setupTimedHistogramTest();

   TimedValuesMock samples(4000, 16);
   timedHistogramTicks = 0;
   samples.set(10.0f);
   timedHistogramTicks = 500;
   samples.set(15.0f);
   timedHistogramTicks = 1000;
   samples.set(12.0f);

   timedHistogramTicks = 1200;

   TimedHistogramMock histogram(4, 4000);
   TimedHistogramSnapshot snapshot = histogram.capture(samples);

   assertEqual(static_cast<size_t>(3), snapshot.valueCount);
   assertEqual(10.0f, snapshot.minValue);
   assertEqual(15.0f, snapshot.maxValue);
   assertEqual(static_cast<uint>(4), snapshot.renderedBins);
   assertEqual(1.2f, snapshot.sampleRangeSeconds);
}

test(TimedHistogramTest, captureShouldExpireValuesIncrementallyAsWindowSlides)
{
   setupTimedHistogramTest();

   TimedValuesMock samples(3000, 16);
   timedHistogramTicks = 0;
   samples.set(10.0f);
   timedHistogramTicks = 1000;
   samples.set(20.0f);
   timedHistogramTicks = 2000;
   samples.set(30.0f);
   timedHistogramTicks = 3000;
   samples.set(40.0f);

   TimedHistogramMock histogram(4, 3000);

   timedHistogramTicks = 3000;
   TimedHistogramSnapshot atStart = histogram.capture(samples);
   assertEqual(static_cast<size_t>(3), atStart.valueCount);
   assertEqual(2.0f, atStart.sampleRangeSeconds);

   timedHistogramTicks = 3500;
   TimedHistogramSnapshot midWindow = histogram.capture(samples);
   assertEqual(static_cast<size_t>(3), midWindow.valueCount);
   assertEqual(2.5f, midWindow.sampleRangeSeconds);
   assertEqual(20.0f, midWindow.minValue);
   assertEqual(40.0f, midWindow.maxValue);

   timedHistogramTicks = 4500;
   TimedHistogramSnapshot tailWindow = histogram.capture(samples);
   assertEqual(static_cast<size_t>(2), tailWindow.valueCount);
   assertEqual(2.5f, tailWindow.sampleRangeSeconds);
   assertEqual(30.0f, tailWindow.minValue);
   assertEqual(40.0f, tailWindow.maxValue);
}

test(TimedHistogramTest, computeNormalizedBinsShouldNormalizeToLargestBin)
{
   setupTimedHistogramTest();

   TimedValuesMock samples(4000, 16);
   timedHistogramTicks = 0;
   samples.set(0.0f);
   timedHistogramTicks = 10;
   samples.set(0.1f);
   timedHistogramTicks = 20;
   samples.set(0.2f);
   timedHistogramTicks = 30;
   samples.set(0.3f);

   timedHistogramTicks = 40;

   TimedHistogramMock histogram(4, 4000);
   TimedHistogramSnapshot snapshot = histogram.capture(samples);

   bool success = histogram.computeNormalizedBins(snapshot);
   assertTrue(success);
   assertEqual(1.0f, histogram.normalizedBinValue(0));
   assertEqual(1.0f, histogram.normalizedBinValue(1));
   assertEqual(1.0f, histogram.normalizedBinValue(2));
   assertEqual(1.0f, histogram.normalizedBinValue(3));
}

test(TimedHistogramTest, computeNormalizedBinsShouldUpdateSmoothlyAsOlderSamplesExpire)
{
   setupTimedHistogramTest();

   TimedValuesMock samples(3000, 16);
   timedHistogramTicks = 0;
   samples.set(0.0f);
   timedHistogramTicks = 1000;
   samples.set(0.0f);
   timedHistogramTicks = 2000;
   samples.set(1.0f);
   timedHistogramTicks = 3000;
   samples.set(1.0f);

   TimedHistogramMock histogram(4, 3000);

   timedHistogramTicks = 3000;
   TimedHistogramSnapshot baseline = histogram.capture(samples);
   assertTrue(histogram.computeNormalizedBins(baseline));
   assertEqual(static_cast<size_t>(3), baseline.valueCount);
   assertEqual(0.5f, histogram.normalizedBinValue(0));
   assertEqual(1.0f, histogram.normalizedBinValue(3));

   timedHistogramTicks = 3501;
   TimedHistogramSnapshot shifted = histogram.capture(samples);
   assertTrue(histogram.computeNormalizedBins(shifted));
   assertEqual(static_cast<size_t>(3), shifted.valueCount);
   assertEqual(0.5f, histogram.normalizedBinValue(0));
   assertEqual(1.0f, histogram.normalizedBinValue(3));

   timedHistogramTicks = 4501;
   TimedHistogramSnapshot concentrated = histogram.capture(samples);
   assertTrue(histogram.computeNormalizedBins(concentrated));
   assertEqual(static_cast<size_t>(2), concentrated.valueCount);
   assertEqual(1.0f, histogram.normalizedBinValue(0));
   assertEqual(0.0f, histogram.normalizedBinValue(3));
}

test(TimedHistogramTest, computeNormalizedBinsShouldClearPreviousStateWhenWindowBecomesEmpty)
{
   setupTimedHistogramTest();

   TimedValuesMock samples(1000, 8);
   timedHistogramTicks = 0;
   samples.set(5.0f);
   timedHistogramTicks = 100;
   samples.set(6.0f);

   TimedHistogramMock histogram(4, 1000);

   timedHistogramTicks = 100;
   TimedHistogramSnapshot populated = histogram.capture(samples);
   assertTrue(histogram.computeNormalizedBins(populated));
   assertEqual(1.0f, histogram.normalizedBinValue(0));
   assertEqual(1.0f, histogram.normalizedBinValue(3));

   timedHistogramTicks = 1501;
   TimedHistogramSnapshot empty = histogram.capture(samples);
   assertTrue(histogram.computeNormalizedBins(empty));
   assertEqual(static_cast<size_t>(0), empty.valueCount);

   for (uint i = 0; i < 4; i++)
   {
	  assertEqual(0.0f, histogram.normalizedBinValue(i));
   }
}

test(TimedHistogramTest, computeNormalizedBinsShouldHandleEmptyInput)
{
   setupTimedHistogramTest();

   TimedValuesMock samples(2000, 8);
   TimedHistogramMock histogram(5, 2000);

   TimedHistogramSnapshot snapshot = histogram.capture(samples);
   bool success = histogram.computeNormalizedBins(snapshot);

   assertTrue(success);
   assertEqual(static_cast<size_t>(0), snapshot.valueCount);
   assertEqual(static_cast<uint>(5), snapshot.renderedBins);
   for (uint i = 0; i < 5; i++)
   {
	  assertEqual(0.0f, histogram.normalizedBinValue(i));
   }
}

} // namespace TimedHistogramTests
