#include <AUnit.h>
#include "SerialX.h"
#include "Stats_Tests.h"

void setup()
{
   SerialX::begin();
}

void loop() 
{
   aunit::TestRunner::run();
}
