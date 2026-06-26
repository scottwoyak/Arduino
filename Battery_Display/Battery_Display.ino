
#include "Feather.h"
#include <Adafruit_MAX1704X.h>
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
   Wire.begin();
   while (battery.begin() == false)
   {
      Serial.println("Looking for battery");
      delay(1000);
   }
}

void loop()
{
   feather.setCursor(0, 0);
   feather.setTextSize(3);
   feather.println("Battery Info", Color::HEADING);
   feather.moveCursorY(feather.charH() / 4);

   feather.println("Volts: ", battery.cellVoltage(), batteryVoltsFormat);
   feather.moveCursorY(feather.charH() / 4);

   feather.print("State: ", battery.cellPercent(), percentFormat);
   uint16_t x = feather.display.getCursorX();
   feather.println();
   feather.moveCursorY(feather.charH() / 4);

   feather.setTextSize(2);
   feather.setCursorX(x);
   float chargeRate = battery.chargeRate();
   feather.println(" Rate: ", chargeRate, chargeRateFormat);
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
