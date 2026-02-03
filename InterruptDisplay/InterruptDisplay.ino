
#include "Feather.h"
#include "SerialX.h"

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

Format pinFormat("##");
Format countFormat("###");

void setup()
{
   SerialX::begin();
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

   feather.setTextSize(2);
   feather.display.setTextWrap(true);
   feather.println("Press the button to switch types", Color::HEADING);
   feather.println();

   if (feather.buttonA.wasPressed())
   {
      choice++;
      if (choice >= NUM_CHOICES)
      {
         choice = 0;
      }

      count = 0;
      attachInterrupt();
   }

   feather.setTextSize(3);
   feather.print("Pin: ");
   feather.println(pin, pinFormat, Color::VALUE);
   feather.print(choices[choice]);
   feather.print(": ");
   feather.println(count, countFormat, Color::VALUE);

   feather.displayDisplay();
}


