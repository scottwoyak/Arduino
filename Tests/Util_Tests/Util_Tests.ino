#include <AUnit.h>
#include "Util_Tests.h"

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
