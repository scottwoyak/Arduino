#include <AUnit.h>
#include "SerialX.h"
#include "TimedAverage_Tests.h"

void setup()
{
   SerialX::begin();
}

void loop()
{
   aunit::TestRunner::run();
}
