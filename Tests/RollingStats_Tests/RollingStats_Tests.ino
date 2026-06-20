#include <AUnit.h>
#include <RollingStats.h>

test(shouldStartEmpty)
{
   RollingStats stats(3);

   assertTrue(isnan(stats.min()));
   assertTrue(isnan(stats.max()));
   assertTrue(isnan(stats.average()));
   assertTrue(isnan(stats.range()));
   assertEqual((size_t)0, stats.count());
}

test(shouldTrackBasicStats)
{
   RollingStats stats(3);

   stats.set(1.0f);
   stats.set(2.0f);
   stats.set(3.0f);

   assertNear(1.0f, stats.min(), 0.0001f);
   assertNear(3.0f, stats.max(), 0.0001f);
   assertNear(2.0f, stats.average(), 0.0001f);
   assertNear(2.0f, stats.range(), 0.0001f);
   assertEqual((size_t)3, stats.count());
}

test(shouldRollWindow)
{
   RollingStats stats(3);

   stats.set(1.0f);
   stats.set(2.0f);
   stats.set(3.0f);
   stats.set(4.0f); // window now [4,2,3]

   assertNear(2.0f, stats.min(), 0.0001f);
   assertNear(4.0f, stats.max(), 0.0001f);
   assertNear(3.0f, stats.average(), 0.0001f);
   assertNear(2.0f, stats.range(), 0.0001f);
   assertEqual((size_t)3, stats.count());
}

test(shouldIgnoreNonFiniteValues)
{
   RollingStats stats(3);

   stats.set(1.0f);
   stats.set(INFINITY);
   stats.set(NAN);

   assertNear(1.0f, stats.min(), 0.0001f);
   assertNear(1.0f, stats.max(), 0.0001f);
   assertNear(1.0f, stats.average(), 0.0001f);
   assertNear(0.0f, stats.range(), 0.0001f);
   assertEqual((size_t)1, stats.count());
}

void setup()
{
   Serial.begin(115200);
   while (!Serial);
}

void loop()
{
   aunit::TestRunner::run();
}
