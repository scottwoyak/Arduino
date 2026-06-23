#include "SerialX.h"

void setup()
{
   uint startMillis = millis();
   Serial.begin(115200);
   while (!Serial);
   uint endMillis = millis();

   Serial.println("Serial Test");
   Serial.println("Start: " + String(startMillis));
   Serial.println("Serial Opened: " + String(endMillis));
   Serial.println("Delta: " + String(endMillis - startMillis) + " ms");
}

uint32_t counter = 0;

void loop()
{
   Serial.println(counter++);
   delay(1000);
}
