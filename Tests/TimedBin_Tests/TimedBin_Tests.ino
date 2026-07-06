#include <AUnit.h>
#include "TimedBin_Tests.h"

void setup()
{
   Serial.begin(115200);
   while (!Serial);
}

void loop()
{
   aunit::TestRunner::run();
}
