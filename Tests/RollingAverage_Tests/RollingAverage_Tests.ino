
#include <AUnit.h>
#include <RollingAverage.h>
#include <cmath>

test(shouldStartAsNAN)
{
   RollingAverage avg(3);

   assertTrue(isnan(avg.get()));
   assertTrue(isnan(avg.getLast()));
}

test(shouldReturnFalseWhenSizeIsZero)
{
   RollingAverage avg(0);

   assertFalse(avg.set(1.0f));
   assertTrue(isnan(avg.get()));
   assertTrue(isnan(avg.getLast()));
}

test(shouldComputeAverageForInitialSamples)
{
   RollingAverage avg(3);

   assertTrue(avg.set(1.0f));
   assertNear(1.0f, avg.get(), 0.0001f);

   assertTrue(avg.set(2.0f));
   assertNear(1.5f, avg.get(), 0.0001f);

   assertTrue(avg.set(3.0f));
   assertNear(2.0f, avg.get(), 0.0001f);
}

test(shouldRollWindowWhenFull)
{
   RollingAverage avg(3);

   avg.set(1.0f);
   avg.set(2.0f);
   avg.set(3.0f);
   assertNear(2.0f, avg.get(), 0.0001f);

   avg.set(4.0f); // window becomes [4,2,3]
   assertNear(3.0f, avg.get(), 0.0001f);

   avg.set(5.0f); // window becomes [4,5,3]
   assertNear(4.0f, avg.get(), 0.0001f);
}

test(shouldReturnLastInsertedValue)
{
   RollingAverage avg(3);

   avg.set(10.0f);
   assertNear(10.0f, avg.getLast(), 0.0001f);

   avg.set(20.0f);
   assertNear(20.0f, avg.getLast(), 0.0001f);
}

test(shouldIgnoreNaNAndInfiniteValuesInAverage)
{
   RollingAverage avg(3);

   avg.set(1.0f);
   avg.set(2.0f);
   assertNear(1.5f, avg.get(), 0.0001f);

   avg.set(NAN);
   assertNear(1.5f, avg.get(), 0.0001f);

   avg.set(INFINITY);
   assertNear(2.0f, avg.get(), 0.0001f);

   avg.set(-INFINITY);
   assertTrue(isnan(avg.get()));
}

test(shouldDropAverageWhenFiniteValueIsOverwrittenByNaN)
{
   RollingAverage avg(2);

   avg.set(2.0f);
   avg.set(4.0f);
   assertNear(3.0f, avg.get(), 0.0001f);

   avg.set(NAN); // overwrites 2.0f
   assertNear(4.0f, avg.get(), 0.0001f);

   avg.set(NAN); // overwrites 4.0f
   assertTrue(isnan(avg.get()));
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
