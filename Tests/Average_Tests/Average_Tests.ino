
#include <AUnit.h>
#include <Average.h>

test(shouldComputeAverages)
{
   Average avg;

   avg.add(1);
   avg.add(2);
   avg.add(3);

   assertEqual(2.0, avg.get());
   assertEqual(3, avg.count());
}

test(shouldStartAsNAN)
{
   Average avg;

   assertTrue(isnan(avg.get()));
   assertEqual(0, avg.count());
}

test(shouldRemoveValues)
{
   Average avg;

   avg.add(1);
   avg.add(2);
   avg.add(3);

   assertEqual(2.0, avg.get());
   assertEqual(3, avg.count());

   avg.remove(1);
   assertEqual(2.5, avg.get());
   assertEqual(2, avg.count());
}

test(shouldNotAllowMoreValueToBeRemovedThanHaveBeenAdded)
{
   Average avg;

   avg.add(1);
   avg.add(2);
   avg.add(3);

   assertEqual(2.0, avg.get());
   assertEqual(3, avg.count());

   avg.remove(1);
   avg.remove(2);
   avg.remove(3);
   assertTrue(isnan(avg.get()));
   assertEqual(0, avg.count());

   avg.remove(3);
   assertTrue(isnan(avg.get()));
   assertEqual(0, avg.count());
}


test(shouldIgnoreNANValues)
{
   Average avg;

   avg.add(1);
   avg.add(2);
   avg.add(3);

   assertEqual(2.0, avg.get());
   assertEqual(3, avg.count());

   avg.add(NAN);
   assertEqual(2.0, avg.get());
   assertEqual(3, avg.count());

   avg.remove(NAN);
   assertEqual(2.0, avg.get());
   assertEqual(3, avg.count());
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
