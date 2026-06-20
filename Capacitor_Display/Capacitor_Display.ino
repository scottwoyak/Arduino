#include <Arduino.h>
#include <CapacitorSensor.h>
#include <Feather.h>
#include <RollingStats.h>
#include <Timer.h>

Feather feather;
Format timeFormat("###.# us");
Format rateFormat("#### per/s");

RollingStats chargeTime10(10);
RollingStats chargeTime100(100);
RollingStats chargeTime1000(1000);
Timer displayTimer(200);

// Hardware Pin Assignments
const int CHARGE_PIN = 5;
const int SENSE_PIN = 6;

CapacitorSensor sensor(CHARGE_PIN, SENSE_PIN);

void setup()
{
   feather.begin();
   sensor.begin();
}

void loop()
{
   if (sensor.hasChanged())
   {
      uint32_t chargeTime = sensor.chargeTimeMicros();
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
      feather.println(chargeTime10.average(), timeFormat, Color::VALUE);
      feather.print(" 100: ", Color::LABEL);
      feather.println(chargeTime100.average(), timeFormat, Color::VALUE);
      feather.print("1000: ", Color::LABEL);
      feather.println(chargeTime1000.average(), timeFormat, Color::VALUE);
      feather.println();

      feather.setTextSize(2);
      feather.setCursorY(feather.height() - feather.charH());
      feather.printlnR(sensor.rate(), rateFormat, Color::SUB_LABEL);
   }
}