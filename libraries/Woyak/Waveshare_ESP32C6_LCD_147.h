#pragma once

#include <Preferences.h>
#include <Wire.h>

// LGX_Waveshare_ESP32C6_LCD_147.h must be included before ArduinoWithDisplay.h
// because ArduinoWithDisplay declares a member of type LGFX.
#include "LGX_Waveshare_ESP32C6_LCD_147.h"
#include "ArduinoWithDisplay.h"
#include "Battery.h"
#include "Button.h"
#include "DeviceStateStatus.h"
#include "LED.h"
#include "MultiStatus.h"
#include "Status.h"

///
/// <summary>
/// Waveshare ESP32-C6-LCD-1.47 board wrapper. 1.47" 172x320 ST7789 IPS TFT display
/// (non-touch) with an onboard WS2812 RGB LED (GPIO8) and BOOT button (GPIO9) as buttonA.
/// </summary>
///
class Waveshare_ESP32C6_LCD_147 : public ArduinoWithDisplay, public IStatus
{
private:
   NeoPixelStatus _neoPixelStatus;
   DeviceStateStatus _deviceStateStatus;

public:
   static constexpr uint8_t DEFAULT_BUTTON_A_PIN = 9;

   ///
   /// <summary>
   /// Battery voltage monitor (disabled by default on this board unless externally wired).
   /// </summary>
   ///
   Battery battery;

   ///
   /// <summary>
   /// Onboard BOOT pushbutton, wired to GPIO9 on ESP32-C6.
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
   /// Onboard WS2812 RGB NeoPixel LED (GPIO8).
   /// </summary>
   ///
   NeoPixelLED neoPixel;

   ///
   /// <summary>
   /// Status indicator that drives the onboard NeoPixel and device state status.
   /// </summary>
   ///
   MultiStatus status;

   ///
   /// <summary>
   /// Initializes a new instance of the Waveshare_ESP32C6_LCD_147 class.
   /// </summary>
   ///
   Waveshare_ESP32C6_LCD_147()
      : ArduinoWithDisplay(),
        _neoPixelStatus(&neoPixel),
        buttonA(DEFAULT_BUTTON_A_PIN),
        status(&_neoPixelStatus)
   {
      status.addStatus(&_deviceStateStatus);
   }

   ///
   /// <summary>
   /// Initializes the display, the I2C bus, the onboard NeoPixel, and the BOOT button.
   /// </summary>
   ///
   void begin() override
   {
      ArduinoWithDisplay::begin();

      Wire.begin();

#ifdef ARDUINO_BATTERY_SUPPORTED
      battery.begin();
#endif
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
