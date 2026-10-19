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
/// Waveshare ESP32-S3 Touch LCD 1.9 / 2.8 / 4.3, Adafruit Feather M0, or a custom pin/divider).
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
   static constexpr uint8_t ADC_SAMPLE_COUNT = 8;

   // Valid 1S LiPo operating window used by isConnected() to distinguish a connected
   // cell (~3.0V-4.2V) from an unattached/switched-off battery rail that either drops
   // near 0V or floats up to the ~5V USB rail.
   static constexpr float MIN_CONNECTED_VOLTS = 2.5f;
   static constexpr float MAX_CONNECTED_VOLTS = 4.5f;

   // On ADC voltage-divider boards, active charging pushes the sensed rail up toward
   // the charger's ~4.16V-4.20V regulation ceiling, whereas a battery powering the
   // board + display on its own sags below ~4.10V even when 100% full. Hysteresis
   // prevents noise near the threshold from toggling between Charging and ~97%.
   static constexpr float MIN_ADC_CHARGING_VOLTS = 4.14f;
   static constexpr float MIN_ADC_CHARGING_HOLD_VOLTS = 4.11f;
   static constexpr float MIN_FUEL_GAUGE_CHARGE_RATE = 1.0f;

   ///
   /// <summary>
   /// Checks whether a measured voltage falls within the valid 1S LiPo operating range.
   /// </summary>
   /// <param name="volts">Measured voltage in volts.</param>
   /// <returns>True if the voltage indicates a connected 1S LiPo battery.</returns>
   ///
   static bool _isVoltsConnected(float volts)
   {
      return isfinite(volts) && volts >= MIN_CONNECTED_VOLTS && volts <= MAX_CONNECTED_VOLTS;
   }

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
#elif defined(ARDUINO_WAVESHARE_ESP32S3_TOUCH_LCD_28) || defined(ARDUINO_WAVESHARE_ESP32_S3_TOUCH_LCD_28)
   static constexpr int8_t DEFAULT_PIN = 8;
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
   bool _adcCharging = false;

#if defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S3_TFT)
   Adafruit_MAX17048 _fuelGauge;
#endif

   ///
   /// <summary>
   /// Updates and returns the latched ADC charging state with hysteresis for a given
   /// measured battery voltage.
   /// </summary>
   /// <param name="volts">Measured battery voltage in volts.</param>
   /// <returns>True if the ADC voltage indicates the battery is charging.</returns>
   ///
   bool _updateAdcCharging(float volts)
   {
      if (!_isVoltsConnected(volts))
      {
         _adcCharging = false;
         return false;
      }

      float threshold = _adcCharging ? MIN_ADC_CHARGING_HOLD_VOLTS : MIN_ADC_CHARGING_VOLTS;
      _adcCharging = (volts >= threshold);
      return _adcCharging;
   }

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
      uint32_t totalMilliVolts = 0;
      for (uint8_t i = 0; i < ADC_SAMPLE_COUNT; i++)
      {
         totalMilliVolts += analogReadMilliVolts(_pin);
      }

      return (totalMilliVolts / static_cast<float>(ADC_SAMPLE_COUNT)) * 0.001f * _voltageDivider;
#else
      uint32_t totalCounts = 0;
      for (uint8_t i = 0; i < ADC_SAMPLE_COUNT; i++)
      {
         totalCounts += analogRead(_pin);
      }

      return (totalCounts / static_cast<float>(ADC_SAMPLE_COUNT)) * _voltageDivider * ADC_REF_VOLTS / ADC_MAX_COUNTS;
#endif
   }

   ///
   /// <summary>
   /// Checks whether a battery is currently connected (and switched on) by verifying that
   /// the measured battery rail voltage is within the valid 1S LiPo operating window
   /// (2.5V to 4.5V). When no battery is attached while powered over USB, boards like the
   /// Hosyond 4.0 float the sensed rail up to ~5.17V.
   /// </summary>
   /// <returns>True if a battery is detected; false if disconnected or unavailable.</returns>
   ///
   bool isConnected()
   {
      return _isVoltsConnected(readVolts());
   }

   ///
   /// <summary>
   /// Checks whether the battery is currently charging. On fuel-gauge boards (MAX17048),
   /// uses the positive charge rate; on ADC voltage-divider boards, detects when the
   /// charger holds the battery rail at or near its ~4.16V-4.20V regulation ceiling.
   /// </summary>
   /// <returns>True if the battery is connected and charging; otherwise false.</returns>
   ///
   bool isCharging()
   {
      float volts = readVolts();
      if (!_isVoltsConnected(volts))
      {
         _adcCharging = false;
         return false;
      }

#if defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S3_TFT)
      if (_pin < 0)
      {
         float rate = readChargeRate();
         return isfinite(rate) && rate >= MIN_FUEL_GAUGE_CHARGE_RATE;
      }
#endif

      return _updateAdcCharging(volts);
   }

   ///
   /// <summary>
   /// Reads the estimated battery state of charge on demand. On ADC voltage-divider
   /// boards, returns NAN while actively charging because the charger holds the sensed
   /// rail at ~4.20V regardless of the underlying cell state of charge.
   /// </summary>
   /// <returns>Battery charge percentage (0.0 to 100.0), or NAN if disconnected, charging on an ADC board, or unavailable.</returns>
   ///
   float readPercent()
   {
      if (!_ready && !begin())
      {
         return NAN;
      }

      float volts = readVolts();
      if (!_isVoltsConnected(volts))
      {
         _adcCharging = false;
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

      if (_updateAdcCharging(volts))
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
