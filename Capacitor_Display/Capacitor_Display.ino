#include <Arduino.h>
#include "CapacitorSensor.h"
#include "Feather.h"
#include "Timer.h"

Feather feather;
Format chargeTimeFormat("###.# us");
Format rateFormat("Raw Sensor Rate #### per/s");

constexpr uint16_t DISPLAY_REFRESH_MS = 100;
constexpr uint8_t HEADER_TEXT_SIZE = 3;
constexpr uint8_t VALUE_TEXT_SIZE = 5;
constexpr uint8_t FOOTER_TEXT_SIZE = 2;

Timer displayRefreshTimer(DISPLAY_REFRESH_MS);

constexpr uint8_t CHARGE_PIN = 6;
constexpr uint8_t SENSE_PIN = 5;

CapacitorSensor sensor(CHARGE_PIN, SENSE_PIN);

int16_t valueY = 0;
int16_t footerY = 0;

void setup()
{
   feather.begin();
   sensor.begin();

   feather.setTextSize(HEADER_TEXT_SIZE);
   int16_t headerCharH = feather.charH();

   feather.setTextSize(VALUE_TEXT_SIZE);
   int16_t valueCharH = feather.charH();

   feather.setTextSize(FOOTER_TEXT_SIZE);
   int16_t footerCharH = feather.charH();

   footerY = feather.height() - footerCharH;
   valueY = headerCharH + (footerY - headerCharH - valueCharH) / 2;
}

void loop()
{
   float chargeTime = sensor.chargeTimeMicros();

   if (displayRefreshTimer.ready())
   {
      feather.setCursor(0, 0);
      feather.setTextSize(HEADER_TEXT_SIZE);
      feather.println("Capacitor", Color::HEADING);

      feather.setTextSize(VALUE_TEXT_SIZE);
      feather.setCursorY(valueY);
      feather.printlnC(chargeTime, chargeTimeFormat, Color::VALUE);

      feather.setTextSize(FOOTER_TEXT_SIZE);
      feather.setCursorY(footerY);
      feather.printlnR(sensor.rate(), rateFormat, Color::SUB_LABEL);
   }
}
