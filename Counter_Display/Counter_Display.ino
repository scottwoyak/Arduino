
#include "Feather.h"
#include "SerialX.h"
#include "Counter.h"

Feather feather;

Counter counter(0);
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
   feather.println("  Pin: ", counter.getPin());
   feather.moveCursorY(2);

   feather.println("Count: ", counter.count(), countFormat);
   feather.moveCursorY(2);

   feather.println(" Span: ", span, spanFormat);
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
      counter.reset();
   }
}
