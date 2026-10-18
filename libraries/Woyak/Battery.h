#pragma once

#include <Arduino.h>
#include <array>
#include <cmath>

#if defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S3_TFT)
#include <Adafruit_MAX1704X.h>
#include <Wire.h>
#endif

#include "ArduinoBoardId.h"

///
/// <summary>
/// On-demand battery monitor supporting both I2C fuel-gauge boards (Adafruit Feather
/// ESP32-S3 TFT with MAX17048) and ADC voltage-divider boards (Hosyond 4.0 ESP32-S3,
/// Waveshare ESP32-S3 Touch LCD 1.9 / 4.3, Adafruit Feather M0, or a custom pin/divider).
/// </summary>
///
class Battery
{
private:
   static constexpr uint8_t NUM_LIPO_CURVE_POINTS = 11;

   // Standard 1S LiPo voltage-to-SoC lookup table from 0% to 100% in 10% increments.
   static constexpr std::array<float, NUM_LIPO_CURVE_POINTS> LIPO_VOLTS_CURVE =
   {
      3.30f, //   0%
      3.58f, //  10%
      3.68f, //  20%
      3.74f, //  30%
      3.77f, //  40%
      3.80f, //  50%
      3.84f, //  60%
      3.91f, //  70%
      3.98f, //  80%
      4.08f, //  90%
      4.20f  // 100%
   };

   static constexpr uint8_t ADC_RESOLUTION_BITS = 12;
   static constexpr uint16_t ADC_MAX_COUNTS = (1 << ADC_RESOLUTION_BITS);
   static constexpr float ADC_REF_VOLTS = 3.3f;

   ///
   /// <summary>
   /// Converts a 1S LiPo cell voltage to an estimated state-of-charge percentage using
   /// piecewise linear interpolation over LIPO_VOLTS_CURVE.
   /// </summary>
   /// <param name="volts">Measured cell voltage in volts.</param>
   /// <returns>Estimated battery percentage (0.0 to 100.0).</returns>
   ///
   static float _voltsToPercent(float volts)
   {
      if (volts <= LIPO_VOLTS_CURVE[0])
      {
         return 0.0f;
      }

      if (volts >= LIPO_VOLTS_CURVE[NUM_LIPO_CURVE_POINTS - 1])
      {
         return 100.0f;
      }

      for (uint8_t i = 0; i < NUM_LIPO_CURVE_POINTS - 1; i++)
      {
         float vLow = LIPO_VOLTS_CURVE[i];
         float vHigh = LIPO_VOLTS_CURVE[i + 1];
         if (volts <= vHigh)
         {
            float fraction = (volts - vLow) / (vHigh - vLow);
            return (i + fraction) * 10.0f;
         }
      }

      return 100.0f;
   }

public:
#if defined(ARDUINO_HOSYOND_ESP32S3_TOUCH_LCD_40) || defined(ARDUINO_HOSYOND_ESP32_S3_VIEWER)
   static constexpr int8_t DEFAULT_PIN = 9;
   static constexpr float DEFAULT_VOLTAGE_DIVIDER = 2.0f;
#elif defined(ARDUINO_WAVESHARE_ESP32S3_TOUCH_LCD_43)
   static constexpr int8_t DEFAULT_PIN = 6;
   static constexpr float DEFAULT_VOLTAGE_DIVIDER = 3.0f;
#elif defined(ARDUINO_WAVESHARE_ESP32S3_TOUCH_LCD_19)
   static constexpr int8_t DEFAULT_PIN = 4;
   static constexpr float DEFAULT_VOLTAGE_DIVIDER = 3.0f;
#elif defined(ADAFRUIT_FEATHER_M0)
   static constexpr int8_t DEFAULT_PIN = 9;
   static constexpr float DEFAULT_VOLTAGE_DIVIDER = 2.0f;
#else
   static constexpr int8_t DEFAULT_PIN = -1;
   static constexpr float DEFAULT_VOLTAGE_DIVIDER = 2.0f;
#endif

private:
   int8_t _pin = DEFAULT_PIN;
   float _voltageDivider = DEFAULT_VOLTAGE_DIVIDER;
   bool _ready = false;

#if defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S3_TFT)
   Adafruit_MAX17048 _fuelGauge;
#endif

public:
   ///
   /// <summary>
   /// Initializes a new instance of the Battery class using the board's default battery
   /// monitoring configuration, or a custom ADC pin and voltage divider ratio.
   /// </summary>
   /// <param name="pin">Analog input pin wired to the battery voltage divider (or -1 if unused).</param>
   /// <param name="voltageDivider">Voltage divider multiplier (e.g. 2.0 for 100k/100k, 3.0 for 200k/100k).</param>
   ///
   Battery(int8_t pin = DEFAULT_PIN, float voltageDivider = DEFAULT_VOLTAGE_DIVIDER)
   {
      _pin = pin;
      _voltageDivider = voltageDivider;
   }

   ///
   /// <summary>
   /// Initializes the battery monitor hardware (I2C MAX17048 fuel gauge on Feather ESP32-S3,
   /// or 12-bit ADC resolution on voltage-divider boards).
   /// </summary>
   /// <returns>True if the battery monitor is ready; false if initialization failed.</returns>
   ///
   bool begin()
   {
#if defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S3_TFT)
      if (_pin < 0)
      {
         _ready = _fuelGauge.begin();
         return _ready;
      }
#endif

      if (_pin < 0)
      {
         _ready = false;
         return false;
      }

      analogReadResolution(ADC_RESOLUTION_BITS);
      pinMode(_pin, INPUT);
      _ready = true;
      return true;
   }

   ///
   /// <summary>
   /// Reads the current battery voltage on demand.
   /// </summary>
   /// <returns>Battery voltage in volts, or NAN if unavailable.</returns>
   ///
   float readVolts()
   {
      if (!_ready && !begin())
      {
         return NAN;
      }

#if defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S3_TFT)
      if (_pin < 0)
      {
         return _fuelGauge.cellVoltage();
      }
#endif

#if defined(ARDUINO_ARCH_ESP32)
      return analogReadMilliVolts(_pin) * 0.001f * _voltageDivider;
#else
      return analogRead(_pin) * _voltageDivider * ADC_REF_VOLTS / ADC_MAX_COUNTS;
#endif
   }

   ///
   /// <summary>
   /// Reads the estimated battery state of charge on demand.
   /// </summary>
   /// <returns>Battery charge percentage (0.0 to 100.0), or NAN if unavailable.</returns>
   ///
   float readPercent()
   {
      if (!_ready && !begin())
      {
         return NAN;
      }

#if defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S3_TFT)
      if (_pin < 0)
      {
         float percent = _fuelGauge.cellPercent();
         if (!isfinite(percent))
         {
            return NAN;
         }

         return constrain(percent, 0.0f, 100.0f);
      }
#endif

      float volts = readVolts();
      if (!isfinite(volts))
      {
         return NAN;
      }

      return _voltsToPercent(volts);
   }

   ///
   /// <summary>
   /// Reads the battery charge/discharge rate in percent per hour on boards with a hardware
   /// fuel gauge (MAX17048). Returns NAN on ADC voltage-divider boards.
   /// </summary>
   /// <returns>Charge rate in %/hr (positive when charging, negative when draining), or NAN if unsupported.</returns>
   ///
   float readChargeRate()
   {
#if defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S3_TFT)
      if (_pin < 0)
      {
         if (!_ready && !begin())
         {
            return NAN;
         }

         return _fuelGauge.chargeRate();
      }
#endif

      return NAN;
   }
};
