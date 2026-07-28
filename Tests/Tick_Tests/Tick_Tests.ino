#include <AUnit.h>
#include "SerialX.h"
#include "Tick_Tests.h"

void setup() {
   SerialX::begin();
   Serial.println("setup()");
}

void loop() {
   aunit::TestRunner::run();
}
