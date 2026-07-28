#include "LGX_ST7796S.h"
#include <AUnit.h>
#include "ScatterPlotSeries_Tests.h"
#include "SerialX.h"

void setup()
{
   SerialX::begin();
}

void loop()
{
   aunit::TestRunner::run();
}
