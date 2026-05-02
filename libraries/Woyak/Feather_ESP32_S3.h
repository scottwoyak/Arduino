#pragma once

#include <ArduinoWithDisplay.h>
#include <Button.h>
#include <string>
#include <Preferences.h>
#include <Led.h>

class Feather_ESP32_S3 : public ArduinoWithDisplay
{
public:
   Button buttonA;
   Preferences preferences;
   NeoPixelLed neoPixel;

   Feather_ESP32_S3() : ArduinoWithDisplay(), buttonA(0), neoPixel(1, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800)
   {
   }

   void begin()
   {
      display.init();
      display.setRotation(DisplayRotation::LANDSCAPE);
      display.fillScreen((uint16_t)Color::BLACK);

      display.setTextColor((uint16_t)Color::WHITE, (uint16_t)Color::BLACK);
      display.setTextSize(2);
      display.setTextWrap(false);

      buttonA.begin();
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
