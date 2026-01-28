
#include "Feather.h"
#include "Adafruit_MAX1704X.h"

Feather feather;
Adafruit_MAX17048 battery;

Format percentFormat("###%");
Format batteryVoltsFormat("#.#v");
Format chargeRateFormat("####%/hr");

void setup()
{
   // start serial port
   Serial.begin(115200);

   // wait a few seconds for the serial monitor to open
   while (millis() < 2000 && !Serial)
   {
   };
   delay(500);

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
   feather.println("Battery Info", Color::CYAN);
   feather.moveCursorY(feather.charH() / 2);

   feather.print("Volts: ", Color::WHITE);
   feather.println(battery.cellVoltage(), batteryVoltsFormat, Color::YELLOW);
   feather.moveCursorY(feather.charH() / 4);

   feather.print("State: ", Color::WHITE);
   uint16_t x = feather.display.getCursorX();
   feather.println(battery.cellPercent(), percentFormat, Color::YELLOW);
   feather.moveCursorY(feather.charH() / 4);

   feather.print(" Rate: ", Color::WHITE);
   feather.setCursorX(x);
   feather.moveCursorY((CharSize::MEDIUM_H - CharSize::SMALL_H) / 2);
   feather.setTextSize(TextSize::SMALL);
   feather.print(battery.chargeRate(), chargeRateFormat, Color::YELLOW);
}
