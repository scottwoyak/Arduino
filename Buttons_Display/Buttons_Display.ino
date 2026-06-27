/// <summary>
/// Multi-button state monitor display.
/// </summary>
/// <remarks>
/// Monitors the real-time state of 12 digital input buttons and displays their
/// current and previous press states. Auto-resets button states after 5 seconds
/// of inactivity. Useful for debugging button input configurations.
/// 
/// Hardware: Arduino Feather with TFT display and multiple button inputs.
/// </remarks>

#include <Arduino.h>

#include "Feather.h"
#include "SerialX.h"
#include "Stopwatch.h"

Feather feather;
Stopwatch sw(false);

Button* buttons[] = {
   new Button(0),    // builtin button
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
   "0", "A0", "A1", "A2", "A3", "A4", "A5", "5", "6", "9", "10", "11",
};

constexpr uint NUM_BUTTONS = sizeof(buttons) / sizeof(buttons[0]);

Format pinFormat(3, Format::Alignment::RIGHT);
Format valueFormat(4);
Format timeFormat("##.#s");

void setup()
{
   SerialX::begin();
   feather.begin();
   feather.setRotation(DisplayRotation::PORTRAIT);

   for (int i = 0; i < NUM_BUTTONS; i++)
   {
      buttons[i]->begin();
      buttons[i]->autoReset = false;
   }
}

void loop()
{
   // Detect button activity and start/restart timer
   for (int i = 0; i < NUM_BUTTONS; i++)
   {
      Button* button = buttons[i];
      if ((button->wasPressed() && !sw.isRunning()) || button->isPressed())
      {
         sw.reset();
         sw.start();
      }
   }

   // Display button states
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

   // Auto-reset after 5 seconds of activity
   if (sw.elapsedSecs() > 5)
   {
      for (int i = 0; i < NUM_BUTTONS; i++)
      {
         buttons[i]->reset();
      }

      sw.stop();
      sw.reset();
   }

   // Display reset countdown
   if (sw.isRunning())
   {
      feather.setTextSize(2);
      feather.setCursor(0, -feather.charH());
      feather.print("Reset: ", Color::SUB_LABEL);
      feather.print((5 - sw.elapsedSecs()), timeFormat, Color::SUB_LABEL);
   }
   else
   {
      // Clear the countdown line
      feather.fillRect(0, -feather.charH(), feather.width(), feather.charH(), Color::BLACK);
   }
}
