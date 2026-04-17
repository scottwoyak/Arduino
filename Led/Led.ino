#include "Led.h"

Led led(BUILTIN_LED);

void setup()
{
   led.begin();
   led.setBlinkInterval(200);
}

// the loop function runs over and over again until power down or reset
void loop()
{
}
