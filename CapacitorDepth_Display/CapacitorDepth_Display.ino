#include <Arduino.h>
#include "CapacitorDepthSensor.h"
#include "Feather.h"
#include "RollingStats.h"
#include "Timer.h"

Feather feather;
Format depthFormat("###.# cm");
Format rateFormat("#### per/s");

Timer displayTimer(100);

// Hardware Pin Assignments
const int CHARGE_PIN = 6;
const int SENSE_PIN = 5;

constexpr auto ZERO_CHARGE_TIME = 128.3;
constexpr auto FULL_CHARGE_TIME = 295;
constexpr auto FULL_DEPTH = 30;

CapacitorDepthSensor sensor(CHARGE_PIN, SENSE_PIN, ZERO_CHARGE_TIME, FULL_CHARGE_TIME, FULL_DEPTH);

void setup()
{
   feather.begin();
   sensor.begin();
}

void loop()
{
   if (displayTimer.ready())
   {
      float depth = sensor.getDepth();

      feather.setCursor(0, 0);
      feather.setTextSize(2);
      feather.println("Capacitor Depth Sensor", Color::HEADING);
      feather.moveCursorY(10);

      feather.setTextSize(5);
      feather.println(depth, depthFormat, Color::VALUE);

      feather.setTextSize(3);
      feather.setCursorY(feather.height() - feather.charH());
      feather.printlnR(sensor.rate(), rateFormat, Color::SUB_LABEL);
   }
}
