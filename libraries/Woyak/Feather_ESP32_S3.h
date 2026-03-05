#pragma once

#include <ArduinoWithDisplay.h>
#include <Button.h>
#include <string>
#include <Preferences.h>

#include <TFT_eSPI.h>

class Feather_ESP32_S3 : public ArduinoWithDisplay
{
public:
   TFT_eSPI display;
   Button buttonA;
   Preferences preferences;

   Feather_ESP32_S3() : ArduinoWithDisplay(&display), display(), buttonA(0)
   {
   }

   void begin()
   {
      // initialize TFT
      pinMode(TFT_BACKLITE, OUTPUT);
      digitalWrite(TFT_BACKLITE, HIGH);

      // turn on the TFT / I2C power supply
      pinMode(TFT_I2C_POWER, OUTPUT);
      digitalWrite(TFT_I2C_POWER, HIGH);
      delay(10);

      display.init();
      display.setRotation(1);
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
