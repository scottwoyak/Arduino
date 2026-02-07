
#include "Feather.h"
#include "SerialX.h"
#include "TimedAverager.h"
#include "WindMeter.h"
#include "Bar.h"

Feather feather;
WindMeter wind(A5);
TimedAverager windAverager(10*60*1000, 20); 

Format speedFormat("##.# mph");

Color c1 = Color565::fromRGB(0, 128, 0);
Color c2 = Color::YELLOW;
MultiHorizontalBar multiBar(Rect16(0, 135 - 40 - 1, 240, 40), RangeF(0, 40), 4, c1, c2, Color::BLACK);

void setup()
{
   SerialX::begin();
   feather.begin();
   wind.begin();

   Color565::println(Color::GREEN);
   Color565::println(Color::YELLOW);
   Color565::println(Color565::blend(Color::GREEN, Color::YELLOW, 0.5));

   delay(1000); // provide time for the wind meter to get a reading
}

void loop()
{
   // get values
   float speed = wind.getSpeed();
   windAverager.set(speed);

   // display values
   feather.setCursor(0, 0);
   feather.setTextSize(3);

   feather.println("Wind", Color::HEADING);
   feather.moveCursorY(4);

   feather.setTextSize(3);
   feather.println(speed, speedFormat, Color::VALUE);
   feather.moveCursorY(1);

   feather.setTextSize(2);
   feather.print("Min: ", Color::LABEL);
   feather.print(windAverager.getMin(), speedFormat, Color::VALUE);
   feather.println();
   feather.moveCursorY(1);
   feather.print("Max: ", Color::LABEL);
   feather.print(windAverager.getMax(), speedFormat, Color::VALUE);

   // display bar
   multiBar.draw(&feather.display, speed);
}
