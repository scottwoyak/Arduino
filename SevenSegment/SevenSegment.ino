#include <Arduino.h>
#include <SevenSegment.h>
#include <RateTracker.h>
#include <Timer.h>

// Module connection pins (Digital Pins)
#define CLK 41
#define DIO 42

RateTracker rate;
Timer timer(1000);

SevenSegment display(CLK, DIO);

void setup()
{
   Serial.begin(115200);
   display.setBrightness(7);
}

int count = -100;

void loop()
{
   rate.tick();

   display.display(count / 10.0f, 1);
   count++;

   if (count == 10000)
   {
      count = -100;
   }

   if (timer.ready())
   {
      Serial.println("Rate: " + String(rate.getRate()) + " per sec");
   }
}
