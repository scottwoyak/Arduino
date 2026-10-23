#pragma once

#include <Wire.h>

#include "ArduinoBase.h"
#include "Button.h"
#include "DeviceStateStatus.h"
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
      neoPixel.begin();
      buttonA.begin();
   }
};

///
/// <summary>
/// Common base class for WaveShare ESP32-S3-Zero sensor boards wired with a custom I2C
/// bus (with dedicated ground/power rail pins powering the enclosure sensor) and a
/// combined MultiStatus indicator that includes the onboard NeoPixel and DeviceStateStatus.
/// </summary>
///
class WaveShare_ESP32_S3_Zero_SensorsBase : public WaveShare_ESP32_S3_Zero, public IStatus
{
public:
   // Default pin assignments, matching Wind_Publisher's wiring.
   static constexpr uint8_t DEFAULT_SDA_PIN = 1;
   static constexpr uint8_t DEFAULT_SCL_PIN = 2;
   static constexpr uint8_t DEFAULT_I2C_AUX_GROUND_PIN = 3; // held LOW to power the enclosure temperature sensor
   static constexpr uint8_t DEFAULT_I2C_AUX_POWER_PIN = 4; // held HIGH to power the enclosure temperature sensor
   static constexpr uint8_t DEFAULT_LED_PIN = 6;

protected:
   uint8_t _sdaPin;
   uint8_t _sclPin;
   uint8_t _i2cAuxGroundPin;
   uint8_t _i2cAuxPowerPin;
   uint8_t _ledPin;
   NeoPixelStatus _neoPixelStatus;
   DeviceStateStatus _deviceStateStatus;

   ///
   /// <summary>
   /// Combined status indicator driving the board's external status LED(s), the onboard
   /// NeoPixel, and DeviceStateStatus.
   /// </summary>
   ///
   MultiStatus _status;

public:
   ///
   /// <summary>
   /// Gets the pin assigned to the external LED or NeoPixel strip.
   /// </summary>
   /// <returns>The external LED pin.</returns>
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
   /// Initializes the shared I2C and status base state for a Waveshare ESP32-S3-Zero
   /// sensor board.
   /// </summary>
   /// <param name="sdaPin">I2C data pin</param>
   /// <param name="sclPin">I2C clock pin</param>
   /// <param name="i2cAuxGroundPin">Pin held LOW to power the enclosure temperature sensor</param>
   /// <param name="i2cAuxPowerPin">Pin held HIGH to power the enclosure temperature sensor</param>
   /// <param name="ledPin">Pin for the external LED or NeoPixel strip</param>
   ///
   WaveShare_ESP32_S3_Zero_SensorsBase(
      uint8_t sdaPin,
      uint8_t sclPin,
      uint8_t i2cAuxGroundPin,
      uint8_t i2cAuxPowerPin,
      uint8_t ledPin)
      : _sdaPin(sdaPin),
        _sclPin(sclPin),
        _i2cAuxGroundPin(i2cAuxGroundPin),
        _i2cAuxPowerPin(i2cAuxPowerPin),
        _ledPin(ledPin),
        _neoPixelStatus(&neoPixel)
   {
      _status.addStatus(&_neoPixelStatus);
      _status.addStatus(&_deviceStateStatus);
   }

   ///
   /// <summary>
   /// Adds an additional status indicator (e.g. a sketch-specific single LED wired to
   /// its own pins) to the combined status indicator, so it reflects the same status as
   /// the board's status LEDs. Must be called before begin() so the added indicator's
   /// begin() is invoked.
   /// </summary>
   /// <param name="status">The status indicator to add.</param>
   ///
   void addStatus(IStatus* status)
   {
      _status.addStatus(status);
   }

   ///
   /// <summary>
   /// Powers the enclosure temperature sensor, sets the custom I2C pins, and initializes
   /// the base WaveShare_ESP32_S3_Zero board.
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
   }

   ///
   /// <summary>
   /// Updates the combined status indicator to reflect the specified status.
   /// </summary>
   /// <param name="status">The status value to display.</param>
   ///
   void _applyStatus(Status status) override
   {
      _status.setStatus(status);
   }

   ///
   /// <summary>
   /// Turns the combined status indicator off.
   /// </summary>
   ///
   void off() override
   {
      _status.off();
   }
};

///
/// <summary>
/// WaveShare ESP32-S3-Zero board wrapper for sketches wired with a custom I2C bus (with
/// dedicated ground/power rail pins powering the bus), a 3-pin RGB LED status indicator,
/// and a standalone general-purpose LED.
/// </summary>
///
class WaveShare_ESP32_S3_Zero_Sensors : public WaveShare_ESP32_S3_Zero_SensorsBase
{
public:
   static constexpr uint8_t DEFAULT_RED_LED_PIN = 9;
   static constexpr uint8_t DEFAULT_GREEN_LED_PIN = 7;
   static constexpr uint8_t DEFAULT_BLUE_LED_PIN = 8;

private:
   RGBLEDStatus _rgbStatus;

public:
   ///
   /// <summary>
   /// 3-pin RGB status LED. Driven through the startup/connectivity status sequence by
   /// default, and also available for direct control by the sketch.
   /// </summary>
   ///
   RGBLED statusLed;

   ///
   /// <summary>
   /// General-purpose LED on the pin returned by ledPin(). Available for sketches that
   /// want a managed LED (begin()/turnOn()/turnOff()/blink()) rather than driving the
   /// pin directly.
   /// </summary>
   ///
   LED led;

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
      : WaveShare_ESP32_S3_Zero_SensorsBase(
           sdaPin,
           sclPin,
           i2cAuxGroundPin,
           i2cAuxPowerPin,
           ledPin),
        _rgbStatus(&statusLed),
        statusLed(redPin, greenPin, bluePin),
        led(ledPin)
   {
      _status.addStatus(&_rgbStatus);
   }

   ///
   /// <summary>
   /// Powers the enclosure temperature sensor, sets the custom I2C pins, initializes
   /// the base board, and starts the combined status indicator and general-purpose LED.
   /// </summary>
   ///
   void begin() override
   {
      WaveShare_ESP32_S3_Zero_SensorsBase::begin();
      _status.begin();
      led.begin();
   }
};

///
/// <summary>
/// WaveShare ESP32-S3-Zero board wrapper (revision 2) wired with a custom I2C bus and a
/// 2-LED WS2812 NeoPixel strip on DEFAULT_LED_PIN (GPIO 6), where pixel 0 is the external
/// status indicator and pixel 1 is the general-purpose LED.
/// </summary>
///
class WaveShare_ESP32_S3_Zero_Sensors2 : public WaveShare_ESP32_S3_Zero_SensorsBase
{
public:
   static constexpr uint16_t NUM_STRIP_LEDS = 2;
   static constexpr uint16_t STATUS_PIXEL_INDEX = 0;
   static constexpr uint16_t LED_PIXEL_INDEX = 1;
   static constexpr neoPixelType DEFAULT_STRIP_TYPE = NEO_GRB + NEO_KHZ800;

private:
   NeoPixelStrip _strip;
   NeoPixelStatus _stripStatus;

public:
   ///
   /// <summary>
   /// Status addressable LED on pixel 0 of the 2-LED NeoPixel strip. Driven through the
   /// startup/connectivity status sequence by default, and also available for direct
   /// control by the sketch.
   /// </summary>
   ///
   NeoPixelLED statusLed;

   ///
   /// <summary>
   /// General-purpose addressable LED on pixel 1 of the 2-LED NeoPixel strip. Supports
   /// both the standard LED interface (turnOn()/turnOff()/blink()/flash()/setLevel())
   /// and RGB color selection via setColor().
   /// </summary>
   ///
   NeoPixelLED led;

   ///
   /// <summary>
   /// Initializes a new instance of the WaveShare_ESP32_S3_Zero_Sensors2 class.
   /// </summary>
   /// <param name="sdaPin">I2C data pin</param>
   /// <param name="sclPin">I2C clock pin</param>
   /// <param name="i2cAuxGroundPin">Pin held LOW to power the enclosure temperature sensor</param>
   /// <param name="i2cAuxPowerPin">Pin held HIGH to power the enclosure temperature sensor</param>
   /// <param name="ledPin">GPIO data pin for the 2-LED NeoPixel strip</param>
   /// <param name="stripType">NeoPixel color order and frequency flags for the 2-LED strip</param>
   ///
   WaveShare_ESP32_S3_Zero_Sensors2(
      uint8_t sdaPin = DEFAULT_SDA_PIN,
      uint8_t sclPin = DEFAULT_SCL_PIN,
      uint8_t i2cAuxGroundPin = DEFAULT_I2C_AUX_GROUND_PIN,
      uint8_t i2cAuxPowerPin = DEFAULT_I2C_AUX_POWER_PIN,
      uint8_t ledPin = DEFAULT_LED_PIN,
      neoPixelType stripType = DEFAULT_STRIP_TYPE)
      : WaveShare_ESP32_S3_Zero_SensorsBase(
           sdaPin,
           sclPin,
           i2cAuxGroundPin,
           i2cAuxPowerPin,
           ledPin),
        _strip(NUM_STRIP_LEDS, ledPin, stripType),
        _stripStatus(&statusLed),
        statusLed(&_strip, STATUS_PIXEL_INDEX),
        led(&_strip, LED_PIXEL_INDEX)
   {
      _status.addStatus(&_stripStatus);
   }

   ///
   /// <summary>
   /// Powers the enclosure temperature sensor, sets the custom I2C pins, initializes
   /// the base board and 2-LED NeoPixel strip, and starts the status and general-purpose
   /// LEDs.
   /// </summary>
   ///
   void begin() override
   {
      WaveShare_ESP32_S3_Zero_SensorsBase::begin();
      _strip.begin();
      statusLed.begin();
      _status.begin();
      led.begin();
   }
};
