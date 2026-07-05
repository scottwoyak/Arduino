#pragma once

#include "ArduinoBase.h"
#include "LED.h"
#include <Preferences.h>

///
/// <summary>
/// WaveShare ESP32-S3-Zero board wrapper. Headless (no onboard display) with an
/// onboard WS2812 NeoPixel LED.
/// </summary>
///
class WaveShare_ESP32_S3_Zero : public ArduinoBase
{
public:
   ///
   /// <summary>
   /// Onboard WS2812 NeoPixel LED.
   /// </summary>
   ///
   NeoPixelLED neoPixel;

   ///
   /// <summary>
   /// Non-volatile key/value storage backed by the ESP32 NVS partition.
   /// </summary>
   ///
   Preferences preferences;

   ///
   /// <summary>
   /// Initializes the onboard NeoPixel LED.
   /// </summary>
   ///
   void begin() override
   {
      neoPixel.begin();
   }
};
