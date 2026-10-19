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
/// Battery charging/health state indicator supporting both hardware fuel gauges and
/// ADC voltage-trend, full-charge, and over-voltage (bad charger) detection.
/// </summary>
///
enum class BatteryChargingState : uint8_t
{
   Unknown = 0,
   No = 1,
   Yes = 2,
   Full = 3,
   BadCharger = 4
};

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

   // When no battery is attached on a TP4054-based ADC board (e.g. Hosyond 4.0) while
   // powered over USB, the TP4054 repeatedly charges the 10uF output capacitor to ~4.16V,
   // terminates, and restarts at ~4.08V as the 200k divider drains the capacitor, creating
   // a continuous ~65-80mV sawtooth oscillation near 4.1V across every window.
   // Unplugging or plugging in USB with a real battery attached causes a single one-way
   // voltage step (~50-150mV) in one window, followed by stable readings in subsequent
   // windows; requiring 2 consecutive high-ripple windows prevents a USB plug/unplug
   // transition from ever triggering a false "No Battery" state.
   static constexpr uint32_t RIPPLE_WINDOW_MS = 1000;
   static constexpr uint8_t MIN_RIPPLE_SAMPLES_FOR_VALID = 5;
   static constexpr float RIPPLE_DISCONNECT_VOLTS = 0.055f;
   static constexpr float RIPPLE_CONNECT_VOLTS = 0.035f;
   static constexpr float MIN_RIPPLE_FLOAT_VOLTS = 3.95f;
   static constexpr uint8_t RIPPLE_DISCONNECT_WINDOWS_REQUIRED = 2;
   static constexpr uint8_t UNPLUGGED_PROBE_SAMPLE_COUNT = 25;
   static constexpr uint8_t UNPLUGGED_PROBE_DELAY_MS = 10;

   // Pseudo charging detection on ADC voltage-divider boards:
   // - Compares window average voltage over time: rising by >= 4mV indicates Charging (Yes),
   //   falling by >= 4mV indicates Draining (No).
   // - Once a connected battery reaches the TP4054 full-charge CV plateau (>= 4.18V) and
   //   stops rising, it is considered fully charged (Charging = No).
   static constexpr float TREND_DEADBAND_VOLTS = 0.004f;
   static constexpr float FULL_CHARGE_VOLTS = 4.18f;

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
   /// Checks whether a measured voltage exceeds the maximum safe 1S LiPo ceiling,
   /// indicating a faulty charger or power-path circuit leaking ~5V onto the battery rail.
   /// </summary>
   /// <param name="volts">Measured voltage in volts.</param>
   /// <returns>True if the voltage exceeds MAX_CONNECTED_VOLTS.</returns>
   ///
   static bool _isVoltsBadCharger(float volts)
   {
      return isfinite(volts) && volts > MAX_CONNECTED_VOLTS;
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
   uint32_t _windowStartMs = 0;
   float _windowMinVolts = NAN;
   float _windowMaxVolts = NAN;
   double _windowSumVolts = 0.0;
   uint16_t _windowSampleCount = 0;
   float _lastRippleVolts = 0.0f;
   bool _hasRippleMeasurement = false;
   bool _rippleDisconnected = false;
   uint8_t _consecutiveRippleWindows = 0;
   float _trendBaselineVolts = NAN;
   BatteryChargingState _adcChargingState = BatteryChargingState::Unknown;

#if defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S3_TFT)
   Adafruit_MAX17048 _fuelGauge;
#endif

   ///
   /// <summary>
   /// Resets the voltage-trend charging state when the battery is disconnected.
   /// </summary>
   ///
   void _resetAdcChargingTrend()
   {
      _trendBaselineVolts = NAN;
      _adcChargingState = BatteryChargingState::Unknown;
   }

   ///
   /// <summary>
   /// Updates the pseudo charging state from the completed window's average voltage.
   /// </summary>
   /// <param name="windowAvgVolts">Average voltage across the completed window.</param>
   ///
   void _updateAdcChargingTrend(float windowAvgVolts)
   {
      if (_rippleDisconnected || !_isVoltsConnected(windowAvgVolts))
      {
         _resetAdcChargingTrend();
         return;
      }

      if (!isfinite(_trendBaselineVolts))
      {
         _trendBaselineVolts = windowAvgVolts;
         if (windowAvgVolts >= FULL_CHARGE_VOLTS)
         {
            _adcChargingState = BatteryChargingState::Full;
         }
         return;
      }

      float delta = windowAvgVolts - _trendBaselineVolts;
      if (delta >= TREND_DEADBAND_VOLTS)
      {
         // Voltage is measurably rising -> actively charging (unless saturated at full charge)
         _adcChargingState = (windowAvgVolts >= FULL_CHARGE_VOLTS)
            ? BatteryChargingState::Full
            : BatteryChargingState::Yes;
         _trendBaselineVolts = windowAvgVolts;
      }
      else if (delta <= -TREND_DEADBAND_VOLTS)
      {
         // Voltage is measurably falling -> draining / not charging
         _adcChargingState = BatteryChargingState::No;
         _trendBaselineVolts = windowAvgVolts;
      }
      else if (windowAvgVolts >= FULL_CHARGE_VOLTS)
      {
         // Holding steady on the 4.18V-4.20V CV plateau -> fully charged (charging complete)
         _adcChargingState = BatteryChargingState::Full;
         _trendBaselineVolts = windowAvgVolts;
      }
   }

   ///
   /// <summary>
   /// Reads a single multisampled ADC battery voltage without updating ripple state.
   /// </summary>
   /// <returns>Sampled voltage in volts.</returns>
   ///
   float _sampleAdcVolts()
   {
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
   /// Records an ADC voltage sample into the rolling ripple window used to detect the
   /// TP4054 no-battery sawtooth oscillation (~4.08V-4.16V) and voltage trend.
   /// </summary>
   /// <param name="volts">Measured battery voltage in volts.</param>
   ///
   void _recordAdcSample(float volts)
   {
      if (!isfinite(volts))
      {
         return;
      }

      uint32_t now = millis();
      if (_windowSampleCount == 0)
      {
         _windowStartMs = now;
         _windowMinVolts = volts;
         _windowMaxVolts = volts;
         _windowSumVolts = volts;
         _windowSampleCount = 1;
         return;
      }

      if (now - _windowStartMs >= RIPPLE_WINDOW_MS)
      {
         if (_windowSampleCount >= MIN_RIPPLE_SAMPLES_FOR_VALID)
         {
            _lastRippleVolts = _windowMaxVolts - _windowMinVolts;
            _hasRippleMeasurement = true;
            float threshold = _rippleDisconnected ? RIPPLE_CONNECT_VOLTS : RIPPLE_DISCONNECT_VOLTS;
            bool windowHasRipple = (_windowMinVolts >= MIN_RIPPLE_FLOAT_VOLTS) && (_lastRippleVolts > threshold);

            if (windowHasRipple)
            {
               if (_consecutiveRippleWindows < RIPPLE_DISCONNECT_WINDOWS_REQUIRED)
               {
                  _consecutiveRippleWindows++;
               }
               if (_consecutiveRippleWindows >= RIPPLE_DISCONNECT_WINDOWS_REQUIRED)
               {
                  _rippleDisconnected = true;
               }
            }
            else
            {
               _consecutiveRippleWindows = 0;
               _rippleDisconnected = false;
            }

            float windowAvgVolts = static_cast<float>(_windowSumVolts / _windowSampleCount);
            _updateAdcChargingTrend(windowAvgVolts);
         }

         _windowStartMs = now;
         _windowMinVolts = volts;
         _windowMaxVolts = volts;
         _windowSumVolts = volts;
         _windowSampleCount = 1;
         return;
      }

      if (volts < _windowMinVolts)
      {
         _windowMinVolts = volts;
      }
      if (volts > _windowMaxVolts)
      {
         _windowMaxVolts = volts;
      }
      _windowSumVolts += volts;
      _windowSampleCount++;
   }

   ///
   /// <summary>
   /// Checks whether the ADC battery rail is exhibiting the TP4054 no-battery sawtooth
   /// ripple (> 55mV peak-to-peak across consecutive windows).
   /// </summary>
   /// <returns>True if no-battery charger sawtooth ripple is present; false if stable.</returns>
   ///
   bool _hasNoBatteryRipple()
   {
      uint32_t now = millis();
      if (_hasRippleMeasurement && (now - _windowStartMs < RIPPLE_WINDOW_MS * 2))
      {
         return _rippleDisconnected;
      }

      float minVolts = _sampleAdcVolts();
      float maxVolts = minVolts;
      _recordAdcSample(minVolts);

      for (uint8_t i = 1; i < UNPLUGGED_PROBE_SAMPLE_COUNT; i++)
      {
         delay(UNPLUGGED_PROBE_DELAY_MS);
         float v = _sampleAdcVolts();
         _recordAdcSample(v);

         if (v < minVolts)
         {
            minVolts = v;
         }
         if (v > maxVolts)
         {
            maxVolts = v;
         }
      }

      _lastRippleVolts = maxVolts - minVolts;
      _hasRippleMeasurement = true;
      bool probeHasRipple = (minVolts >= MIN_RIPPLE_FLOAT_VOLTS) && (_lastRippleVolts > RIPPLE_DISCONNECT_VOLTS);
      _consecutiveRippleWindows = probeHasRipple ? RIPPLE_DISCONNECT_WINDOWS_REQUIRED : 0;
      _rippleDisconnected = probeHasRipple;
      return _rippleDisconnected;
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

      float volts = _sampleAdcVolts();
      _recordAdcSample(volts);
      return volts;
   }

   ///
   /// <summary>
   /// Checks whether a battery is currently connected (and switched on) by verifying that
   /// the measured battery rail voltage is within the valid 1S LiPo operating window
   /// (2.5V to 4.5V) and is not exhibiting the TP4054 no-battery sawtooth ripple (~4.08V-4.16V).
   /// </summary>
   /// <returns>True if a battery is detected; false if disconnected or unavailable.</returns>
   ///
   bool isConnected()
   {
      float volts = readVolts();
      if (!_isVoltsConnected(volts))
      {
         _resetAdcChargingTrend();
         return false;
      }

#if defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S3_TFT)
      if (_pin < 0)
      {
         return true;
      }
#endif

      if (_hasNoBatteryRipple())
      {
         _resetAdcChargingTrend();
         return false;
      }

      return true;
   }

   ///
   /// <summary>
   /// Checks whether the battery rail voltage exceeds the maximum safe 1S LiPo ceiling
   /// (> 4.5V), indicating a faulty charger or power-path circuit.
   /// </summary>
   /// <returns>True if over-voltage is detected on the battery rail; otherwise false.</returns>
   ///
   bool hasBadCharger()
   {
      return _isVoltsBadCharger(readVolts());
   }

   ///
   /// <summary>
   /// Returns the charging state indicator (Yes, No, Unknown, or BadCharger). On fuel-gauge
   /// boards (MAX17048), uses the hardware charge rate; on ADC voltage-divider boards,
   /// uses voltage trend (rising vs. falling), full-charge plateau detection (>= 4.18V),
   /// and over-voltage detection (> 4.5V).
   /// </summary>
   /// <returns>BatteryChargingState::Yes, No, Unknown, or BadCharger.</returns>
   ///
   BatteryChargingState readChargingState()
   {
      float volts = readVolts();
      if (_isVoltsBadCharger(volts))
      {
         _resetAdcChargingTrend();
         return BatteryChargingState::BadCharger;
      }

      if (!isConnected())
      {
         return BatteryChargingState::Unknown;
      }

#if defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S3_TFT)
      if (_pin < 0)
      {
         float rate = readChargeRate();
         if (!isfinite(rate))
         {
            return BatteryChargingState::Unknown;
         }

         if (rate >= MIN_FUEL_GAUGE_CHARGE_RATE)
         {
            return BatteryChargingState::Yes;
         }

         return (volts >= FULL_CHARGE_VOLTS)
            ? BatteryChargingState::Full
            : BatteryChargingState::No;
      }
#endif

      return _adcChargingState;
   }

   ///
   /// <summary>
   /// Checks whether the battery is currently charging.
   /// </summary>
   /// <returns>True if charging is detected; otherwise false.</returns>
   ///
   bool isCharging()
   {
      return readChargingState() == BatteryChargingState::Yes;
   }

   ///
   /// <summary>
   /// Reads the estimated battery state of charge on demand.
   /// </summary>
   /// <returns>Battery charge percentage (0.0 to 100.0), or NAN if disconnected or unavailable.</returns>
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

      if (_hasNoBatteryRipple())
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

   ///
   /// <summary>
   /// Returns the most recently measured peak-to-peak ADC voltage ripple (in volts) across
   /// the ripple detection window.
   /// </summary>
   /// <returns>Peak-to-peak voltage ripple in volts.</returns>
   ///
   float readRippleVolts() const
   {
      return _lastRippleVolts;
   }
};
