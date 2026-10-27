//
// LED test sketch for the Waveshare ESP32-S3-Zero (Sensors 2) board.
//
// Repeats a full test cycle on the LEDs:
// 1. Steps through the normal startup status sequence on the status pixel
//    (pixel 0 on GPIO 6, mirrored on the onboard GPIO 21 NeoPixel):
//    - STARTED (solid white)
//    - WIFI_CONNECTING (blinking blue)
//    - SERVICES_CONNECTING (blinking green)
//    - RUNNING (solid green)
// 2. While RUNNING stays solid green on the status pixel, blinks the
//    general-purpose LED pixels (led2 and led3 on pixels 1 and 2 of GPIO 6) and cycles
//    their colors every 2 seconds through Orange, Yellow, Cyan, and Magenta.
//

#include <Arduino.h>
#include <array>

#include "SerialX.h"
#include "Timer.h"
#include "Waveshare_ESP32_S3_Zero.h"

constexpr uint16_t INTERVAL_STARTED_MS = 1000;
constexpr uint16_t INTERVAL_WIFI_CONNECTING_MS = 2000;
constexpr uint16_t INTERVAL_SERVICES_CONNECTING_MS = 2000;

constexpr uint16_t BLINK_INTERVAL_MS = 300;
constexpr uint16_t COLOR_CYCLE_INTERVAL_MS = 2000;

struct TestColor
{
   const char* name;
   Color color;
};

constexpr std::array<TestColor, 4> TEST_COLORS = {{
   { "Orange",  Color::ORANGE },
   { "Yellow",  Color::YELLOW },
   { "Cyan",    Color::CYAN },
   { "Magenta", Color::MAGENTA },
}};

WaveShare_ESP32_S3_Zero_Sensors2 arduino;
Timer colorTimer(COLOR_CYCLE_INTERVAL_MS);
size_t currentColorIndex = 0;

///
/// <summary>
/// Applies the current color from TEST_COLORS to the general-purpose LEDs and logs it to Serial.
/// </summary>
///
void applyCurrentColor()
{
   const TestColor& entry = TEST_COLORS[currentColorIndex];

   Serial.print("LED color: ");
   Serial.println(entry.name);

   arduino.led2.setColor(entry.color);
   arduino.led3.setColor(entry.color);
}

///
/// <summary>
/// Runs the startup status sequence on the status pixels, then starts the general-purpose LED color cycle.
/// </summary>
///
void startCycle()
{
   arduino.led2.turnOff();
   arduino.led3.turnOff();

   Serial.println("LED_Sensors2 - Running status startup sequence...");

   Serial.println("Status: STARTED");
   arduino.setStatus(Status::STARTED);
   delay(INTERVAL_STARTED_MS);

   Serial.println("Status: WIFI_CONNECTING");
   arduino.setStatus(Status::WIFI_CONNECTING);
   delay(INTERVAL_WIFI_CONNECTING_MS);

   Serial.println("Status: SERVICES_CONNECTING");
   arduino.setStatus(Status::SERVICES_CONNECTING);
   delay(INTERVAL_SERVICES_CONNECTING_MS);

   Serial.println("Status: RUNNING");
   arduino.setStatus(Status::RUNNING);

   // Start blinking the general-purpose LEDs (pixels 1 and 2) and apply the first color.
   currentColorIndex = 0;
   applyCurrentColor();
   arduino.led2.blink(BLINK_INTERVAL_MS);
   arduino.led3.blink(BLINK_INTERVAL_MS);
   colorTimer.reset();
}

void setup()
{
   SerialX::begin();
   arduino.begin();

   startCycle();
}

void loop()
{
   if (colorTimer.ready())
   {
      currentColorIndex++;
      if (currentColorIndex < TEST_COLORS.size())
      {
         applyCurrentColor();
      }
      else
      {
         startCycle();
      }
   }
}
