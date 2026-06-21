#pragma once

#include <Arduino.h>
#include <I2C.h>
#include <TM1637Display.h>
#include <Adafruit_LEDBackpack.h>
#include <math.h>

/// <summary>
/// Defines the contract for seven-segment display implementations.
/// </summary>
class ISevenSegment
{
public:
   /// <summary>
   /// Virtual destructor for interface cleanup.
   /// </summary>
   virtual ~ISevenSegment() = default;

   /// <summary>
   /// Initializes communication with the display.
   /// </summary>
   /// <returns>True when initialization succeeds; otherwise false.</returns>
   virtual bool begin() = 0;

   /// <summary>
   /// Sets display brightness.
   /// </summary>
   /// <param name="brightness">Brightness level from 0.0 to 1.0, where 0.0 is off.</param>
   virtual void setBrightness(float brightness) = 0;

   /// <summary>
   /// Clears the display.
   /// </summary>
   virtual void clear() = 0;

   /// <summary>
   /// Prints a floating-point value with a specified number of decimals.
   /// </summary>
   /// <param name="value">Value to print.</param>
   /// <param name="numDecimals">Number of decimal places to render (0-3). Defaults to 1.</param>
   virtual void print(float value, int numDecimals = 1) = 0;
};

/// <summary>
/// Wrapper for a 4-digit TM1637 seven-segment display.
/// </summary>
class SevenSegmentTM1637 : public ISevenSegment
{
private:
   TM1637Display _display;

public:
   /// <summary>
   /// Initializes a SevenSegmentTM1637 display wrapper.
   /// </summary>
   /// <param name="clkPin">Clock pin connected to TM1637 CLK.</param>
   /// <param name="dataPin">Data pin connected to TM1637 DIO.</param>
   /// <param name="bitDelayUs">TM1637 bit delay in microseconds.</param>
   SevenSegmentTM1637(uint8_t clkPin=SCL, uint8_t dataPin=SDA, unsigned int bitDelayUs = 1u)
      : _display(clkPin, dataPin, bitDelayUs)
   {
   }

   /// <summary>
   /// Initializes communication with the display.
   /// </summary>
   /// <returns>True when initialization succeeds; otherwise false.</returns>
   bool begin() override
   {
      return true;
   }

   /// <summary>
   /// Sets display brightness.
   /// </summary>
   /// <param name="brightness">Brightness level from 0.0 to 1.0, where 0.0 is off.</param>
   void setBrightness(float brightness) override
   {
      float clamped = constrain(brightness, 0.0f, 1.0f);

      uint8_t mapped = (uint8_t)lroundf(clamped * 7.0f);
      _display.setBrightness(mapped, clamped > 0.0f);
   }

   /// <summary>
   /// Clears the display.
   /// </summary>
   void clear() override
   {
      _display.clear();
   }

   /// <summary>
   /// Prints a floating-point value with a specified number of decimals.
   /// </summary>
   /// <param name="value">Value to print.</param>
   /// <param name="numDecimals">Number of decimal places to render (0-3). Defaults to 1.</param>
   void print(float value, int numDecimals = 1) override
   {
      numDecimals = constrain(numDecimals, 0, 3);

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

/// <summary>
/// Wrapper for a 4-digit VK16K33 seven-segment display using Adafruit_7segment.
/// </summary>
class SevenSegmentVK16K33 : public ISevenSegment
{
public:
   static constexpr uint8_t DEFAULT_I2C_ADDRESS = 0x70;

private:
   Adafruit_7segment _display;

public:
   /// <summary>
   /// Initializes a SevenSegmentVK16K33 display wrapper.
   /// </summary>
   SevenSegmentVK16K33()
      : _display()
   {
   }

   /// <summary>
   /// Initializes communication with the display.
   /// </summary>
   /// <returns>True when initialization succeeds; otherwise false.</returns>
   bool begin() override
   {
      bool started = _display.begin(DEFAULT_I2C_ADDRESS);
      if (started)
      {
         clear();
      }

      return started;
   }

   /// <summary>
   /// Sets display brightness.
   /// </summary>
   /// <param name="brightness">Brightness level from 0.0 to 1.0, where 0.0 is off.</param>
   void setBrightness(float brightness) override
   {
      float clamped = constrain(brightness, 0.0f, 1.0f);
      uint8_t mapped = (uint8_t)lroundf(clamped * 15.0f);
      _display.setBrightness(mapped);
      _display.setDisplayState(clamped > 0.0f);
   }

   /// <summary>
   /// Clears the display.
   /// </summary>
   void clear() override
   {
      _display.clear();
      _display.writeDisplay();
   }

   /// <summary>
   /// Prints a floating-point value with a specified number of decimals.
   /// </summary>
   /// <param name="value">Value to print.</param>
   /// <param name="numDecimals">Number of decimal places to render (0-3). Defaults to 1.</param>
   void print(float value, int numDecimals = 1) override
   {
      numDecimals = constrain(numDecimals, 0, 3);

      _display.print((double)value, numDecimals);
      _display.writeDisplay();
   }
};

/// <summary>
/// Factory for creating seven-segment display implementations.
/// </summary>
class SevenSegmentFactory
{
public:
   /// <summary>
   /// Creates a seven-segment display instance by auto-detecting I2C presence.
   /// </summary>
   /// <param name="clkPin">Clock pin for TM1637 fallback.</param>
   /// <param name="dataPin">Data pin for TM1637 fallback.</param>
   /// <returns>An allocated ISevenSegment implementation. Caller owns and must delete it.</returns>
   static ISevenSegment* create(uint8_t clkPin = SCL, uint8_t dataPin = SDA)
   {
      if (I2C::exists(SevenSegmentVK16K33::DEFAULT_I2C_ADDRESS))
      {
         Serial.println("SevenSegmentFactory: detected SevenSegmentVK16K33");
         return new SevenSegmentVK16K33();
      }

      Serial.println("SevenSegmentFactory: detected SevenSegmentTM1637");
      return new SevenSegmentTM1637(clkPin, dataPin);
   }
};
