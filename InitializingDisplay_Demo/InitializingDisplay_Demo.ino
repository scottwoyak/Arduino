//
// Demonstrates the InitializingDisplay class on a Feather display board.
//
// Shows a normal heading and then adds a simulated initialization line every second.
// Once the lines fill the area below the heading, older lines scroll up so the newest
// line is always fully visible at the bottom. The sequence then restarts.
//

#include <Arduino.h>
#include <array>

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#include "WrongBoard.h"
#endif

#include "InitializingDisplay.h"
#include "SerialX.h"
#include "Timer.h"

constexpr float LINE_PERIOD_S = 1;

constexpr std::array<const char*, 14> INIT_STEPS =
{
   "Display...",
   "Serial...",
   "I2C bus...",
   "Temp sensor...",
   "Pressure sensor...",
   "Light sensor...",
   "SD card...",
   "WiFi...",
   "Time sync...",
   "Influx...",
   "OTA check...",
   "Calibrating...",
   "Self test...",
   "Ready",
};

Arduino arduino;
InitializingDisplay initDisplay(&arduino);
TimerSecs lineTimer(LINE_PERIOD_S);
size_t nextStep = 0;

void setup()
{
   SerialX::begin();

   arduino.begin();

   arduino.printInitHeader("Initializing");
   initDisplay.begin();
}

void loop()
{
   if (lineTimer.ready())
   {
      if (nextStep == INIT_STEPS.size())
      {
         initDisplay.clear();
         nextStep = 0;
      }

      initDisplay.printLabel(INIT_STEPS[nextStep]);

      delay(500);  // simulates the initialization work

      initDisplay.setValue("OK");
      nextStep++;
   }
}
