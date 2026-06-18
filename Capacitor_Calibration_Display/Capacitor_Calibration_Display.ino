#include <Arduino.h>
#include <CapacitorDepthSensor.h>
#include <Feather.h>
#include <RateTracker.h>
#include <RollingAverage.h>
#include <Timer.h>

Feather feather;
Format chargeFormat("###.# us");
Format rateFormat("#### per/s");

Timer displayTimer(500);
RollingRateTracker rate(500);

// Hardware Pin Assignments
const int CHARGE_PIN = 5;
const int SENSE_PIN = 6;

CapacitorSensor sensor(CHARGE_PIN, SENSE_PIN);
RollingAverage avg10(10);
RollingAverage avg100(100);
RollingAverage avg500(500);

void setup()
{
   feather.begin();
   sensor.begin();
}

void loop()
{
   if (sensor.loop())
   {
      rate.tick();
      float chargeTimeUs = sensor.chargeTimeUs();
      avg10.set(chargeTimeUs);
      avg100.set(chargeTimeUs);
      avg500.set(chargeTimeUs);
   }

   if (displayTimer.ready())
   {
      feather.setCursor(0, 0);
      feather.setTextSize(2);
      feather.println("Capacitor Sensor Tuner", Color::HEADING);
      feather.moveCursorY(10);

      feather.setTextSize(3);
      feather.println(avg10.get(), chargeFormat, Color::VALUE);
      feather.println(avg100.get(), chargeFormat, Color::VALUE);
      feather.println(avg500.get(), chargeFormat, Color::VALUE);

      feather.setTextSize(2);
      feather.setCursorY(feather.height() - feather.charH());
      feather.printlnR(rate.getRate(), rateFormat, Color::SUB_LABEL);
   }
}