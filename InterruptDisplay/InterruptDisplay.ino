
#include "Feather.h"

Feather feather;

constexpr uint8_t pin = 9;
volatile long count = 0;

static void onInterrupt()
{
   count = count + 1;
}

const char* choices[] = {
   "Rising",
   "Falling",
   "Change",
};

int choice = 0;
int NUM_CHOICES = sizeof(choices) / sizeof(choices[0]);

void setup()
{
   Serial.begin(115200);
   feather.begin();

   attachInterrupt(digitalPinToInterrupt(pin), onInterrupt, RISING);
}

static void attachInterrupt()
{
   switch (choice)
   {
   case 0:
      attachInterrupt(digitalPinToInterrupt(pin), onInterrupt, RISING);
      break;

   case 1:
      attachInterrupt(digitalPinToInterrupt(pin), onInterrupt, FALLING);
      break;

   case 2:
      attachInterrupt(digitalPinToInterrupt(pin), onInterrupt, CHANGE);
      break;
   }
}

void loop()
{
   feather.setCursor(0, 0);

   feather.setTextSize(TextSize::SMALL);
   feather.println("Press the button to switch types");
   feather.println();

   if (feather.buttonA.wasPressed())
   {
      feather.buttonA.reset();

      choice++;
      if (choice >= NUM_CHOICES)
      {
         choice = 0;
      }

      count = 0;
      attachInterrupt();
   }

   feather.setTextSize(TextSize::MEDIUM);
   feather.print("Pin: ");
   feather.println(pin, 2, ValueColor);
   feather.print(choices[choice]);
   feather.print(": ");
   feather.println(count, 6, ValueColor);

#if defined(ADAFRUIT_FEATHER_M0)
   feather.display.display();
#endif
}


