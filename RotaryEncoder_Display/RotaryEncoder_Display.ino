//
// Rotary encoder position and button state display.
//
// Displays real-time rotary encoder position, pin states (A/B high/low), and button status.
// Button press resets encoder position to zero.
//
// Hardware: Feather ESP32 with rotary encoder on GPIO pins and TFT display.
//

#include <Arduino.h>

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include "RotaryEncoder.h"
#include "SerialX.h"
#include "Stopwatch.h"

// ----------- Pins
constexpr uint8_t PIN_A = 9;
constexpr uint8_t PIN_B = 6;
constexpr uint8_t PIN_BUTTON = 5;

// ----------- The Board
Arduino arduino;
RotaryEncoder encoder(PIN_A, PIN_B, PIN_BUTTON);

// ----------- Display formatting
Format posFormat("+######");

void setup()
{
   SerialX::begin();
   arduino.begin();
   encoder.begin();
}

void loop()
{
   // Reset position on button press
   if (encoder.button.wasPressed())
   {
      encoder.setPosition(0);
   }

   arduino.setCursor(0, 0);
   arduino.setTextSize(3);

   // Display pin A state
   arduino.println("     A: ", encoder.isLowA() ? "Low " : "High");
   arduino.moveCursorY(2);

   // Display pin B state
   arduino.println("     B: ", encoder.isLowB() ? "Low " : "High");
   arduino.moveCursorY(2);

   // Display current position
   arduino.println("   Pos:", encoder.getPosition(), posFormat);
   arduino.moveCursorY(2);

   // Display button state
   arduino.println("Button: ", encoder.button.isPressed() ? "True " : "False");

   // Display pin configuration
   arduino.setTextSize(2);
   arduino.setCursorY(-arduino.charH());
   arduino.print("A:", encoder.getPinA());
   arduino.print("  B:", encoder.getPinB());
   arduino.println("  Button:", encoder.getButtonPin());
}
