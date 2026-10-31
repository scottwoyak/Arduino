#pragma once

#include <Preferences.h>
#include <Wire.h>

// LGX_Waveshare_ESP32P4_Touch_LCD_43.h must be included before ArduinoWithDisplay.h
// because ArduinoWithDisplay declares a member of type LGFX.
#include "LGX_Waveshare_ESP32P4_Touch_LCD_43.h"
#include "ArduinoWithDisplay.h"
#include "Battery.h"
#include "Button.h"
#include "DeviceStateStatus.h"
#include "MultiStatus.h"
#include "Sound.h"
#include "Status.h"

///
/// <summary>
/// Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3 board wrapper. 4.3" 480x800 ST7701 MIPI-DSI IPS
/// display with GT911 capacitive touch controller and ES8311 audio codec, using the BOOT
/// button (GPIO35) as buttonA.
/// </summary>
///
class Waveshare_ESP32P4_Touch_LCD_43 : public ArduinoWithDisplay, public IStatus
{
public:
   static constexpr uint8_t DEFAULT_BUTTON_A_PIN = 35;
   static constexpr uint8_t DEFAULT_SDA_PIN = 7;
   static constexpr uint8_t DEFAULT_SCL_PIN = 8;
   static constexpr int8_t DEFAULT_AMP_ENABLE_PIN = 53;
   static constexpr uint8_t DEFAULT_I2S_MCLK_PIN = 13;
   static constexpr uint8_t DEFAULT_I2S_BCLK_PIN = 12;
   static constexpr uint8_t DEFAULT_I2S_DOUT_PIN = 9;
   static constexpr uint8_t DEFAULT_I2S_LRCK_PIN = 10;
   static constexpr uint8_t DEFAULT_I2S_DIN_PIN = 11;

   ///
   /// <summary>
   /// Onboard battery voltage monitor.
   /// </summary>
   ///
   Battery battery;

   ///
   /// <summary>
   /// Onboard BOOT pushbutton, wired to GPIO35.
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
   /// Speaker; plays tones and MP3 sounds via the onboard ES8311 codec.
   /// </summary>
   ///
   Sound sound;

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
   /// Initializes a new instance of the Waveshare_ESP32P4_Touch_LCD_43 class.
   /// </summary>
   ///
   Waveshare_ESP32P4_Touch_LCD_43()
      : ArduinoWithDisplay(),
        buttonA(DEFAULT_BUTTON_A_PIN),
        sound(
           DEFAULT_AMP_ENABLE_PIN,
           DEFAULT_I2S_MCLK_PIN,
           DEFAULT_I2S_BCLK_PIN,
           DEFAULT_I2S_DOUT_PIN,
           DEFAULT_I2S_LRCK_PIN,
           DEFAULT_I2S_DIN_PIN,
           DEFAULT_SDA_PIN,
           DEFAULT_SCL_PIN,
           false,
           false)
   {
      status.addStatus(&_deviceStateStatus);
   }

   ///
   /// <summary>
   /// Initializes the display (defaulting to landscape mode), the speaker, the battery
   /// monitor, and the BOOT button.
   /// </summary>
   ///
   void begin() override
   {
      ArduinoWithDisplay::begin();

      sound.begin();
#ifdef ARDUINO_BATTERY_SUPPORTED
      battery.begin();
#endif
      buttonA.begin();
      status.begin();
   }

   ///
   /// <summary>
   /// Updates the status indicator to reflect the specified status.
   /// </summary>
   /// <param name="status">New status value</param>
   ///
   void _applyStatus(Status status) override
   {
      this->status.setStatus(status);
   }

   ///
   /// <summary>
   /// Turns off the status indicator.
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
   /// <param name="level">Brightness level, from 0.0 (off) to 1.0 (fully on)</param>
   ///
   void displayLevel(float level)
   {
      uint8_t pwm = constrain(level * 255, 0, 255);
      display.setBrightness(pwm);
   }
};
