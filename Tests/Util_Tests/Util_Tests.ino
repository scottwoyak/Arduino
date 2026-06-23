
#include <AUnit.h>
#include "Util.h"
#include <cstdint>

test(shouldGetSpan)
{
   uint32_t start = 1000;
   uint32_t end = 1500;

   uint32_t span = Util::getSpan(start, end);

   assertEqual((uint32_t)500, span);
}

test(shouldGetSpanWithWrapping)
{
   uint32_t start = UINT32_MAX;
   uint32_t end = 10;

   uint32_t span = Util::getSpan(start, end);

   assertEqual((uint32_t)11, span);
}

test(shouldReturnZeroWhenStartEqualsEnd)
{
   uint32_t start = 1000;
   uint32_t end = 1000;

   uint32_t span = Util::getSpan(start, end);

   assertEqual((uint32_t)0, span);
}

test(shouldHandleMaximumSpanWithoutWrapping)
{
   uint32_t start = 0;
   uint32_t end = UINT32_MAX;

   uint32_t span = Util::getSpan(start, end);

   assertEqual(UINT32_MAX, span);
}

test(shouldHandleSmallWraparound)
{
   uint32_t start = 4294967294UL;
   uint32_t end = 0;

   uint32_t span = Util::getSpan(start, end);

   assertEqual((uint32_t)2, span);
}

test(shouldHandleWraparoundAtBoundary)
{
   uint32_t start = UINT32_MAX;
   uint32_t end = 0;

   uint32_t span = Util::getSpan(start, end);

   assertEqual((uint32_t)1, span);
}

test(shouldHandleLargeWraparound)
{
   uint32_t start = 100;
   uint32_t end = 99;

   uint32_t span = Util::getSpan(start, end);

   assertEqual(UINT32_MAX, span);
}

void setup()
{
   // Standard Arduino test setups usually wait for Serial
   delay(1000);
   Serial.begin(115200);
   while (!Serial);
}

void loop()
{
   // Run the test runner. It completes automatically and stops execution loop.
   aunit::TestRunner::run();
}
