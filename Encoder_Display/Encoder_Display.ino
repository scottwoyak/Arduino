//
// Encoder position and button state display.
//
// Displays real-time encoder position and button status. Button press resets
// encoder position to zero.
//
// Hardware: Feather ESP32 with a rotary encoder (direct GPIO wiring or a Modulino
// Knob module) and a TFT display.
//

#include <Arduino.h>

#include "ArduinoBoard.h"

#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include "QuadratureEncoder.h"
#include "ModulinoEncoder.h"
#include "SerialX.h"

// Set to 1 to use a direct GPIO quadrature encoder, or 0 to use a Modulino Knob
// module connected over I2C.
#define USE_QUADRATURE_ENCODER 0

// ----------- Pins
constexpr uint8_t PIN_A = 9;
constexpr uint8_t PIN_B = 6;
constexpr uint8_t PIN_BUTTON = 5;

// ----------- The Board
Arduino arduino;

#if USE_QUADRATURE_ENCODER
// Direct GPIO quadrature encoder wired to the board.
QuadratureEncoder encoder(PIN_A, PIN_B, PIN_BUTTON);
#else
// Modulino Knob module connected over I2C.
ModulinoEncoder encoder(0xFF);
#endif

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

   // Display encoder type
#if USE_QUADRATURE_ENCODER
   arduino.println("Encoder: Quadrature", Color::HEADING);
#else
   arduino.println("Encoder: Modulino", Color::HEADING);
#endif
   arduino.moveCursorY(2);

   // Display current position
   arduino.println("   Pos:", encoder.getPosition(), posFormat);
   arduino.moveCursorY(2);

   // Display button state
   arduino.println("Button: ", encoder.button.isPressed() ? "True " : "False");
   arduino.moveCursorY(2);

#if USE_QUADRATURE_ENCODER
   // Display phase A/B pin states (only available on the direct GPIO backend).
   arduino.println(" Pin A: ", encoder.isLowA() ? "Low " : "High");
   arduino.println(" Pin B: ", encoder.isLowB() ? "Low " : "High");
#else
   // Pin states aren't available on the Modulino backend, so gray them out.
   arduino.println(" Pin A: ", "N/A", Color::DARKGRAY);
   arduino.println(" Pin B: ", "N/A", Color::DARKGRAY);
#endif
}
