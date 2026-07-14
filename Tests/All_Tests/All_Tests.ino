#include <AUnit.h>
#include "../BufferedTimeSeries_Tests/BufferedTimeSeries_Tests.h"
#include "../Histogram_Tests/Histogram_Tests.h"
#include "../RollingAverage_Tests/RollingAverage_Tests.h"
#include "../RollingMicros_Tests/RollingMicros_Tests.h"
#include "../RollingRate_Tests/RollingRate_Tests.h"
#include "../RollingStats_Tests/RollingStats_Tests.h"
#include "../RollingValues_Tests/RollingValues_Tests.h"
#include "../Stats_Tests/Stats_Tests.h"
#include "../Stopwatch_Tests/Stopwatch_Tests.h"
#include "../Tick_Tests/Tick_Tests.h"
#include "../TimedAverage_Tests/TimedAverage_Tests.h"
#include "../TimedAverageHistory_Tests/TimedAverageHistory_Tests.h"
#include "../TimedBin_Tests/TimedBin_Tests.h"
#include "../TimedMinMax_Tests/TimedMinMax_Tests.h"
#include "../TimedRate_Tests/TimedRate_Tests.h"
#include "../TimedStats_Tests/TimedStats_Tests.h"
#include "../TimedValues_Tests/TimedValues_Tests.h"
#include "../Timer_Tests/Timer_Tests.h"
#include "../Util_Tests/Util_Tests.h"

void setup()
{
   Serial.begin(115200);
   while (!Serial);
}

void loop()
{
   aunit::TestRunner::run();
}
