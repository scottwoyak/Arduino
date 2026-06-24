#include <AUnit.h>
#include "Timer_Tests.h"

void setup()
{
   Serial.begin(115200);
   while (!Serial)
   {
      ; // wait for serial port to connect
   }
}

void loop()
{
   aunit::TestRunner::run();
}
