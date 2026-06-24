#include <AUnit.h>
#include "BufferedTimeSeries_Tests.h"

void setup()
{
   Serial.begin(115200);
   while (!Serial);
}

void loop()
{
   aunit::TestRunner::run();
}
