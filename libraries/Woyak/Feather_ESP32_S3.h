#pragma once

#include <Adafruit_ST7789.h> // TFT display
#include <ArduinoWithDisplay.h>
#include <Button.h>
#include <FixedLengthString.h>
#include <string>
#include <Preferences.h>

//
// Annoyingling, Adafruit has a lot of nifty properties in AdafruitGFX that you can set, but not 
// get. This class is just a wrapper that exposes them
//
class SW_ST7789 : public Adafruit_ST7789
{
public:
   SW_ST7789(int8_t cs, int8_t dc, int8_t mosi, int8_t sclk, int8_t rst = -1) : Adafruit_ST7789(cs, dc, mosi, sclk, rst)
   {
   }

   SW_ST7789(int8_t cs, int8_t dc, int8_t rst) : Adafruit_ST7789(cs, dc, rst)
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

class Feather_ESP32_S3 : public ArduinoWithDisplay
{
public:
   SW_ST7789 display;
   Button buttonA;
   Preferences preferences;

   Feather_ESP32_S3() : ArduinoWithDisplay(&display), display(TFT_CS, TFT_DC, TFT_RST), buttonA(0)
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

      display.init(135, 240); // Init ST7789 240x135
      display.setRotation(3);
      display.fillScreen((uint16_t)Color::BLACK);

      display.setTextColor((uint16_t)Color::WHITE, (uint16_t)Color::BLACK);
      display.setTextSize(2);

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

   void printR(const char* str, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      setCursorX(-strlen(str) * charW());
      print(str, textColor, backgroundColor);
   }

   void printlnR(const char* str, Color textColor = Color::WHITE, Color backgroundColor = Color::BLACK)
   {
      setCursorX(-strlen(str) * charW());
      println(str, textColor, backgroundColor);
   }
};
