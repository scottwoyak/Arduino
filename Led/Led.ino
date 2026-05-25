#include <LED.h>

LED led(BUILTIN_LED);

void setup()
{
   led.begin();
   led.blink(200);
}

void loop()
{
}
