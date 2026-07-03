#pragma once

#include <AUnit.h>
#include "Histogram.h"
#include <cmath>

namespace HistogramTests
{

test(HistogramTest, shouldBuildBinsFromFiniteValues)
{
   const float values[] = { 1.0f, 2.0f, 3.0f, 4.0f };
   Histogram histogram(values, 4, 4);

   assertEqual(static_cast<size_t>(4), histogram.count());
   assertEqual(1.0f, histogram.min());
   assertEqual(4.0f, histogram.max());
   assertEqual(3.0f, histogram.range());
   assertEqual(static_cast<size_t>(4), histogram.bins());

   assertEqual(static_cast<uint32_t>(1), histogram.bin(0));
   assertEqual(static_cast<uint32_t>(1), histogram.bin(1));
   assertEqual(static_cast<uint32_t>(1), histogram.bin(2));
   assertEqual(static_cast<uint32_t>(1), histogram.bin(3));
}

test(HistogramTest, shouldIgnoreNaNAndInfiniteValues)
{
   const float values[] = { 1.0f, NAN, INFINITY, -INFINITY, 2.0f };
   Histogram histogram(values, 5, 3);

   assertEqual(static_cast<size_t>(2), histogram.count());
   assertEqual(1.0f, histogram.min());
   assertEqual(2.0f, histogram.max());
   assertEqual(static_cast<uint32_t>(1), histogram.bin(0));
   assertEqual(static_cast<uint32_t>(0), histogram.bin(1));
   assertEqual(static_cast<uint32_t>(1), histogram.bin(2));
}

test(HistogramTest, shouldPlaceFlatDataIntoFirstBin)
{
   const float values[] = { 5.0f, 5.0f, 5.0f };
   Histogram histogram(values, 3, 4);

   assertEqual(static_cast<size_t>(3), histogram.count());
   assertEqual(5.0f, histogram.min());
   assertEqual(5.0f, histogram.max());
   assertEqual(0.0f, histogram.range());

   assertEqual(static_cast<uint32_t>(3), histogram.bin(0));
   assertEqual(static_cast<uint32_t>(0), histogram.bin(1));
   assertEqual(static_cast<uint32_t>(0), histogram.bin(2));
   assertEqual(static_cast<uint32_t>(0), histogram.bin(3));
}

test(HistogramTest, emptyInputShouldHaveNoValues)
{
   Histogram histogram(nullptr, 0, 4);

   assertEqual(static_cast<size_t>(0), histogram.count());
   assertTrue(std::isnan(histogram.min()));
   assertTrue(std::isnan(histogram.max()));
   assertTrue(std::isnan(histogram.range()));
   assertEqual(static_cast<uint32_t>(0), histogram.bin(0));
   assertEqual(static_cast<uint32_t>(0), histogram.bin(10));
}

test(HistogramTest, recommendBinCountShouldStayWithinBounds)
{
   const float values[] = { 10.0f, 11.0f, 12.0f, 13.0f, 14.0f, 15.0f };
   size_t bins = Histogram::recommendBinCount(values, 6, 3, 6);

   assertTrue(bins >= static_cast<size_t>(3));
   assertTrue(bins <= static_cast<size_t>(6));
}

} // namespace HistogramTests
