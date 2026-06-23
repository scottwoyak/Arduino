
#include <AUnit.h>
#include "Stats.h"
#include <cmath>

test(shouldComputeAverages)
{
   Stats avg;

   avg.add(1);
   avg.add(2);
   avg.add(3);

   assertEqual(2.0, avg.get());
   assertEqual(3u, avg.count());
   assertEqual(1.0, avg.min());
   assertEqual(3.0, avg.max());
}

test(shouldResetToEmpty)
{
   Stats avg;

   avg.add(1);
   avg.add(2);

   assertEqual(1.5, avg.get());
   assertEqual(2u, avg.count());

   avg.reset();

   assertTrue(isnan(avg.get()));
   assertTrue(isnan(avg.min()));
   assertTrue(isnan(avg.max()));
   assertEqual(0u, avg.count());
}

test(shouldStartAsNAN)
{
   Stats avg;

   assertTrue(isnan(avg.get()));
   assertTrue(isnan(avg.min()));
   assertTrue(isnan(avg.max()));
   assertEqual(0u, avg.count());
}

test(shouldRemoveValues)
{
   Stats avg;

   avg.add(1);
   avg.add(2);
   avg.add(3);

   assertEqual(2.0, avg.get());
   assertEqual(3u, avg.count());

   avg.remove(1);
   assertEqual(2.5, avg.get());
   assertEqual(2u, avg.count());
   assertTrue(isnan(avg.min()));
   assertTrue(isnan(avg.max()));
}

test(shouldNotAllowMoreValueToBeRemovedThanHaveBeenAdded)
{
   Stats avg;

   avg.add(1);
   avg.add(2);
   avg.add(3);

   assertEqual(2.0, avg.get());
   assertEqual(3u, avg.count());

   avg.remove(1);
   avg.remove(2);
   avg.remove(3);
   assertTrue(isnan(avg.get()));
   assertEqual(0u, avg.count());

   avg.remove(3);
   assertTrue(isnan(avg.get()));
   assertEqual(0u, avg.count());
}


test(shouldIgnoreNANValues)
{
   Stats avg;

   avg.add(1);
   avg.add(2);
   avg.add(3);

   assertEqual(2.0, avg.get());
   assertEqual(3u, avg.count());

   avg.add(NAN);
   assertEqual(2.0, avg.get());
   assertEqual(3u, avg.count());

   avg.remove(NAN);
   assertEqual(2.0, avg.get());
   assertEqual(3u, avg.count());
}

test(shouldIgnoreInfiniteValues)
{
   Stats avg;

   avg.add(1);
   avg.add(2);

   assertEqual(1.5, avg.get());
   assertEqual(2u, avg.count());

   avg.add(INFINITY);
   assertEqual(1.5, avg.get());
   assertEqual(2u, avg.count());

   avg.add(-INFINITY);
   assertEqual(1.5, avg.get());
   assertEqual(2u, avg.count());

   avg.remove(INFINITY);
   assertEqual(1.5, avg.get());
   assertEqual(2u, avg.count());

   avg.remove(-INFINITY);
   assertEqual(1.5, avg.get());
   assertEqual(2u, avg.count());
}

test(shouldNotChangeWhenRemovingFromEmpty)
{
   Stats avg;

   avg.remove(1.0f);
   assertTrue(isnan(avg.get()));
   assertEqual(0u, avg.count());
}

test(roundTripStability)
{
   Stats avg;

   for (int i = 1; i <= 10; ++i) avg.add(i);

   float saved = avg.get();
   size_t savedCount = avg.count();

   for (int i = 1; i <= 10; ++i) avg.remove(i);

   assertTrue(isnan(avg.get()));
   assertEqual(0u, avg.count());

   for (int i = 1; i <= 10; ++i) avg.add(i);
   assertEqual(saved, avg.get());
   assertEqual(savedCount, avg.count());
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
