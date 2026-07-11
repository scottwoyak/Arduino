#pragma once

#include "LGX_ST7796S.h"
#include "ArduinoWithDisplay.h"
#include "RotaryEncoder.h"
#include <Preferences.h>

///
/// <summary>
/// ESP32-S3 Playground board wrapper. Pairs an LGX_ST7796 TFT display with a
/// rotary encoder for interactive experimentation. More peripherals will be added to
/// this board over time.
/// </summary>
///
class ESP32_S3_Playground : public ArduinoWithDisplay
{
public:
   #if defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S3_TFT) || defined(ARDUINO_ESP32_DEV)
   ///
   /// <summary>
   /// Rotary encoder wired to pins A4 (phase A), A5 (phase B), and A3 (integral button).
   /// </summary>
   ///
   RotaryEncoder encoder;
#else
   ///
   /// <summary>
   /// Rotary encoder wired to pins A0 (phase A), A1 (phase B), and A2 (integral button).
   /// A4/A5 are avoided since they collide with the display's CS/DC pins on this board.
   /// </summary>
   ///
   RotaryEncoder encoder;
#endif

   ///
   /// <summary>
   /// Non-volatile storage for persisting settings across power cycles.
   /// </summary>
   ///
   Preferences preferences;

   ///
   /// <summary>
   /// Initializes a new instance of the ESP32_S3_Playground class.
   /// </summary>
   ///
   #if defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S3_TFT) || defined(ARDUINO_ESP32_DEV)
   ESP32_S3_Playground() : ArduinoWithDisplay(), encoder(A4, A5, A3)
   {
   }
#else
   ESP32_S3_Playground() : ArduinoWithDisplay(), encoder(A0, A1, A2)
   {
   }
#endif

   ///
   /// <summary>
   /// Initializes the display and rotary encoder.
   /// </summary>
   ///
   void begin() override
   {
      ArduinoWithDisplay::begin();
      setRotation(DisplayRotation::LANDSCAPE_FLIP);
      encoder.begin();
   }
};
