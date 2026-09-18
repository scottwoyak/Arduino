#pragma once

#include "../LovyanGFX/src/lgfx_user/LGFX_Waveshare_ESP32S3_Touch_LCD_43.h"
#include "ArduinoWithDisplay.h"
#include "Button.h"
#include "MultiStatus.h"
#include "Status.h"
#include <Preferences.h>

///
/// <summary>
/// Waveshare ESP32-S3-Touch-LCD-4.3B board wrapper. 800x480 RGB parallel display with
/// GT911 capacitive touch controller, driven through a CH422G I/O expander (also used
/// for backlight control).
/// </summary>
///
class Waveshare_ESP32S3_Touch_LCD_43 : public ArduinoWithDisplay
{
public:
   // Onboard BOOT button, wired to GPIO0.
   static constexpr uint8_t DEFAULT_BUTTON_A_PIN = 0;

   Preferences preferences;
   Button buttonA;

   ///
   /// <summary>
   /// Status indicator; this board has no physical NeoPixel, so no underlying
   /// indicators are wired in for now.
   /// </summary>
   ///
   MultiStatus status;

   Waveshare_ESP32S3_Touch_LCD_43() : ArduinoWithDisplay(), buttonA(DEFAULT_BUTTON_A_PIN)
   {
   }

   void begin() override
   {
      ArduinoWithDisplay::begin();

      // The RGB panel is wired physically landscape (800x480 native), so undo the
      // LANDSCAPE rotation ArduinoWithDisplay::begin() applies by default; rotation 0
      // (PORTRAIT) is this panel's native, already-landscape orientation. The panel is
      // also mounted upside down relative to that native orientation, so flip 180.
      display.setRotation(DisplayRotation::PORTRAIT_FLIP);

      buttonA.begin();
      status.begin();
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
