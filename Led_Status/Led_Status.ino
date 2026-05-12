#include "Led.h"

Led ledBlue(3);
Led ledGreen(6);
Led ledRed(4);

void setup()
{
   ledBlue.begin();
   ledGreen.begin();
   ledRed.begin();

ledRed.turnOn();

/*
   ledGreen.setLevel(0.5f);

   ledBlue.setBlinkInterval(500);
   delay(2000);

   ledBlue.setBlinkInterval(0);
   ledGreen.setBlinkInterval(500);
   delay(2000);


   ledGreen.setBlinkInterval(0);


   ledRed.setBlinkInterval(100);
*/
}

// the loop function runs over and over again until power down or reset
void loop()
{
}
