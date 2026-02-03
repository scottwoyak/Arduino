#pragma once

#include <Adafruit_ST7796S.h> // TFT display
#include <ArduinoWithDisplay.h>
#include <Button.h>
#include <string>
#include <Preferences.h>

//
// Annoyingling, Adafruit has a lot of nifty properties in Adafruit_GFX that you can set, but not 
// get. This class is just a wrapper that exposes them
//
class SW_ST7796S : public Adafruit_ST7796S, public Adafruit_GFX_wInfo
{
public:
   SW_ST7796S(int8_t cs, int8_t dc, int8_t mosi, int8_t sclk, int8_t rst = -1) : Adafruit_ST7796S(cs, dc, mosi, sclk, rst)
   {
   }

   SW_ST7796S(int8_t cs, int8_t dc, int8_t rst) : Adafruit_ST7796S(cs, dc, rst)
   {
   }

   uint8_t charW()
   {
      // 6 is the baseline character width that is scaled for larger text
      return 6 * textsize_x;
   }

   uint8_t charH()
   {
      // 8 is the baseline character width that is scaled for larger text
      return 8 * textsize_y;
   }
};

class Feather_ESP32_S3_ST7796S : public ArduinoWithDisplay
{
private:
   uint8_t _pinBACKLITE;

public:
   SW_ST7796S display;
   Button buttonA;
   Preferences preferences;

   Feather_ESP32_S3_ST7796S(uint8_t pinCS, uint8_t pinDC, uint8_t pinRST, uint8_t pinBACKLITE) : ArduinoWithDisplay(&display, &display), display(pinCS, pinDC, pinRST), buttonA(0)
   {
      _pinBACKLITE = pinBACKLITE;
   }

   void begin()
   {
      // turn on the TFT / I2C power supply
      pinMode(TFT_I2C_POWER, OUTPUT);
      digitalWrite(TFT_I2C_POWER, HIGH);
      delay(10);

      display.init(320, 480, 0, 0, ST7796S_BGR);
      display.setRotation(0);
      display.invertDisplay(true);
      display.fillScreen(ST77XX_BLACK);
      display.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
      display.setTextSize(2);
      display.setTextWrap(false);

      buttonA.begin();

      // initialize TFT
      pinMode(_pinBACKLITE, OUTPUT);
      digitalWrite(_pinBACKLITE, HIGH);

   }

   void displayOn()
   {
      digitalWrite(_pinBACKLITE, HIGH);
   }

   void displayOff()
   {
      digitalWrite(_pinBACKLITE, LOW);
   }

   bool isDisplayOn()
   {
      return digitalRead(_pinBACKLITE) == HIGH;
   }

   void displayLevel(float level)
   {
      uint8_t pwm = constrain(level * 255, 0, 255);
      analogWrite(_pinBACKLITE, pwm);
   }

   void displayDisplay()
   {
      // nothing to do here. The TFT isn't double buffered
   }

   uint8_t charW()
   {
      return display.charW();
   }

   uint8_t charH()
   {
      return display.charH();
   }
};
