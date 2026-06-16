#include <Arduino.h>
#include <CapacitySensor.h>
#include <Feather.h>
#include <RateTracker.h>
#include <RunningAverager.h>
#include <Timer.h>

Feather feather;
Format timeFormat("###.# us");
Format rateFormat("#### per/s");

RunningAverager chargeTime10(10);
RunningAverager chargeTime100(100);
RunningAverager chargeTime1000(1000);
Timer displayTimer(500);
RollingRateTracker rate(500);

// Hardware Pin Assignments
const int CHARGE_PIN = 5;
const int SENSE_PIN = 6;

CapacitySensor sensor(CHARGE_PIN, SENSE_PIN);

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
      uint32_t chargeTime = sensor.chargeTimeUs();
      chargeTime10.set(chargeTime);
      chargeTime100.set(chargeTime);
      chargeTime1000.set(chargeTime);
   }

   if (displayTimer.ready())
   {
      feather.setCursor(0, 0);
      feather.setTextSize(2);
      feather.println("Charge Time: (avg of N)", Color::HEADING);
      feather.moveCursorY(10);

      feather.setTextSize(3);
      feather.print("  10: ", Color::LABEL);
      feather.println(chargeTime10.get(), timeFormat, Color::VALUE);
      feather.print(" 100: ", Color::LABEL);
      feather.println(chargeTime100.get(), timeFormat, Color::VALUE);
      feather.print("1000: ", Color::LABEL);
      feather.println(chargeTime1000.get(), timeFormat, Color::VALUE);
      feather.println();

      feather.setTextSize(2);
      feather.setCursorY(feather.height() - feather.charH());
      feather.printlnR(rate.getRate(), rateFormat, Color::SUB_LABEL);
   }
}