#pragma once

#include <AUnit.h>
#include "Util.h"
#include <cstdint>

namespace UtilTests
{

test(UtilTest, shouldGetSpan)
{
   uint32_t start = 1000;
   uint32_t end = 1500;

   uint32_t span = Util::getSpan(start, end);

   assertEqual((uint32_t)500, span);
}

test(UtilTest, shouldGetSpanWithWrapping)
{
   uint32_t start = UINT32_MAX;
   uint32_t end = 10;

   uint32_t span = Util::getSpan(start, end);

   assertEqual((uint32_t)11, span);
}

test(UtilTest, shouldReturnZeroWhenStartEqualsEnd)
{
   uint32_t start = 1000;
   uint32_t end = 1000;

   uint32_t span = Util::getSpan(start, end);

   assertEqual((uint32_t)0, span);
}

test(UtilTest, shouldHandleMaximumSpanWithoutWrapping)
{
   uint32_t start = 0;
   uint32_t end = UINT32_MAX;

   uint32_t span = Util::getSpan(start, end);

   assertEqual(UINT32_MAX, span);
}

test(UtilTest, shouldHandleSmallWraparound)
{
   uint32_t start = 4294967294UL;
   uint32_t end = 0;

   uint32_t span = Util::getSpan(start, end);

   assertEqual((uint32_t)2, span);
}

test(UtilTest, shouldHandleWraparoundAtBoundary)
{
   uint32_t start = UINT32_MAX;
   uint32_t end = 0;

   uint32_t span = Util::getSpan(start, end);

   assertEqual((uint32_t)1, span);
}

test(UtilTest, shouldHandleLargeWraparound)
{
   uint32_t start = 100;
   uint32_t end = 99;

   uint32_t span = Util::getSpan(start, end);

   assertEqual(UINT32_MAX, span);
}

} // namespace UtilTests
