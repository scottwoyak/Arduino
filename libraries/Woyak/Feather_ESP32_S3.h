#pragma once

#include "LGX_FeatherESP32_S3_TFT.h"
#include "ArduinoWithDisplay.h"
#include "Button.h"
#include "MultiStatus.h"
#include <string>
#include <Preferences.h>
#include "LED.h"
#include "Status.h"
#include "LGFXUtil.h"

class Feather_ESP32_S3 : public ArduinoWithDisplay
{
public:
   Button buttonA;
   Preferences preferences;
   NeoPixelLED neoPixel;
   LED led{ LED_BUILTIN };

private:
   NeoPixelStatus _neoPixelStatus;

public:
   ///
   /// <summary>
   /// Status indicator that drives the onboard NeoPixel.
   /// </summary>
   ///
   MultiStatus status;

   Feather_ESP32_S3() : ArduinoWithDisplay(), buttonA(0), _neoPixelStatus(&neoPixel),
      status(&_neoPixelStatus)
   {
   }


   void begin()
   {
      ArduinoWithDisplay::begin();

      buttonA.begin();
      neoPixel.begin();
      led.begin();
      status.begin();
   }

   void displayOn()
   {
      digitalWrite(TFT_BACKLITE, HIGH);
   }

   void displayOff()
   {
      digitalWrite(TFT_BACKLITE, LOW);
   }

   bool isDisplayOn()
   {
      return digitalRead(TFT_BACKLITE) == HIGH;
   }

   void displayLevel(float level)
   {
      uint8_t pwm = constrain(level * 255, 0, 255);
      analogWrite(TFT_BACKLITE, pwm);
   }

   void deepSleep(float seconds)
   {
      esp_sleep_enable_timer_wakeup(seconds * 1000000); 
      esp_deep_sleep_start();
   }
};
