//
// Demonstrates several text-rendering techniques and compares their performance.
//
// Continuously displays an 8-digit random number in the center of the display,
// along with the resulting frame rate in the lower-right corner in size 2 text.
// Rotate encoderA to cycle between three rendering modes, and rotate encoderB to
// adjust the number's text size:
//
//   Default      - the number is fully reprinted every frame using a plain
//                   print(), the naive approach, with the default Adafruit-GFX-
//                   style font.
//   Smooth Fonts - the number is fully reprinted every frame using a plain
//                   print(), with a proportional font loaded via setTextSize().
//   Sprites      - the number is drawn with DisplayField, redrawing only the
//                   value via a sprite.
//

#include <Arduino.h>

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif
#ifndef ARDUINO_PLAYGROUND_SUPPORTED
#error "This sketch requires a board with rotary encoders (e.g. an ESP32-S3 Playground setup)."
#endif

#include "DisplayField.h"
#include "RollingRate.h"
#include "SerialX.h"

///
/// <summary>
/// Text-rendering techniques available for the number display.
/// </summary>
///
enum class DisplayMode : uint8_t
{
   DEFAULT_MODE,
   SMOOTH_FONTS,
   SPRITES,
};

// ----------- Modes
constexpr uint8_t NUM_MODES = 3;
const char* MODE_NAMES[NUM_MODES] = { "Default", "Smooth Fonts", "Sprites" };

// ----------- Text sizes
constexpr uint8_t MIN_NUMBER_TEXT_SIZE = 1;
constexpr uint8_t MAX_NUMBER_TEXT_SIZE = 7;
constexpr uint8_t HEADING_TEXT_SIZE = 3;
constexpr uint8_t SUB_HEADING_TEXT_SIZE = 2;
constexpr uint8_t RATE_TEXT_SIZE = 2;

// ----------- The Board
Arduino arduino;
RollingRate frameRate;

// ----------- Formats
Format numberFormat("########");
Format rateFormat("###/s", Format::Alignment::RIGHT);

// ----------- State
DisplayMode currentMode = DisplayMode::DEFAULT_MODE;
uint8_t numberTextSize = 4;

DisplayField* numberField = nullptr;
DisplayField* rateField = nullptr;
int16_t numberX = 0;
int16_t numberY = 0;

///
/// <summary>
/// Clears the display and redraws the heading, sub-heading, and the fields needed
/// for the current mode. Called on startup and whenever the mode changes.
/// </summary>
///
void setupMode()
{
   delete numberField;
   numberField = nullptr;

   delete rateField;
   rateField = nullptr;

   frameRate.reset();

   arduino.clearDisplay();

   arduino.setTextSize(HEADING_TEXT_SIZE);
   arduino.println("Text Performance", Color::HEADING);
   arduino.moveCursorY(arduino.charH() / 2);

   arduino.setTextSize(SUB_HEADING_TEXT_SIZE);
   arduino.print("Mode: ", Color::LABEL);
   arduino.println(MODE_NAMES[(uint8_t)currentMode], Color::HEADING2);
   arduino.moveCursorY(arduino.charH() / 2);

   if (currentMode == DisplayMode::DEFAULT_MODE)
   {
      // fall back to the default Adafruit-GFX-style font, matching the
      // "naive" appearance the display starts with before any font is loaded
      arduino.display.setFont(&fonts::Font0);
      arduino.display.setTextSize(numberTextSize);
   }
   else
   {
      arduino.setTextSize(numberTextSize);
   }

   int16_t numberWidth = arduino.textWidth("00000000");
   numberX = (arduino.width() - numberWidth) / 2;
   numberY = (arduino.height() - arduino.charH()) / 2;

   if (currentMode == DisplayMode::SPRITES)
   {
      numberField = new DisplayField(&arduino, numberX, numberY, "", numberFormat, numberTextSize);
   }

   arduino.setTextSize(RATE_TEXT_SIZE);
   int16_t rateWidth = arduino.textWidth("Rate: 999/s");
   int16_t x = arduino.width() - rateWidth - 10;
   int16_t y = arduino.height() - arduino.charH() - 10;
   rateField = new DisplayField(&arduino, x, y, "Rate", rateFormat, RATE_TEXT_SIZE);
}

void setup()
{
   SerialX::begin();
   arduino.begin();
   randomSeed(analogRead(A0));

   setupMode();
}

void loop()
{
   int32_t modeDelta = arduino.encoderA.delta();

   if (modeDelta != 0)
   {
      int8_t newMode = ((int8_t)currentMode + modeDelta) % NUM_MODES;
      if (newMode < 0)
      {
         newMode += NUM_MODES;
      }
      currentMode = (DisplayMode)newMode;
      setupMode();
   }

   int32_t sizeDelta = arduino.encoderB.delta();

   if (sizeDelta != 0)
   {
      int16_t newSize = constrain((int16_t)numberTextSize + sizeDelta, MIN_NUMBER_TEXT_SIZE, MAX_NUMBER_TEXT_SIZE);
      if (newSize != numberTextSize)
      {
         numberTextSize = (uint8_t)newSize;
         setupMode();
      }
   }

   frameRate.tick();

   unsigned long number = (unsigned long)random(10000000, 100000000);

   switch (currentMode)
   {
      case DisplayMode::DEFAULT_MODE:
         arduino.display.setFont(&fonts::Font0);
         arduino.display.setTextSize(numberTextSize);
         arduino.setCursor(numberX, numberY);
         arduino.print(number, numberFormat, Color::VALUE);
         break;

      case DisplayMode::SMOOTH_FONTS:
         arduino.setTextSize(numberTextSize);
         arduino.setCursor(numberX, numberY);
         arduino.print(number, numberFormat, Color::VALUE);
         break;

      case DisplayMode::SPRITES:
         numberField->draw(number);
         break;
   }

   rateField->draw(frameRate.get());
}
