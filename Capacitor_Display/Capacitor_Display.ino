//
// Displays capacitor charge time and raw sampling rate on a Feather display.
//
// Reads rolling-average capacitor charge time in microseconds from CapacitorSensor,
// updates the main value at a fixed cadence, and renders a footer row with raw sensor rate.
//

#include <Arduino.h>
#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include "CapacitorSensor.h"
#include "Timer.h"

// ----------- Pins
constexpr uint8_t CHARGE_PIN = 6;
constexpr uint8_t SENSE_PIN = 5;

// ----------- The Board
Arduino arduino;

// ----------- Display Items
Format chargeTimeFormat("###.# us");
Format rateFormat("Raw Sensor Rate #### per/s");
constexpr uint16_t DISPLAY_REFRESH_MS = 100;
Timer displayRefreshTimer(DISPLAY_REFRESH_MS);

// ----------- Sensor
CapacitorSensor sensor(CHARGE_PIN, SENSE_PIN);

// ----------- Sketch Variables
int16_t valueY = 0;

void setup()
{
   arduino.begin();
   sensor.begin();

   arduino.setTextSize(3);
   int16_t headerCharH = arduino.charH();

   arduino.setTextSize(2);
   int16_t subheaderCharH = arduino.charH();

   arduino.setTextSize(5);
   int16_t valueCharH = arduino.charH();

   arduino.setTextSize(2);
   int16_t footerCharH = arduino.charH();

   int16_t footerTopY = arduino.height() - footerCharH;
   int16_t contentTopY = headerCharH + subheaderCharH;
   valueY = contentTopY + (footerTopY - contentTopY - valueCharH) / 2;
}

void loop()
{
   if (displayRefreshTimer.ready())
   {
      float chargeTime = sensor.chargeTimeMicros();

      arduino.setCursor(0, 0);
      arduino.setTextSize(3);
      arduino.println("Capacitor", Color::HEADING);

      arduino.setTextSize(2);
      arduino.println("Charge Time", Color::LABEL);

      arduino.setTextSize(5);
      arduino.setCursorY(valueY);
      arduino.printlnC(chargeTime, chargeTimeFormat, Color::VALUE);

      arduino.setTextSize(2);
      arduino.setCursorY(-arduino.charH());
      arduino.printlnR(sensor.rate(), rateFormat, Color::SUB_LABEL);
   }
}
