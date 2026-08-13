#pragma once

#include <Wire.h>

#include "ArduinoBase.h"
#include "LED.h"
#include "Status.h"
#include <Preferences.h>

///
/// <summary>
/// WaveShare ESP32-S3-Zero board wrapper. Headless (no onboard display) with an
/// onboard WS2812 NeoPixel LED.
/// </summary>
///
class WaveShare_ESP32_S3_Zero : public ArduinoBase
{
public:
   ///
   /// <summary>
   /// Onboard WS2812 NeoPixel LED.
   /// </summary>
   ///
   NeoPixelLED neoPixel;

   ///
   /// <summary>
   /// Non-volatile key/value storage backed by the ESP32 NVS partition.
   /// </summary>
   ///
   Preferences preferences;

   ///
   /// <summary>
   /// Initializes the onboard NeoPixel LED.
   /// </summary>
   ///
   void begin() override
   {
      neoPixel.begin();
   }
};

///
/// <summary>
/// WaveShare ESP32-S3-Zero board wrapper for sketches wired with a custom I2C bus (with
/// dedicated ground/power rail pins powering the bus) and an RGB LED status indicator.
/// </summary>
///
class WaveShare_ESP32_S3_Zero_Sensors : public WaveShare_ESP32_S3_Zero
{
private:
   // Default pin assignments, matching Wind_Publisher's wiring.
   static constexpr uint8_t DEFAULT_SDA_PIN = 1;
   static constexpr uint8_t DEFAULT_SCL_PIN = 2;
   static constexpr uint8_t DEFAULT_I2C_AUX_GROUND_PIN = 3; // held LOW to power the enclosure temperature sensor
   static constexpr uint8_t DEFAULT_I2C_AUX_POWER_PIN = 4; // held HIGH to power the enclosure temperature sensor
   static constexpr uint8_t DEFAULT_LED_PIN = 6;
   static constexpr uint8_t DEFAULT_RED_LED_PIN = 9;
   static constexpr uint8_t DEFAULT_GREEN_LED_PIN = 7;
   static constexpr uint8_t DEFAULT_BLUE_LED_PIN = 8;

   uint8_t _sdaPin;
   uint8_t _sclPin;
   uint8_t _i2cAuxGroundPin;
   uint8_t _i2cAuxPowerPin;
   uint8_t _ledPin;

public:
   ///
   /// <summary>
   /// RGB LED status indicator used to represent startup and connectivity states.
   /// </summary>
   ///
   RGBLEDStatus status;

   ///
   /// <summary>
   /// Gets the pin assigned to the general-purpose LED (e.g. for use by a WindMeter or
   /// similar peripheral that drives its own LED directly).
   /// </summary>
   /// <returns>The general-purpose LED pin.</returns>
   ///
   uint8_t ledPin() const
   {
      return _ledPin;
   }

   ///
   /// <summary>
   /// Initializes a new instance of the WaveShare_ESP32_S3_Zero_Sensors class. Defaults
   /// match Wind_Publisher's wiring.
   /// </summary>
   /// <param name="sdaPin">I2C data pin</param>
   /// <param name="sclPin">I2C clock pin</param>
   /// <param name="i2cAuxGroundPin">Pin held LOW to power the enclosure temperature sensor</param>
   /// <param name="i2cAuxPowerPin">Pin held HIGH to power the enclosure temperature sensor</param>
   /// <param name="ledPin">Pin for the general-purpose LED (see ledPin())</param>
   /// <param name="redPin">Red channel pin for the RGB status LED</param>
   /// <param name="greenPin">Green channel pin for the RGB status LED</param>
   /// <param name="bluePin">Blue channel pin for the RGB status LED</param>
   ///
   WaveShare_ESP32_S3_Zero_Sensors(
      uint8_t sdaPin = DEFAULT_SDA_PIN,
      uint8_t sclPin = DEFAULT_SCL_PIN,
      uint8_t i2cAuxGroundPin = DEFAULT_I2C_AUX_GROUND_PIN,
      uint8_t i2cAuxPowerPin = DEFAULT_I2C_AUX_POWER_PIN,
      uint8_t ledPin = DEFAULT_LED_PIN,
      uint8_t redPin = DEFAULT_RED_LED_PIN,
      uint8_t greenPin = DEFAULT_GREEN_LED_PIN,
      uint8_t bluePin = DEFAULT_BLUE_LED_PIN)
      : _sdaPin(sdaPin),
        _sclPin(sclPin),
        _i2cAuxGroundPin(i2cAuxGroundPin),
        _i2cAuxPowerPin(i2cAuxPowerPin),
        _ledPin(ledPin),
        status(redPin, greenPin, bluePin)
   {
   }

   ///
   /// <summary>
   /// Powers the enclosure temperature sensor, sets the custom I2C pins, initializes
   /// the base board, and starts the RGB status LED.
   /// </summary>
   ///
   void begin() override
   {
      pinMode(_i2cAuxGroundPin, OUTPUT);
      pinMode(_i2cAuxPowerPin, OUTPUT);
      digitalWrite(_i2cAuxGroundPin, LOW);
      digitalWrite(_i2cAuxPowerPin, HIGH);

      // On boards with non-standard I2C pins, set them before calling Wire.begin()
      // with no arguments so it picks them up as its defaults.
      Wire.setPins(_sdaPin, _sclPin);
      Wire.begin();

      WaveShare_ESP32_S3_Zero::begin();
      status.begin();
   }
};
