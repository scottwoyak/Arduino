#pragma once

#include <Wire.h>

#include "ArduinoBase.h"
#include "Button.h"
#include "LED.h"
#include "MultiStatus.h"
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
   // The ESP32-S3-Zero's onboard BOOT button, wired to GPIO0.
   static constexpr uint8_t DEFAULT_BUTTON_A_PIN = 0;

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
   /// Onboard BOOT button, on the pin returned by DEFAULT_BUTTON_A_PIN.
   /// </summary>
   ///
   Button buttonA;

   ///
   /// <summary>
   /// Constructs the board wrapper with buttonA on DEFAULT_BUTTON_A_PIN.
   /// </summary>
   ///
   WaveShare_ESP32_S3_Zero() : buttonA(DEFAULT_BUTTON_A_PIN)
   {
   }

   ///
   /// <summary>
   /// Initializes the onboard NeoPixel LED and BOOT button.
   /// </summary>
   ///
   void begin() override
   {
      _checkRunningPartition();

      neoPixel.begin();
      buttonA.begin();
   }
};

///
/// <summary>
/// WaveShare ESP32-S3-Zero board wrapper for sketches wired with a custom I2C bus (with
/// dedicated ground/power rail pins powering the bus) and an RGB LED status indicator.
/// </summary>
///
class WaveShare_ESP32_S3_Zero_Sensors : public WaveShare_ESP32_S3_Zero, public IStatus
{
public:
   // Default pin assignments, matching Wind_Publisher's wiring.
   static constexpr uint8_t DEFAULT_SDA_PIN = 1;
   static constexpr uint8_t DEFAULT_SCL_PIN = 2;
   static constexpr uint8_t DEFAULT_I2C_AUX_GROUND_PIN = 3; // held LOW to power the enclosure temperature sensor
   static constexpr uint8_t DEFAULT_I2C_AUX_POWER_PIN = 4; // held HIGH to power the enclosure temperature sensor
   static constexpr uint8_t DEFAULT_LED_PIN = 6;
   static constexpr uint8_t DEFAULT_RED_LED_PIN = 9;
   static constexpr uint8_t DEFAULT_GREEN_LED_PIN = 7;
   static constexpr uint8_t DEFAULT_BLUE_LED_PIN = 8;

private:
   uint8_t _sdaPin;
   uint8_t _sclPin;
   uint8_t _i2cAuxGroundPin;
   uint8_t _i2cAuxPowerPin;
   uint8_t _ledPin;
   RGBLEDStatus _rgbStatus;
   NeoPixelStatus _neoPixelStatus;

   ///
   /// <summary>
   /// Status indicator that drives both the external RGB LED and the onboard NeoPixel,
   /// so status is visible even when the external LED isn't plugged in.
   /// </summary>
   ///
   MultiStatus _status;

public:
   ///
   /// <summary>
   /// General-purpose LED on the pin returned by ledPin(). Available for sketches that
   /// want a managed LED (begin()/turnOn()/turnOff()/blink()) rather than driving the
   /// pin directly (e.g. as WindMeter does).
   /// </summary>
   ///
   LED led;

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
   /// Gets the pin assigned to the I2C data (SDA) line.
   /// </summary>
   /// <returns>The I2C SDA pin.</returns>
   ///
   uint8_t sdaPin() const
   {
      return _sdaPin;
   }

   ///
   /// <summary>
   /// Gets the pin assigned to the I2C clock (SCL) line.
   /// </summary>
   /// <returns>The I2C SCL pin.</returns>
   ///
   uint8_t sclPin() const
   {
      return _sclPin;
   }

   ///
   /// <summary>
   /// Gets the pin held LOW to power the enclosure temperature sensor's I2C bus.
   /// </summary>
   /// <returns>The I2C auxiliary ground pin.</returns>
   ///
   uint8_t i2cAuxGroundPin() const
   {
      return _i2cAuxGroundPin;
   }

   ///
   /// <summary>
   /// Gets the pin held HIGH to power the enclosure temperature sensor's I2C bus.
   /// </summary>
   /// <returns>The I2C auxiliary power pin.</returns>
   ///
   uint8_t i2cAuxPowerPin() const
   {
      return _i2cAuxPowerPin;
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
        _rgbStatus(redPin, greenPin, bluePin),
        _neoPixelStatus(&neoPixel),
        led(ledPin)
   {
      _status.addStatus(&_rgbStatus);
      _status.addStatus(&_neoPixelStatus);
   }

   ///
   /// <summary>
   /// Powers the enclosure temperature sensor, sets the custom I2C pins, initializes
   /// the base board, and starts the combined status indicator (external RGB LED and
   /// onboard NeoPixel).
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
      _status.begin();
      led.begin();
   }

   ///
   /// <summary>
   /// Updates the combined status indicator (external RGB LED and onboard NeoPixel) to
   /// reflect the specified status.
   /// </summary>
   /// <param name="status">The status value to display.</param>
   ///
   void setStatus(Status status) override
   {
      _status.setStatus(status);
   }
};
