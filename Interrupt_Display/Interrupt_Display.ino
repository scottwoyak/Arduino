
#include "ArduinoBoard.h"

#ifndef ARDUINO_BUTTON_SUPPORTED
#error "This sketch requires a board with button support (e.g. Feather ESP32-S3 or Feather M0)."
#endif
#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include "SerialX.h"

Arduino arduino;

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
   arduino.begin();

   pinMode(risingPin, INPUT_PULLUP);
   pinMode(fallingPin, INPUT_PULLUP);
   pinMode(changePin, INPUT_PULLUP);

   attachInterrupt(digitalPinToInterrupt(risingPin), onRising, RISING);
   attachInterrupt(digitalPinToInterrupt(fallingPin), onFalling, FALLING);
   attachInterrupt(digitalPinToInterrupt(changePin), onChange, CHANGE);
}

void loop()
{
   arduino.setCursor(0, 0);

   if (arduino.buttonA.wasPressed())
   {
      noInterrupts();
      risingCount = 0;
      fallingCount = 0;
      changeCount = 0;
      interrupts();
   }

   arduino.setTextSize(3);
   arduino.println("Interrupts", Color::HEADING);
   arduino.moveCursorY(arduino.charH() / 2);

   arduino.setTextSize(2);
   arduino.print(" Rising ", Color::LABEL);
   arduino.print(risingPin, pinFormat, Color::LABEL);
   arduino.println(risingCount, countFormat, Color::VALUE);
   arduino.moveCursorY(1);

   arduino.print("Falling ", Color::LABEL);
   arduino.print(fallingPin, pinFormat, Color::LABEL);
   arduino.println(fallingCount, countFormat, Color::VALUE);
   arduino.moveCursorY(1);

   arduino.print(" Change ", Color::LABEL);
   arduino.print(changePin, pinFormat, Color::LABEL);
   arduino.println(changeCount, countFormat, Color::VALUE);
   arduino.moveCursorY(1);

   arduino.print((float)fallingCount / risingCount);
   arduino.print("  ");
   arduino.print((float)changeCount / risingCount);
}


