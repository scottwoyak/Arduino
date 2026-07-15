#pragma once

#include "LGX_ST7796S.h"
#include "ArduinoWithDisplay.h"
#include "RotaryEncoder.h"
#include "Button.h"
#include "LED.h"
#include <Preferences.h>

// Pin assignments for all peripherals on this board. Grouped per board variant so
// that every constant relevant to a given board is defined together.
#if defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S3_TFT)
// Avoids LGX_ST7796S's display pins:
//   PIN_TFT_CS  = 5
//   PIN_TFT_DC  = 6
//   PIN_TFT_RST = 9
//   PIN_TFT_BL  = 10
// and the default SPI pins:
//   SCK  = 36
//   MOSI = 35
//   MISO = 37
// and the default I2C pins:
//   SDA = 42
//   SCL = 41
constexpr int8_t PIN_ENCODER_A_PHASE_A = A5;
constexpr int8_t PIN_ENCODER_A_PHASE_B = A4;
constexpr int8_t PIN_ENCODER_A_BUTTON  = A3;
constexpr int8_t PIN_ENCODER_B_PHASE_A = A0;
constexpr int8_t PIN_ENCODER_B_PHASE_B = A1;
constexpr int8_t PIN_ENCODER_B_BUTTON  = A2;
constexpr int8_t PIN_BUTTON_A          = 18;
constexpr int8_t PIN_BUTTON_B          = 21;

#else
// Avoids LGX_ST7796S's display pins:
//   PIN_TFT_CS  = 5
//   PIN_TFT_DC  = 6
//   PIN_TFT_RST = 14
//   PIN_TFT_BL  = 10
// and the default SPI pins:
//   SCK  = 12
//   MOSI = 11
//   MISO = 13
// and the default I2C pins:
//   SDA = 8
//   SCL = 9
constexpr int8_t PIN_ENCODER_A_PHASE_A = 39;
constexpr int8_t PIN_ENCODER_A_PHASE_B = 40;
constexpr int8_t PIN_ENCODER_A_BUTTON  = 38;
constexpr int8_t PIN_ENCODER_B_PHASE_A = 18;
constexpr int8_t PIN_ENCODER_B_PHASE_B = 4;
constexpr int8_t PIN_ENCODER_B_BUTTON  = 3;
constexpr int8_t PIN_BUTTON_A          = 46;
constexpr int8_t PIN_BUTTON_B          = 21;
#endif

///
/// <summary>
/// ESP32-S3 Playground board wrapper. Pairs an LGX_ST7796 TFT display with two rotary
/// encoders and two standalone pushbuttons for interactive experimentation. More
/// peripherals will be added to this board over time.
/// </summary>
///
class ESP32_S3_Playground : public ArduinoWithDisplay
{
public:
   ///
   /// <summary>
   /// Rotary encoder A. See PIN_ENCODER_A_PHASE_A/PIN_ENCODER_A_PHASE_B/PIN_ENCODER_A_BUTTON
   /// for its pin assignments.
   /// </summary>
   ///
   RotaryEncoder encoderA;

   ///
   /// <summary>
   /// Rotary encoder B. See PIN_ENCODER_B_PHASE_A/PIN_ENCODER_B_PHASE_B/PIN_ENCODER_B_BUTTON
   /// for its pin assignments, which vary by board to avoid colliding with the display.
   /// </summary>
   ///
   RotaryEncoder encoderB;

   ///
   /// <summary>
   /// Standalone pushbutton A. See PIN_BUTTON_A for its pin assignment.
   /// </summary>
   ///
   Button buttonA;

   ///
   /// <summary>
   /// Standalone pushbutton B. See PIN_BUTTON_B for its pin assignment.
   /// </summary>
   ///
   Button buttonB;

   ///
   /// <summary>
   /// Non-volatile storage for persisting settings across power cycles.
   /// </summary>
   ///
   Preferences preferences;

   ///
   /// <summary>
   /// Onboard WS2812 NeoPixel LED (GPIO48).
   /// </summary>
   ///
   NeoPixelLED neoPixel;

   ///
   /// <summary>
   /// Initializes a new instance of the ESP32_S3_Playground class.
   /// </summary>
   ///
   ESP32_S3_Playground()
      : ArduinoWithDisplay(),
        encoderA(PIN_ENCODER_A_PHASE_A, PIN_ENCODER_A_PHASE_B, PIN_ENCODER_A_BUTTON),
        encoderB(PIN_ENCODER_B_PHASE_A, PIN_ENCODER_B_PHASE_B, PIN_ENCODER_B_BUTTON),
        buttonA(PIN_BUTTON_A),
        buttonB(PIN_BUTTON_B)
   {
   }

   ///
   /// <summary>
   /// Initializes the display, rotary encoders, and standalone buttons.
   /// </summary>
   ///
   void begin() override
   {
      ArduinoWithDisplay::begin();
      setRotation(DisplayRotation::LANDSCAPE_FLIP);
      encoderA.begin();
      encoderB.begin();
      buttonA.begin();
      buttonB.begin();
      neoPixel.begin();
   }
};

