//
// Displays capacitor charge time and raw sampling rate on a Feather display.
//
// Reads rolling-average capacitor charge time in microseconds from CapacitorSensor,
// updates the main value at a fixed cadence, and renders a footer row with raw sensor rate.
//

#include <Arduino.h>
#include "CapacitorSensor.h"
#include "Feather.h"
#include "Timer.h"

Feather feather;
Format chargeTimeFormat("###.# us");
Format rateFormat("Raw Sensor Rate #### per/s");

constexpr uint16_t DISPLAY_REFRESH_MS = 100;
Timer displayRefreshTimer(DISPLAY_REFRESH_MS);

constexpr uint8_t CHARGE_PIN = 6;
constexpr uint8_t SENSE_PIN = 5;

CapacitorSensor sensor(CHARGE_PIN, SENSE_PIN);

int16_t valueY = 0;

void setup()
{
   feather.begin();
   sensor.begin();

   feather.setTextSize(3);
   int16_t headerCharH = feather.charH();

   feather.setTextSize(2);
   int16_t subheaderCharH = feather.charH();

   feather.setTextSize(5);
   int16_t valueCharH = feather.charH();

   feather.setTextSize(2);
   int16_t footerCharH = feather.charH();

   int16_t footerTopY = feather.height() - footerCharH;
   int16_t contentTopY = headerCharH + subheaderCharH;
   valueY = contentTopY + (footerTopY - contentTopY - valueCharH) / 2;
}

void loop()
{
   float chargeTime = sensor.chargeTimeMicros();

   if (displayRefreshTimer.ready())
   {
      feather.setCursor(0, 0);
      feather.setTextSize(3);
      feather.println("Capacitor", Color::HEADING);

      feather.setTextSize(2);
      feather.println("Charge Time", Color::LABEL);

      feather.setTextSize(5);
      feather.setCursorY(valueY);
      feather.printlnC(chargeTime, chargeTimeFormat, Color::VALUE);

      feather.setTextSize(2);
      feather.setCursorY(-feather.charH());
      feather.printlnR(sensor.rate(), rateFormat, Color::SUB_LABEL);
   }
}
