
#include "Feather.h"
#include "SerialX.h"
#include "Counter.h"

Feather feather;

//Counter counter(0);
Counter counter(5);
double minSpan = 0;

Format countFormat("######");
Format spanFormat("#######.## ms");

void setup()
{
   SerialX::begin();
   feather.begin();
   counter.begin();
}

void loop()
{
   feather.setCursor(0, 0);
   feather.setTextSize(3);

   double span = counter.span() / 1000.0;
   minSpan = min(span, minSpan == 0 ? span : minSpan);

   feather.println("Counter", Color::HEADING);
   feather.moveCursorY(feather.charH() / 2);

   feather.setTextSize(2);
   feather.print("  Pin: ", Color::LABEL);
   feather.println(counter.getPin(), Color::VALUE);
   feather.moveCursorY(2);

   feather.print("Count: ", Color::LABEL);
   feather.println(counter.count(), countFormat, Color::VALUE);
   feather.moveCursorY(2);

   feather.print(" Span: ", Color::LABEL);
   feather.println(span, spanFormat, Color::VALUE);
   feather.moveCursorY(2);

   feather.print("  Min: ", Color::LABEL);
   if (minSpan == 0)
   {
      feather.print("----", Color::GRAY);
   }
   else
   {
      feather.print(minSpan, spanFormat, Color::VALUE);
   }

   if (feather.buttonA.wasPressed())
   {
      minSpan = 0;
      feather.buttonA.reset();
      counter.reset();
   }
}
