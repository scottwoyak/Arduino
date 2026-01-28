
#include "Feather.h"
#include "Adafruit_MAX1704X.h"

Feather feather;
Adafruit_MAX17048 battery;

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
   feather.moveCursorY(10);

   feather.print("Volts: ", Color::WHITE);
   feather.println(battery.cellVoltage(), Color::YELLOW);
   feather.moveCursorY(4);

   feather.print("State: ", Color::WHITE);
   uint16_t x = feather.display.getCursorX();
   feather.println(battery.cellPercent(), "%", 6, 1, Color::YELLOW);
   feather.moveCursorY(4);

   feather.print(" Rate: ", Color::WHITE);
   feather.setCursorX(x);
   feather.moveCursorY((CharSize::MEDIUM_H - CharSize::SMALL_H)/2);
   feather.setTextSize(TextSize::SMALL);
   feather.print((int) battery.chargeRate(), "%/hr", 10, Color::YELLOW);

   /*
   feather.setTextSize(TextSize::SMALL);

   feather.moveCursor(5, 8);
   feather.print("%/hr", Color::YELLOW);
   */
}
