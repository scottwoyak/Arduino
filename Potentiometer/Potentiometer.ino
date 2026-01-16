//
// Example to demonstrate the usage of a potentiometer
// 
// o Potentiometer is connected to A0
// o Values are printed to serial output and demonstrate jitter and
//   how the class can remove it.
//
#include "Potentiometer.h"

Potentiometer p(PIN_A0);
ScaledPotentiometer p2(PIN_A0, 0, 100);

void setup()
{
   // start serial port
   Serial.begin(115200);

   // wait a few seconds for the serial monitor to open
   while (millis() < 2000 && !Serial)
      ;
   delay(500);
}

// the loop function runs over and over again until power down or reset
void loop()
{
   Serial.print(p.read());
   Serial.print(" ");
   Serial.print(p2.read());
   Serial.println();
}
