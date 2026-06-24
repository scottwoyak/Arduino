#include <AUnit.h>
#include "TimedStats_Tests.h"

void setup()
{
   Serial.begin(115200);
   while (!Serial);

   TimedStats::tickFunc = getTimedStatsTestTicks;
}

void loop()
{
   aunit::TestRunner::run();
}
