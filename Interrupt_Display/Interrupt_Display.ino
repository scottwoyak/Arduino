
#include "Feather.h"
#include "SerialX.h"

Feather feather;

constexpr uint8_t risingPin = A3;
constexpr uint8_t fallingPin = A4;
constexpr uint8_t changePin = A5;

volatile unsigned long risingCount = 0;
volatile unsigned long fallingCount = 0;
volatile unsigned long changeCount = 0;
volatile unsigned long risingMicros = 0;
volatile unsigned long fallingMicros = 0;
volatile unsigned long changeMicros = 0;

static void onRising()
{
   risingMicros = micros();
   risingCount = risingCount + 1;
}
static void onFalling()
{
   fallingMicros = micros();
   fallingCount = fallingCount + 1;
}
static void onChange()
{
   changeMicros = micros();
   changeCount = changeCount + 1;
}

Format pinFormat("(##): ");
Format countFormat("#######");

void setup()
{
   SerialX::begin();
   feather.begin();

   pinMode(risingPin, INPUT_PULLUP);
   pinMode(fallingPin, INPUT_PULLUP);
   pinMode(changePin, INPUT_PULLUP);

   attachInterrupt(digitalPinToInterrupt(risingPin), onRising, RISING);
   attachInterrupt(digitalPinToInterrupt(fallingPin), onFalling, FALLING);
   attachInterrupt(digitalPinToInterrupt(changePin), onChange, CHANGE);
}

void loop()
{
   feather.setCursor(0, 0);

   if (feather.buttonA.wasPressed())
   {
      noInterrupts();
      risingCount = 0;
      fallingCount = 0;
      changeCount = 0;
      interrupts();
   }

   feather.setTextSize(3);
   feather.println("Interrupts", Color::HEADING);
   feather.moveCursorY(feather.charH() / 2);

   feather.setTextSize(2);
   feather.print(" Rising ", Color::LABEL);
   feather.print(risingPin, pinFormat, Color::LABEL);
   feather.println(risingCount, countFormat, Color::VALUE);
   feather.moveCursorY(1);

   feather.print("Falling ", Color::LABEL);
   feather.print(fallingPin, pinFormat, Color::LABEL);
   feather.println(fallingCount, countFormat, Color::VALUE);
   feather.moveCursorY(1);

   feather.print(" Change ", Color::LABEL);
   feather.print(changePin, pinFormat, Color::LABEL);
   feather.println(changeCount, countFormat, Color::VALUE);
   feather.moveCursorY(1);

   feather.print((float)fallingCount / risingCount);
   feather.print("  ");
   feather.print((float)changeCount / risingCount);
}


