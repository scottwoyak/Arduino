#include <AUnit.h>
#include "SerialX.h"
#include "RollingStats_Tests.h"

void setup()
{
   SerialX::begin();
}

void loop()
{
   aunit::TestRunner::run();
}
