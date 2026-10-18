#pragma once

#include <Preferences.h>
#include <Wire.h>

// LGX_Waveshare_ESP32S3_Touch_LCD_28.h must be included before ArduinoWithDisplay.h
// because ArduinoWithDisplay declares a member of type LGFX.
#include "LGX_Waveshare_ESP32S3_Touch_LCD_28.h"
#include "ArduinoWithDisplay.h"
#include "Battery.h"
#include "Button.h"
#include "DeviceStateStatus.h"
#include "MultiStatus.h"
#include "Status.h"

///
/// <summary>
/// Waveshare ESP32-S3-Touch-LCD-2.8 board wrapper. 2.8" 240x320 ST7789T3 TFT display
/// with CST328 capacitive touch controller, using the BOOT button (GPIO0) as buttonA.
/// </summary>
///
class Waveshare_ESP32S3_Touch_LCD_28 : public ArduinoWithDisplay, public IStatus
{
public:
   static constexpr uint8_t DEFAULT_BUTTON_A_PIN = 0;
   static constexpr uint8_t DEFAULT_SDA_PIN = 11;
   static constexpr uint8_t DEFAULT_SCL_PIN = 10;

   ///
   /// <summary>
   /// Onboard battery voltage monitor (GPIO8 via 3:1 voltage divider).
   /// </summary>
   ///
   Battery battery;

   ///
   /// <summary>
   /// Onboard BOOT pushbutton, wired to GPIO0.
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
   /// Status indicator; this board has no physical NeoPixel, so no underlying
   /// indicators are wired in for now.
   /// </summary>
   ///
   MultiStatus status;

   DeviceStateStatus _deviceStateStatus;

   ///
   /// <summary>
   /// Initializes a new instance of the Waveshare_ESP32S3_Touch_LCD_28 class.
   /// </summary>
   ///
   Waveshare_ESP32S3_Touch_LCD_28() : ArduinoWithDisplay(), buttonA(DEFAULT_BUTTON_A_PIN)
   {
      status.addStatus(&_deviceStateStatus);
   }

   ///
   /// <summary>
   /// Initializes the display (defaulting to portrait mode), the I2C bus, the battery
   /// monitor, and the BOOT button.
   /// </summary>
   ///
   void begin() override
   {
      Wire.setPins(DEFAULT_SDA_PIN, DEFAULT_SCL_PIN);

      ArduinoWithDisplay::begin();
      display.setRotation(DisplayRotation::PORTRAIT);

      Wire.begin();

      battery.begin();
      buttonA.begin();
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

   ///
   /// <summary>
   /// Turns the backlight fully on.
   /// </summary>
   ///
   void displayOn()
   {
      display.setBrightness(255);
   }

   ///
   /// <summary>
   /// Turns the backlight fully off.
   /// </summary>
   ///
   void displayOff()
   {
      display.setBrightness(0);
   }

   ///
   /// <summary>
   /// Sets the backlight brightness.
   /// </summary>
   /// <param name="level">Brightness level, from 0.0 (off) to 1.0 (fully on).</param>
   ///
   void displayLevel(float level)
   {
      uint8_t pwm = constrain(level * 255, 0, 255);
      display.setBrightness(pwm);
   }
};
