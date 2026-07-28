#include <AUnit.h>
#include "SerialX.h"
#include "TimedValues_Tests.h"

void setup()
{
   SerialX::begin();
}

void loop()
{
   aunit::TestRunner::run();
}
