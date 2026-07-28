#include <AUnit.h>
#include "SerialX.h"
#include "Histogram_Tests.h"

void setup()
{
   SerialX::begin();
}

void loop() 
{
   aunit::TestRunner::run();
}
