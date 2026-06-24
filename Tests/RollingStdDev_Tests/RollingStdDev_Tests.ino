#include <AUnit.h>
#include "RollingStdDev_Tests.h"

void setup()
{
   delay(1000);
   Serial.begin(115200);
   while (!Serial);
}

void loop()
{
   aunit::TestRunner::run();
}
