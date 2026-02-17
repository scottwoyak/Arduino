
#include "SerialX.h"
#include "Feather.h"
#include "Stopwatch.h"

Feather feather;

Stopwatch sw(false);

Button* buttons[] = {
    new Button(0), // builtin button
    new Button(A0),
    new Button(A1),
    new Button(A2),
    new Button(A3),
    new Button(A4),
    new Button(A5),
    new Button(5),
    new Button(6),
    new Button(9),
    new Button(10),
    new Button(11),
};

String labels[] = {
   "0",
   "A0",
   "A1",
   "A2",
   "A3",
   "A4",
   "A5",
   "5",
   "6",
   "9",
   "10",
   "11",
};

constexpr uint NUM_BUTTONS = sizeof(buttons) / sizeof(buttons[0]);

Format pinFormat(3);
Format valueFormat(4);
Format timeFormat("##.#s");

void setup()
{
   pinFormat.alignment = Format::Alignment::RIGHT;

   SerialX::begin();
   feather.begin();
   feather.display.setRotation(2);

   for (int i = 0; i < NUM_BUTTONS; i++)
   {
      buttons[i]->begin();
      buttons[i]->autoReset = false;
   }
}

void loop()
{
   for (int i = 0; i < NUM_BUTTONS; i++)
   {
      Button* button = buttons[i];
      if ((button->wasPressed() && sw.isRunning() == false) || button->isPressed())
      {
         sw.reset();
         sw.start();
      }
   }

   feather.setTextSize(2);
   feather.setCursor(0, 0);

   feather.print("Pin ", Color::LABEL);
   feather.print("Is  ", Color::LABEL);
   feather.print("Was ", Color::LABEL);
   feather.println();
   feather.moveCursorY(2);

   for (int i = 0; i < NUM_BUTTONS; i++)
   {
      Button* button = buttons[i];
      feather.print(labels[i], pinFormat, Color::VALUE);
      feather.print(" ");
      feather.print(button->isPressed() ? "Yes" : "No", valueFormat, button->isPressed() ? Color::VALUE2 : Color::DARKGRAY);
      feather.print(button->wasPressed() ? "Yes" : "No", valueFormat, button->wasPressed() ? Color::VALUE2 : Color::DARKGRAY);
      feather.println();
      feather.moveCursorY(1);
   }

   if (sw.elapsedSecs() > 5)
   {
      for (int i = 0; i < NUM_BUTTONS; i++)
      {
         buttons[i]->reset();
      }

      sw.stop();
      sw.reset();
   }

   if (sw.isRunning())
   {
      feather.display.setTextSize(2);
      feather.setCursor(0, feather.display.height() - feather.charH());
      feather.print("Reset: ", Color::SUB_LABEL);
      feather.print((5 - sw.elapsedSecs()), timeFormat, Color::SUB_LABEL);
   }
   else
   {
      // cover up the time remaining msg
      feather.display.fillRect(0, feather.display.height() - feather.charH(), feather.display.width(), feather.charH(), (uint16_t)Color::BLACK);
   }
}


