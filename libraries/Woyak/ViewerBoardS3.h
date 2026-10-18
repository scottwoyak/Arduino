#pragma once

#include <Preferences.h>
#include <Wire.h>

// LGX_HosyondESP32S3.h must be included before ArduinoWithDisplay.h because
// ArduinoWithDisplay declares a member of type LGFX.
#include "LGX_HosyondESP32S3.h"
#include "ArduinoWithDisplay.h"
#include "Battery.h"
#include "Button.h"
#include "DeviceStateStatus.h"
#include "LED.h"
#include "MultiStatus.h"
#include "Sound.h"
#include "Status.h"

///
/// <summary>
/// ViewerBoardS3 board wrapper. Hosyond ESP32-S3 dev board with a builtin 4" ST7796S
/// TFT display and FT6336U capacitive touch controller, using the BOOT button (GPIO0)
/// as buttonA.
/// </summary>
///
class ViewerBoardS3 : public ArduinoWithDisplay, public IStatus
{
private:
   ///
   /// <summary>
   /// GPIO pin wired to the FT6336U touch controller's RST line. Must be held high for
   /// the controller to operate normally.
   /// </summary>
   ///
   static constexpr uint8_t TOUCH_RESET_PIN = 18;

public:
   static constexpr uint8_t DEFAULT_SDA_PIN = 16;
   static constexpr uint8_t DEFAULT_SCL_PIN = 15;

   ///
   /// <summary>
   /// Onboard battery voltage monitor (GPIO9 via 2:1 voltage divider).
   /// </summary>
   ///
   Battery battery;

   ///
   /// <summary>
   /// BOOT pushbutton, wired to GPIO0.
   /// </summary>
   ///
   Button buttonA;

   ///
   /// <summary>
   /// Non-volatile storage for persisting settings across power cycles.
   /// </summary>
   ///
   Preferences preferences;

   ///
   /// <summary>
   /// Speaker; plays tones and MP3 sounds.
   /// </summary>
   ///
   Sound sound;

   ///
   /// <summary>
   /// Onboard WS2812 NeoPixel LED (GPIO42).
   /// </summary>
   ///
   NeoPixelLED neoPixel;

private:
   NeoPixelStatus _neoPixelStatus;
   DeviceStateStatus _deviceStateStatus;

public:
   ///
   /// <summary>
   /// Status indicator that drives the onboard NeoPixel.
   /// </summary>
   ///
   MultiStatus status;

   ///
   /// <summary>
   /// Initializes a new instance of the ViewerBoardS3 class.
   /// </summary>
   ///
   ViewerBoardS3() : ArduinoWithDisplay(), buttonA(0), _neoPixelStatus(&neoPixel),
      status(&_neoPixelStatus)
   {
      status.addStatus(&_deviceStateStatus);
   }

   ///
   /// <summary>
   /// Initializes the display, the FT6336U touch controller's RST line, the I2C bus,
   /// the speaker, the BOOT button, and the onboard NeoPixel status indicator.
   /// </summary>
   ///
   void begin() override
   {
      pinMode(TOUCH_RESET_PIN, OUTPUT);
      digitalWrite(TOUCH_RESET_PIN, HIGH);

      // On boards with non-standard I2C pins, set them before calling Wire.begin()
      // with no arguments so it picks them up as its defaults.
      Wire.setPins(DEFAULT_SDA_PIN, DEFAULT_SCL_PIN);

      ArduinoWithDisplay::begin();

      Wire.begin();

      sound.begin();
      battery.begin();
      buttonA.begin();
      neoPixel.begin();
      status.begin();
   }

       ///
       /// <summary>
       /// Updates the status indicator to reflect the specified status.
       /// </summary>
       /// <param name="status">The status value to display.</param>
       ///
       void _applyStatus(Status status) override
             {
             this->status.setStatus(status);
          }

          ///
          /// <summary>
          /// Turns the status indicator off.
          /// </summary>
          ///
          void off() override
          {
             this->status.off();
          }
   };
