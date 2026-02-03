
#include "Feather.h"
#include "Adafruit_MAX1704X.h"
#include "Adafruit_INA219.h"
#include "SerialX.h"
#include "RunningAverager.h"

Feather feather;
Adafruit_MAX17048 battery;
Adafruit_INA219 sensor;

Format voltsFormat("#.##v");
Format mvoltsFormat("+##.#mV");
Format currentFormat("+####.#mA");
Format percentFormat("###.#%");

RunningAverager busVolts(100);
RunningAverager shuntVolts(100);
RunningAverager mA(100);

void setup()
{
   SerialX::begin();

   if (!sensor.begin())
   {
      Serial.println("INA219 Not Found");
      while (1);
   }

   feather.begin();
   while (battery.begin() == false)
   {
      Serial.print("Looking for battery");
      delay(1000);
   }
}

void loop()
{
   feather.setCursor(0, 0);
   feather.setTextSize(2);

   //feather.println("INA219 Sensor", Color::HEADING);
   mA.set(sensor.getCurrent_mA());
   busVolts.set(sensor.getBusVoltage_V());
   shuntVolts.set(sensor.getShuntVoltage_mV());

   feather.print("    BusV: ", Color::WHITE);
   feather.moveCursor(feather.charW() / 2, feather.charH() / 4);
   feather.println(sensor.getBusVoltage_V(), voltsFormat, Color::VALUE);

   feather.print("  ShuntV:", Color::WHITE);
   feather.moveCursor(feather.charW() / 2, feather.charH() / 4);
   feather.println(sensor.getShuntVoltage_mV(), mvoltsFormat, Color::VALUE);

   feather.print("   LoadV: ", Color::WHITE);
   feather.moveCursor(feather.charW() / 2, feather.charH() / 4);
   feather.println(busVolts.get() + shuntVolts.get() / 1000, voltsFormat, Color::VALUE);

   feather.print(" Current:", Color::WHITE);
   feather.moveCursor(feather.charW() / 2, feather.charH() / 4);
   feather.println(mA.get(), currentFormat, Color::VALUE);

   //feather.println("Battery Sensor", Color::HEADING);
   feather.moveCursorY(10);

   feather.print("   Volts: ", Color::WHITE);
   feather.moveCursor(feather.charW() / 2, feather.charH() / 4);
   feather.println(battery.cellVoltage(), voltsFormat, Color::VALUE2);

   feather.print("   State: ", Color::WHITE);
   feather.moveCursor(feather.charW() / 2, feather.charH() / 4);
   feather.println(battery.cellPercent(), percentFormat, Color::VALUE2);
}
