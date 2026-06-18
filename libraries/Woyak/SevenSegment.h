#pragma once

#include <Arduino.h>
#include <TM1637Display.h>
#include <math.h>

/// <summary>
/// Wrapper for a 4-digit TM1637 seven-segment display.
/// </summary>
class SevenSegment
{
private:
   TM1637Display _display;
   uint8_t _brightness;
   bool _isOn;

public:
   /// <summary>
   /// Initializes a SevenSegment display wrapper.
   /// </summary>
   /// <param name="clkPin">Clock pin connected to TM1637 CLK.</param>
   /// <param name="dataPin">Data pin connected to TM1637 DIO.</param>
   /// <param name="brightness">Brightness level from 0 to 7.</param>
   /// <param name="bitDelayUs">TM1637 bit delay in microseconds.</param>
   SevenSegment(uint8_t clkPin, uint8_t dataPin, uint8_t brightness = 7, unsigned int bitDelayUs = 1u)
      : _display(clkPin, dataPin, bitDelayUs), _brightness(brightness), _isOn(true)
   {
      _display.setBrightness(_brightness, true);
   }

   /// <summary>
   /// Sets display brightness.
   /// </summary>
   /// <param name="brightness">Brightness level from 0 to 7.</param>
   void setBrightness(uint8_t brightness)
   {
      _brightness = brightness;
      _display.setBrightness(_brightness, _isOn);
   }

   /// <summary>
   /// Turns on the display output.
   /// </summary>
   void turnOn()
   {
      _isOn = true;
      _display.setBrightness(_brightness, true);
   }

   /// <summary>
   /// Turns off the display output.
   /// </summary>
   void turnOff()
   {
      _isOn = false;
      _display.setBrightness(_brightness, false);
   }

   /// <summary>
   /// Clears the display.
   /// </summary>
   void clear()
   {
      _display.clear();
   }

   /// <summary>
   /// Displays a floating-point value with a specified number of decimals.
   /// </summary>
   /// <param name="value">Value to display.</param>
   /// <param name="numDecimals">Number of decimal places to render (0-3).</param>
   void display(float value, int numDecimals)
   {
      if (numDecimals < 0)
      {
         numDecimals = 0;
      }
      if (numDecimals > 3)
      {
         numDecimals = 3;
      }

      static const int32_t multipliers[] = { 1, 10, 100, 1000 };
      int32_t scaled = (int32_t)lroundf(value * multipliers[numDecimals]);

      uint8_t dots = 0;
      if (numDecimals > 0)
      {
         dots = (uint8_t)(0x80 >> (3 - numDecimals));
      }

      _display.showNumberDecEx(scaled, dots, false);
   }
};
