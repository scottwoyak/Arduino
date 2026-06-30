#pragma once

#include "ArduinoWithDisplay.h"
#include "Button.h"
#include <Adafruit_ST7796S.h>
#include <Preferences.h>
#include <string>

/// <summary>
/// ST7796S wrapper that exposes display metrics from Adafruit_GFX internals.
/// </summary>
class SW_ST7796S : public Adafruit_ST7796S, public Adafruit_GFX_wInfo
{
public:
   /// <summary>
   /// Initializes a software SPI ST7796S display instance.
   /// </summary>
   /// <param name="cs">Chip select pin.</param>
   /// <param name="dc">Data/command pin.</param>
   /// <param name="mosi">MOSI pin.</param>
   /// <param name="sclk">Clock pin.</param>
   /// <param name="rst">Reset pin, or -1 when unused.</param>
   SW_ST7796S(int8_t cs, int8_t dc, int8_t mosi, int8_t sclk, int8_t rst = -1) : Adafruit_ST7796S(cs, dc, mosi, sclk, rst)
   {
   }

   /// <summary>
   /// Initializes a hardware SPI ST7796S display instance.
   /// </summary>
   /// <param name="cs">Chip select pin.</param>
   /// <param name="dc">Data/command pin.</param>
   /// <param name="rst">Reset pin.</param>
   SW_ST7796S(int8_t cs, int8_t dc, int8_t rst) : Adafruit_ST7796S(cs, dc, rst)
   {
   }

   /// <summary>
   /// Gets the current character width in pixels.
   /// </summary>
   /// <returns>Character width in pixels.</returns>
   uint8_t charW()
   {
      return 6 * textsize_x;
   }

   /// <summary>
   /// Gets the current character height in pixels.
   /// </summary>
   /// <returns>Character height in pixels.</returns>
   uint8_t charH()
   {
      return 8 * textsize_y;
   }
};

/// <summary>
/// Feather ESP32-S3 board wrapper with ST7796S display and button support.
/// </summary>
class Feather_ESP32_S3_ST7796S : public ArduinoWithDisplay
{
private:
   /// <summary>
   /// Backlight control pin.
   /// </summary>
   uint8_t _pinBACKLITE;

public:
   /// <summary>
   /// Display driver instance.
   /// </summary>
   SW_ST7796S display;

   /// <summary>
   /// Primary user button.
   /// </summary>
   Button buttonA;

   /// <summary>
   /// Persistent key-value storage.
   /// </summary>
   Preferences preferences;

   /// <summary>
   /// Initializes a Feather ESP32-S3 ST7796S wrapper.
   /// </summary>
   /// <param name="pinCS">Display chip select pin.</param>
   /// <param name="pinDC">Display data/command pin.</param>
   /// <param name="pinRST">Display reset pin.</param>
   /// <param name="pinBACKLITE">Display backlight control pin.</param>
   Feather_ESP32_S3_ST7796S(uint8_t pinCS, uint8_t pinDC, uint8_t pinRST, uint8_t pinBACKLITE) : ArduinoWithDisplay(&display, &display), display(pinCS, pinDC, pinRST), buttonA(0)
   {
      _pinBACKLITE = pinBACKLITE;
   }

   /// <summary>
   /// Initializes the display hardware and related peripherals.
   /// </summary>
   void begin()
   {
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

      pinMode(_pinBACKLITE, OUTPUT);
      digitalWrite(_pinBACKLITE, HIGH);
   }

   /// <summary>
   /// Turns the display backlight on.
   /// </summary>
   void displayOn()
   {
      digitalWrite(_pinBACKLITE, HIGH);
   }

   /// <summary>
   /// Turns the display backlight off.
   /// </summary>
   void displayOff()
   {
      digitalWrite(_pinBACKLITE, LOW);
   }

   /// <summary>
   /// Indicates whether the display backlight is currently on.
   /// </summary>
   /// <returns>True when the backlight pin is high; otherwise false.</returns>
   bool isDisplayOn()
   {
      return digitalRead(_pinBACKLITE) == HIGH;
   }

   /// <summary>
   /// Sets the display backlight level from 0.0 to 1.0.
   /// </summary>
   /// <param name="level">Brightness level where 0.0 is off and 1.0 is full brightness.</param>
   void displayLevel(float level)
   {
      uint8_t pwm = constrain(level * 255, 0, 255);
      analogWrite(_pinBACKLITE, pwm);
   }

   /// <summary>
   /// Flushes any pending display updates.
   /// </summary>
   void displayDisplay()
   {
      // Nothing to do; this display is not double buffered.
   }

   /// <summary>
   /// Gets the current character width in pixels.
   /// </summary>
   /// <returns>Character width in pixels.</returns>
   uint8_t charW()
   {
      return display.charW();
   }

   /// <summary>
   /// Gets the current character height in pixels.
   /// </summary>
   /// <returns>Character height in pixels.</returns>
   uint8_t charH()
   {
      return display.charH();
   }
};
