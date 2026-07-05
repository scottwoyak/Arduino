
#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include <Adafruit_MAX1704X.h>
#include <Adafruit_INA219.h>
#include "SerialX.h"
#include "RollingAverage.h"

Arduino arduino;
Adafruit_MAX17048 battery;
Adafruit_INA219 sensor;

Format voltsFormat("#.##v");
Format mvoltsFormat("+##.#mV");
Format currentFormat("+####.#mA");
Format percentFormat("###.#%");

RollingAverage busVolts(100);
RollingAverage shuntVolts(100);
RollingAverage mA(100);

void setup()
{
   SerialX::begin();

   if (!sensor.begin())
   {
      Serial.println("INA219 Not Found");
      while (1);
   }

   arduino.begin();
   while (battery.begin() == false)
   {
      Serial.print("Looking for battery");
      delay(1000);
   }
}

void loop()
{
   arduino.setCursor(0, 0);
   arduino.setTextSize(2);

   //arduino.println("INA219 Sensor", Color::HEADING);
   mA.set(sensor.getCurrent_mA());
   busVolts.set(sensor.getBusVoltage_V());
   shuntVolts.set(sensor.getShuntVoltage_mV());

   arduino.print("    BusV: ", Color::WHITE);
   arduino.moveCursor(arduino.charW() / 2, arduino.charH() / 4);
   arduino.println(sensor.getBusVoltage_V(), voltsFormat, Color::VALUE);

   arduino.print("  ShuntV:", Color::WHITE);
   arduino.moveCursor(arduino.charW() / 2, arduino.charH() / 4);
   arduino.println(sensor.getShuntVoltage_mV(), mvoltsFormat, Color::VALUE);

   arduino.print("   LoadV: ", Color::WHITE);
   arduino.moveCursor(arduino.charW() / 2, arduino.charH() / 4);
   arduino.println(busVolts.get() + shuntVolts.get() / 1000, voltsFormat, Color::VALUE);

   arduino.print(" Current:", Color::WHITE);
   arduino.moveCursor(arduino.charW() / 2, arduino.charH() / 4);
   arduino.println(mA.get(), currentFormat, Color::VALUE);

   //arduino.println("Battery Sensor", Color::HEADING);
   arduino.moveCursorY(10);

   arduino.print("   Volts: ", Color::WHITE);
   arduino.moveCursor(arduino.charW() / 2, arduino.charH() / 4);
   arduino.println(battery.cellVoltage(), voltsFormat, Color::VALUE2);

   arduino.print("   State: ", Color::WHITE);
   arduino.moveCursor(arduino.charW() / 2, arduino.charH() / 4);
   arduino.println(battery.cellPercent(), percentFormat, Color::VALUE2);
}
