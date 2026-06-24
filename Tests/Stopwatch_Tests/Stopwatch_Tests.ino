#include <AUnit.h>
#include "Stopwatch_Tests.h"

void setup() {
   Serial.begin(115200);
   while (!Serial)
   {
      delay(100);
   }

   delay(1000);
   Serial.println("setup()");
}

void loop() {
   aunit::TestRunner::run();
}
