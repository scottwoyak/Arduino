
#include "Feather.h"
#include "Adafruit_MAX1704X.h"
#include "SerialX.h"

Feather feather;
Adafruit_MAX17048 battery;

Format batteryVoltsFormat("#.#v");
Format percentFormat("###%");
Format chargeRateFormat("+###%/hr");

void setup()
{
   SerialX::begin();
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
   feather.setTextSize(3);
   feather.println("Battery Info", Color::HEADING);
   feather.moveCursorY(feather.charH() / 4);

   feather.print("Volts: ", Color::LABEL);
   feather.println(battery.cellVoltage(), batteryVoltsFormat, Color::VALUE);
   feather.moveCursorY(feather.charH() / 4);

   feather.print("State: ", Color::LABEL);
   uint16_t x = feather.display.getCursorX();
   feather.println(battery.cellPercent(), percentFormat, Color::VALUE);
   feather.moveCursorY(feather.charH() / 4);

   feather.print(" Rate: ", Color::LABEL);
   feather.setTextSize(2);
   feather.setCursorX(x);
   float chargeRate = battery.chargeRate();
   feather.println(chargeRate, chargeRateFormat, Color::VALUE);
   feather.setCursorX(x);

   if (fabs(chargeRate) >= 1)
   {
      feather.println(chargeRate > 0 ? "Charging" : "Draining", Color::VALUE2);
   }
   else
   {
      feather.println("        ");
   }
}
