
#include "Feather.h"
#include "Adafruit_MAX1704X.h"
#include "SerialX.h"

Feather feather;
Adafruit_MAX17048 battery;

Format batteryVoltsFormat("#.#v");
Format percentFormat("###%");
Format chargeRateFormat("####%/hr");

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
   feather.setTextSize(TextSize::MEDIUM);
   feather.println("Battery Info", Color::HEADING);
   feather.moveCursorY(feather.charH() / 2);

   feather.print("Volts: ", Color::WHITE);
   feather.println(battery.cellVoltage(), batteryVoltsFormat, Color::VALUE);
   feather.moveCursorY(feather.charH() / 4);

   feather.print("State: ", Color::WHITE);
   uint16_t x = feather.display.getCursorX();
   feather.println(battery.cellPercent(), percentFormat, Color::VALUE);
   feather.moveCursorY(feather.charH() / 4);

   feather.print(" Rate: ", Color::WHITE);
   feather.setCursorX(x);
   feather.moveCursorY((CharSize::MEDIUM_H - CharSize::SMALL_H) / 2);
   feather.setTextSize(TextSize::SMALL);
   feather.print(battery.chargeRate(), chargeRateFormat, Color::VALUE);
}
