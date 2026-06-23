#include <Arduino.h>
#include "SevenSegment.h"
#include "RollingRate.h"
#include "SerialX.h"
#include "Timer.h"

RollingRate rate;
Timer timer(1000);

ISevenSegment* display = nullptr;

void setup()
{
   SerialX::begin();
   Wire.begin();

   display = SevenSegmentFactory::create();
   if (display != nullptr)
   {
      display->begin();
      display->setBrightness(1.0f);
   }
}

int count = -100;

void loop()
{
   rate.tick();

   if (display != nullptr)
   {
      display->print(count / 10.0f, 1);
   }
   count++;

   if (count == 10000)
   {
      count = -100;
   }

   if (timer.ready())
   {
      Serial.println("Rate: " + String(rate.get()) + " per sec");
   }
}
