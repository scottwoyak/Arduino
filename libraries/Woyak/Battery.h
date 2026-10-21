#pragma once

#include <Arduino.h>
#include <array>
#include <cmath>

#include <Adafruit_INA219.h>
#include <Wire.h>

#if defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S3_TFT)
#include <Adafruit_MAX1704X.h>
#endif

#if defined(ARDUINO_ARCH_ESP32)
#include <Preferences.h>
#endif

#include "ArduinoBoardId.h"
#include "RollingAverage.h"

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
/// Waveshare ESP32-S3 Touch LCD 1.9 / 2.0 / 2.8 / 4.3, Adafruit Feather M0, or a custom pin/divider).
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

   // When no battery is attached on a charger-backed battery rail (e.g. TP4054 on Hosyond 4.0
   // or MCP73831 + MAX17048 on Feather ESP32-S3 TFT) while powered over USB, the charger IC
   // repeatedly charges the 10uF output capacitor to ~4.16V-4.20V, terminates, and restarts at
   // ~4.08V-4.10V as the divider or fuel gauge drains the capacitor, creating a continuous
   // sawtooth oscillation near 4.1V-4.2V across every window.
   // On the MAX17048 fuel gauge, its internal 125ms delta-sigma integration and 250ms register
   // update interval reduce steady-state connected noise to <= 3mV while partially attenuating
   // the no-battery sawtooth to ~15-95mV.
   // Unplugging or plugging in USB with a real battery attached causes a single one-way
   // voltage step (~50-150mV) in one window, followed by stable readings in subsequent
   // windows; requiring 2 consecutive high-ripple windows prevents a USB plug/unplug
   // transition from ever triggering a false "No Battery" state.
   static constexpr uint32_t RIPPLE_WINDOW_MS = 1000;
   static constexpr uint32_t SAMPLE_PERIOD_MS = 100;
   static constexpr uint8_t MIN_RIPPLE_SAMPLES_FOR_VALID = 5;
   static constexpr float ADC_RIPPLE_DISCONNECT_VOLTS = 0.055f;
   static constexpr float ADC_RIPPLE_CONNECT_VOLTS = 0.035f;
   static constexpr float FUEL_GAUGE_RIPPLE_DISCONNECT_VOLTS = 0.024f;
   static constexpr float FUEL_GAUGE_RIPPLE_CONNECT_VOLTS = 0.020f;
   static constexpr float MIN_RIPPLE_FLOAT_VOLTS = 3.95f;
   static constexpr float ADC_MIN_RIPPLE_PEAK_VOLTS = 4.14f;
   static constexpr float FUEL_GAUGE_MIN_RIPPLE_PEAK_VOLTS = 4.175f;
   static constexpr uint8_t RIPPLE_DISCONNECT_WINDOWS_REQUIRED = 2;
   static constexpr uint8_t RIPPLE_CONNECT_WINDOWS_REQUIRED = 3;
   static constexpr uint8_t ADC_UNPLUGGED_PROBE_SAMPLE_COUNT = 25;
   static constexpr uint8_t ADC_UNPLUGGED_PROBE_DELAY_MS = 10;
   static constexpr uint8_t FUEL_GAUGE_UNPLUGGED_PROBE_SAMPLE_COUNT = 35;
   static constexpr uint8_t FUEL_GAUGE_UNPLUGGED_PROBE_DELAY_MS = 25;
   static constexpr uint16_t FUEL_GAUGE_STARTUP_TIMEOUT_MS = 500;

   // Pseudo charging detection and adaptive full-charge plateau learning:
   // - Compares window average voltage over time: rising above the deadband across
   //   consecutive windows indicates Charging (Yes), falling below the deadband indicates
   //   Draining (No).
   // - Different batteries and boards reach slightly different topped-off voltages due to
   //   cell age/internal resistance, load current, and ADC voltage-divider tolerances.
   //   When charging in the upper voltage region (>= 4.08V) and the voltage stops rising
   //   for PLATEAU_FULL_WINDOWS_REQUIRED (180s = 3 minutes), the battery has reached its
   //   actual CV/termination plateau: it transitions to Full and updates the learned
   //   full-charge voltage in NVS (Preferences) so both Full detection and the 0-100%
   //   voltage curve automatically adapt to that specific battery and board.
   // - On the Feather MAX17048 + MCP73831, if a nearly-full battery (>= 4.10V) is connected
   //   after the MCP73831 has terminated charge, the charger rests in standby while 5V
   //   supplies the load (holding the battery flat with <= 6mV window span), which also
   //   represents a Full/standby state until 5V is unplugged (span jumps to >= 14mV).
   static constexpr const char* PREFERENCES_NAMESPACE = "Device";
   static constexpr const char* FULL_VOLTS_PREF_KEY = "fullVolts";
   static constexpr float ADC_TREND_DEADBAND_VOLTS = 0.008f;
   static constexpr float FUEL_GAUGE_TREND_DEADBAND_VOLTS = 0.004f;
   static constexpr uint8_t TREND_RISE_WINDOWS_REQUIRED = 2;
   static constexpr float DEFAULT_FULL_CHARGE_VOLTS = 4.185f;
   static constexpr float MIN_LEARNED_FULL_VOLTS = 4.080f;
   static constexpr float MAX_LEARNED_FULL_VOLTS = 4.240f;
   static constexpr float SAVE_FULL_VOLTS_TOLERANCE = 0.003f;
   static constexpr uint16_t PLATEAU_FULL_WINDOWS_REQUIRED = 180;
   static constexpr float FUEL_GAUGE_STANDBY_FULL_VOLTS = 4.100f;
   static constexpr float FUEL_GAUGE_STANDBY_MAX_SPAN_VOLTS = 0.006f;
   static constexpr float FULL_CHARGE_PERCENT = 99.5f;

   static constexpr float MIN_FUEL_GAUGE_CHARGE_RATE = 1.0f;

   // INA219 current-based state thresholds (with VIN+ on Battery+ and VIN- on Board JST+,
   // discharging current is negative and charging current is positive after sign inversion):
   // - Current >= +5.0 mA: actively charging (Yes)
   // - Current <= -5.0 mA: actively discharging (No)
   // - |Current| < 5.0 mA and voltage >= 4.05V: charge terminated / full (Full)
   // - |Current| < 2.0 mA and voltage < 2.5V (or no current with float rail): disconnected
   static constexpr float INA219_CHARGE_CURRENT_THRESHOLD_MA = 5.0f;
   static constexpr float INA219_DISCHARGE_CURRENT_THRESHOLD_MA = -5.0f;
   static constexpr float INA219_IDLE_CURRENT_THRESHOLD_MA = 2.0f;
   static constexpr float INA219_FULL_MIN_VOLTS = 4.05f;

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
   /// piecewise linear interpolation over LIPO_VOLTS_CURVE, scaled between the 0% cutoff
   /// (3.30V) and the board/battery's calibrated full-charge ceiling.
   /// </summary>
   /// <param name="volts">Measured cell voltage in volts.</param>
   /// <param name="fullChargeVolts">Calibrated 100% full-charge voltage for this battery/board.</param>
   /// <returns>Estimated battery percentage (0.0 to 100.0).</returns>
   ///
   static float _voltsToPercent(float volts, float fullChargeVolts = LIPO_VOLTS_CURVE[NUM_LIPO_CURVE_POINTS - 1])
   {
      constexpr float minCurveVolts = LIPO_VOLTS_CURVE[0];
      constexpr float maxCurveVolts = LIPO_VOLTS_CURVE[NUM_LIPO_CURVE_POINTS - 1];

      if (volts <= minCurveVolts)
      {
         return 0.0f;
      }

      float effectiveFullVolts = (isfinite(fullChargeVolts) && fullChargeVolts >= MIN_LEARNED_FULL_VOLTS)
         ? fullChargeVolts
         : maxCurveVolts;
      if (volts >= effectiveFullVolts)
      {
         return 100.0f;
      }

      float normalizedVolts = minCurveVolts
         + (volts - minCurveVolts) * ((maxCurveVolts - minCurveVolts) / (effectiveFullVolts - minCurveVolts));

      if (normalizedVolts >= maxCurveVolts)
      {
         return 100.0f;
      }

      for (uint8_t i = 0; i < NUM_LIPO_CURVE_POINTS - 1; i++)
      {
         float vLow = LIPO_VOLTS_CURVE[i];
         float vHigh = LIPO_VOLTS_CURVE[i + 1];
         if (normalizedVolts <= vHigh)
         {
            float fraction = (normalizedVolts - vLow) / (vHigh - vLow);
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
#elif defined(ARDUINO_WAVESHARE_ESP32S3_TOUCH_LCD_20) || defined(ARDUINO_WAVESHARE_ESP32_S3_TOUCH_LCD_20)
   static constexpr int8_t DEFAULT_PIN = 5;
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
   bool _hasIna219 = false;
   bool _hasFuelGauge = false;
   uint32_t _windowStartMs = 0;
   float _windowMinVolts = NAN;
   float _windowMaxVolts = NAN;
   float _windowLastVolts = NAN;
   float _windowRiseVolts = 0.0f;
   float _windowFallVolts = 0.0f;
   double _windowSumVolts = 0.0;
   uint16_t _windowSampleCount = 0;
   float _lastWindowMinVolts = NAN;
   float _lastWindowMaxVolts = NAN;
   float _lastWindowSpanVolts = 0.0f;
   float _lastWindowRiseVolts = 0.0f;
   float _lastWindowFallVolts = 0.0f;
   float _lastRippleVolts = 0.0f;
   bool _hasRippleMeasurement = false;
   bool _rippleDisconnected = false;
   bool _seenRippleRise = false;
   bool _seenRippleFall = false;
   uint8_t _consecutiveRippleWindows = 0;
   uint8_t _consecutiveCalmWindows = 0;
   uint8_t _consecutiveRisingWindows = 0;
   uint16_t _plateauFlatWindows = 0;
   static constexpr size_t SMOOTHING_SAMPLE_COUNT = 10;

   float _fullChargeVolts = DEFAULT_FULL_CHARGE_VOLTS;
   float _trendBaselineVolts = NAN;
   BatteryChargingState _adcChargingState = BatteryChargingState::Unknown;
   RollingAverage _voltsAverage{ SMOOTHING_SAMPLE_COUNT };
   RollingAverage _percentAverage{ SMOOTHING_SAMPLE_COUNT };
   RollingAverage _currentAverage{ SMOOTHING_SAMPLE_COUNT };
   Adafruit_INA219 _ina219;

#if defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S3_TFT)
   Adafruit_MAX17048 _fuelGauge;
#endif

   void _lock() const {}
   void _unlock() const {}
   void _startTimer() {}

   ///
   /// <summary>
   /// Checks whether this instance uses the onboard I2C fuel gauge rather than an ADC pin.
   /// </summary>
   /// <returns>True when using the hardware fuel gauge; false when using an ADC pin.</returns>
   ///
   bool _isFuelGauge() const
   {
#if defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S3_TFT)
      return _pin < 0 && _hasFuelGauge;
#else
      return false;
#endif
   }

   ///
   /// <summary>
   /// Loads the persisted full-charge plateau voltage for this battery/board from NVS.
   /// </summary>
   ///
   void _loadCalibration()
   {
#if defined(ARDUINO_ARCH_ESP32)
      Preferences prefs;
      if (prefs.begin(PREFERENCES_NAMESPACE, true))
      {
         float savedFull = prefs.getFloat(FULL_VOLTS_PREF_KEY, NAN);
         prefs.end();
         if (isfinite(savedFull) && savedFull >= MIN_LEARNED_FULL_VOLTS && savedFull <= MAX_LEARNED_FULL_VOLTS)
         {
            _fullChargeVolts = savedFull;
         }
      }
#endif
   }

   ///
   /// <summary>
   /// Saves a newly observed full-charge plateau voltage to NVS if it differs meaningfully
   /// from the currently stored value.
   /// </summary>
   /// <param name="fullVolts">Measured full-charge plateau voltage in volts.</param>
   ///
   void _saveFullChargeVolts(float fullVolts)
   {
      float clamped = constrain(fullVolts, MIN_LEARNED_FULL_VOLTS, MAX_LEARNED_FULL_VOLTS);
      if (fabsf(clamped - _fullChargeVolts) < SAVE_FULL_VOLTS_TOLERANCE)
      {
         _fullChargeVolts = clamped;
         return;
      }

      _fullChargeVolts = clamped;
#if defined(ARDUINO_ARCH_ESP32)
      Preferences prefs;
      if (prefs.begin(PREFERENCES_NAMESPACE, false))
      {
         prefs.putFloat(FULL_VOLTS_PREF_KEY, _fullChargeVolts);
         prefs.end();
      }
#endif
   }

   ///
   /// <summary>
   /// Resets the voltage-trend charging state when the battery is disconnected.
   /// </summary>
   ///
   void _resetAdcChargingTrend()
   {
      _trendBaselineVolts = NAN;
      _consecutiveRisingWindows = 0;
      _plateauFlatWindows = 0;
      _adcChargingState = BatteryChargingState::Unknown;
   }

   ///
   /// <summary>
   /// Updates the pseudo charging state and adaptive full-charge ceiling from the completed
   /// window's average voltage.
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

      bool fuelGaugeStandbyFull = _isFuelGauge()
         && (windowAvgVolts >= FUEL_GAUGE_STANDBY_FULL_VOLTS)
         && (_lastWindowSpanVolts > 0.0f)
         && (_lastWindowSpanVolts <= FUEL_GAUGE_STANDBY_MAX_SPAN_VOLTS);

      if (!isfinite(_trendBaselineVolts))
      {
         _trendBaselineVolts = windowAvgVolts;
         _consecutiveRisingWindows = 0;
         _plateauFlatWindows = 0;
         if (windowAvgVolts >= _fullChargeVolts || fuelGaugeStandbyFull || (_isFuelGauge() && _percentAverage.average() >= FULL_CHARGE_PERCENT))
         {
            _adcChargingState = BatteryChargingState::Full;
         }
         return;
      }

      float deadband = _isFuelGauge() ? FUEL_GAUGE_TREND_DEADBAND_VOLTS : ADC_TREND_DEADBAND_VOLTS;
      float delta = windowAvgVolts - _trendBaselineVolts;
      if (delta >= deadband)
      {
         if (_consecutiveRisingWindows < TREND_RISE_WINDOWS_REQUIRED)
         {
            _consecutiveRisingWindows++;
         }

         // Require 2 consecutive rising windows before switching from Discharging to Charging
         // so a single 1-second WiFi TX voltage sag and rebound does not flip the state.
         if (_adcChargingState != BatteryChargingState::No || _consecutiveRisingWindows >= TREND_RISE_WINDOWS_REQUIRED)
         {
            _plateauFlatWindows = 0;
            if (windowAvgVolts > _fullChargeVolts && windowAvgVolts <= MAX_LEARNED_FULL_VOLTS)
            {
               // Battery climbed higher than the previous learned ceiling -> raise ceiling.
               _saveFullChargeVolts(windowAvgVolts);
            }

            _adcChargingState = (windowAvgVolts >= DEFAULT_FULL_CHARGE_VOLTS)
               ? BatteryChargingState::Full
               : BatteryChargingState::Yes;
            _trendBaselineVolts = windowAvgVolts;
         }
      }
      else if (delta <= -deadband)
      {
         // Voltage is measurably falling -> draining / not charging
         _consecutiveRisingWindows = 0;
         _plateauFlatWindows = 0;
         _adcChargingState = BatteryChargingState::No;
         _trendBaselineVolts = windowAvgVolts;
      }
      else
      {
         _consecutiveRisingWindows = 0;

         // If the battery is charging (or sitting on the low-ripple 5V rail) in the upper
         // voltage region (>= 4.08V) and its voltage has stopped rising for 3 minutes, it
         // has reached this battery/board's actual full-charge plateau.
         if ((_adcChargingState == BatteryChargingState::Yes || fuelGaugeStandbyFull) && windowAvgVolts >= MIN_LEARNED_FULL_VOLTS)
         {
            if (_plateauFlatWindows < PLATEAU_FULL_WINDOWS_REQUIRED)
            {
               _plateauFlatWindows++;
            }
            if (_plateauFlatWindows >= PLATEAU_FULL_WINDOWS_REQUIRED)
            {
               _saveFullChargeVolts(windowAvgVolts);
               _adcChargingState = BatteryChargingState::Full;
               _trendBaselineVolts = windowAvgVolts;
               return;
            }
         }

         if (windowAvgVolts >= _fullChargeVolts || fuelGaugeStandbyFull || (_isFuelGauge() && _percentAverage.average() >= FULL_CHARGE_PERCENT && _adcChargingState != BatteryChargingState::No))
         {
            // Holding steady at or above the calibrated full-charge plateau (or on the
            // MCP73831 post-termination standby plateau while 5V powers the load) -> Full.
            _adcChargingState = BatteryChargingState::Full;
            _trendBaselineVolts = windowAvgVolts;
         }
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
   /// Reads the battery-side terminal voltage (VIN+) and signed battery current (mA, where
   /// positive is charging into the battery and negative is discharging out of the battery)
   /// from the INA219 sensor.
   /// </summary>
   /// <returns>Sampled battery voltage in volts.</returns>
   ///
   float _sampleIna219Volts()
   {
      float busVolts = _ina219.getBusVoltage_V();
      float shuntMilliVolts = _ina219.getShuntVoltage_mV();
      // With VIN+ wired to Battery+ and VIN- wired to Board JST+:
      // - V(VIN+) = V(VIN-) + Vshunt = busVolts + (shuntMilliVolts * 0.001f)
      // - INA219 getCurrent_mA() is positive when current flows from Battery+ to Board
      //   (discharging), so negate it so positive = charging and negative = discharging.
      float rawCurrentMa = _ina219.getCurrent_mA();
      if (isfinite(rawCurrentMa))
      {
         _currentAverage.set(-rawCurrentMa);
      }
      return busVolts + (shuntMilliVolts * 0.001f);
   }

   ///
   /// <summary>
   /// Reads a single raw battery voltage sample from the active hardware source (INA219,
   /// I2C fuel gauge, or multisampled ADC pin) without updating ripple state.
   /// </summary>
   /// <returns>Sampled voltage in volts.</returns>
   ///
   float _sampleVolts()
   {
      if (_hasIna219)
      {
         return _sampleIna219Volts();
      }

#if defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S3_TFT)
      if (_pin < 0)
      {
         if (_fuelGauge.isHibernating())
         {
            _fuelGauge.wake();
         }
         return _fuelGauge.cellVoltage();
      }
#endif

      return _sampleAdcVolts();
   }

   ///
   /// <summary>
   /// Records a voltage sample into the rolling ripple window used to detect the charger
   /// no-battery sawtooth oscillation (~4.08V-4.20V) and voltage trend.
   /// </summary>
   /// <param name="volts">Measured battery voltage in volts.</param>
   ///
   void _recordAdcSample(float volts)
   {
      if (!isfinite(volts))
      {
         return;
      }

      _voltsAverage.set(volts);

      uint32_t now = millis();
      if (_windowSampleCount == 0)
      {
         _windowStartMs = now;
         _windowMinVolts = volts;
         _windowMaxVolts = volts;
         _windowLastVolts = volts;
         _windowRiseVolts = 0.0f;
         _windowFallVolts = 0.0f;
         _windowSumVolts = volts;
         _windowSampleCount = 1;
         return;
      }

      if (now - _windowStartMs >= RIPPLE_WINDOW_MS)
      {
         if (_windowSampleCount >= MIN_RIPPLE_SAMPLES_FOR_VALID)
         {
            bool wasDisconnected = _rippleDisconnected;
            float disconnectThreshold = _isFuelGauge() ? FUEL_GAUGE_RIPPLE_DISCONNECT_VOLTS : ADC_RIPPLE_DISCONNECT_VOLTS;
            float connectThreshold = _isFuelGauge() ? FUEL_GAUGE_RIPPLE_CONNECT_VOLTS : ADC_RIPPLE_CONNECT_VOLTS;
            float threshold = _rippleDisconnected ? connectThreshold : disconnectThreshold;

            // A charger no-battery sawtooth oscillates between ~4.08V and the ~4.16V-4.20V
            // regulation ceiling, exhibiting both upward recharge ramps and downward drain.
            // A real battery when USB is unplugged drops monotonically under load without
            // recharging back up to the charger ceiling.
            float minPeakVolts = _isFuelGauge() ? FUEL_GAUGE_MIN_RIPPLE_PEAK_VOLTS : ADC_MIN_RIPPLE_PEAK_VOLTS;
            bool inFloatRange = (_windowMinVolts >= MIN_RIPPLE_FLOAT_VOLTS) && (_windowMaxVolts >= minPeakVolts);
            bool windowRises = (_windowRiseVolts > threshold);
            bool windowFalls = (_windowFallVolts > threshold);
            float rawSpanVolts = _windowMaxVolts - _windowMinVolts;
            _lastWindowMinVolts = _windowMinVolts;
            _lastWindowMaxVolts = _windowMaxVolts;
            _lastWindowSpanVolts = rawSpanVolts;
            _lastWindowRiseVolts = _windowRiseVolts;
            _lastWindowFallVolts = _windowFallVolts;
            _lastRippleVolts = (windowRises || _seenRippleRise) ? rawSpanVolts : std::min(rawSpanVolts, _windowRiseVolts);
            _hasRippleMeasurement = true;
            bool windowHasRipple = inFloatRange && (rawSpanVolts > threshold) && (windowRises || windowFalls);

            if (windowHasRipple)
            {
               _consecutiveCalmWindows = 0;
               _seenRippleRise = _seenRippleRise || windowRises;
               _seenRippleFall = _seenRippleFall || windowFalls;
               if (_consecutiveRippleWindows < RIPPLE_DISCONNECT_WINDOWS_REQUIRED)
               {
                  _consecutiveRippleWindows++;
               }
               if (_consecutiveRippleWindows >= RIPPLE_DISCONNECT_WINDOWS_REQUIRED && _seenRippleRise && _seenRippleFall)
               {
                  _rippleDisconnected = true;
               }
            }
            else if (!inFloatRange)
            {
               _consecutiveRippleWindows = 0;
               _seenRippleRise = false;
               _seenRippleFall = false;
               _consecutiveCalmWindows = RIPPLE_CONNECT_WINDOWS_REQUIRED;
               _rippleDisconnected = false;
            }
            else
            {
               _consecutiveRippleWindows = 0;
               _seenRippleRise = false;
               _seenRippleFall = false;
               if (_consecutiveCalmWindows < RIPPLE_CONNECT_WINDOWS_REQUIRED)
               {
                  _consecutiveCalmWindows++;
               }
               if (_consecutiveCalmWindows >= RIPPLE_CONNECT_WINDOWS_REQUIRED)
               {
                  _rippleDisconnected = false;
               }
            }

            if (wasDisconnected && !_rippleDisconnected)
            {
               // Just transitioned from no-battery (~4.18V charger float) to connected
               // battery: reset the voltage-trend baseline and re-seed the fuel gauge
               // so the initial voltage drop from the float rail to the battery's true
               // voltage isn't misinterpreted as discharging.
               _resetAdcChargingTrend();
               _voltsAverage.reset();
               _voltsAverage.set(volts);
               _percentAverage.reset();
#if defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S3_TFT)
               if (_isFuelGauge())
               {
                  _fuelGauge.quickStart();
               }
#endif
            }
            else
            {
               float windowAvgVolts = static_cast<float>(_windowSumVolts / _windowSampleCount);
               _updateAdcChargingTrend(windowAvgVolts);
            }
         }

         _windowStartMs = now;
         _windowMinVolts = volts;
         _windowMaxVolts = volts;
         _windowRiseVolts = (volts > _windowLastVolts) ? (volts - _windowLastVolts) : 0.0f;
         _windowFallVolts = (volts < _windowLastVolts) ? (_windowLastVolts - volts) : 0.0f;
         _windowLastVolts = volts;
         _windowSumVolts = volts;
         _windowSampleCount = 1;
         return;
      }

      if (volts > _windowLastVolts)
      {
         _windowRiseVolts += (volts - _windowLastVolts);
      }
      else if (volts < _windowLastVolts)
      {
         _windowFallVolts += (_windowLastVolts - volts);
      }
      _windowLastVolts = volts;

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
   /// Checks whether the battery rail is exhibiting the charger no-battery sawtooth ripple
   /// across consecutive windows.
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

      uint8_t probeSamples = _isFuelGauge() ? FUEL_GAUGE_UNPLUGGED_PROBE_SAMPLE_COUNT : ADC_UNPLUGGED_PROBE_SAMPLE_COUNT;
      uint8_t probeDelayMs = _isFuelGauge() ? FUEL_GAUGE_UNPLUGGED_PROBE_DELAY_MS : ADC_UNPLUGGED_PROBE_DELAY_MS;
      float disconnectThreshold = _isFuelGauge() ? FUEL_GAUGE_RIPPLE_DISCONNECT_VOLTS : ADC_RIPPLE_DISCONNECT_VOLTS;

      float lastVolts = _sampleVolts();
      float minVolts = lastVolts;
      float maxVolts = lastVolts;
      float riseVolts = 0.0f;
      float fallVolts = 0.0f;
      double sumVolts = lastVolts;
      uint8_t sampleCount = 1;

      if (minVolts >= MIN_RIPPLE_FLOAT_VOLTS)
      {
         for (uint8_t i = 1; i < probeSamples; i++)
         {
            delay(probeDelayMs);
            float sample = _sampleVolts();
            if (sample > lastVolts)
            {
               riseVolts += (sample - lastVolts);
            }
            else if (sample < lastVolts)
            {
               fallVolts += (lastVolts - sample);
            }
            lastVolts = sample;
            sumVolts += lastVolts;
            sampleCount++;

            if (lastVolts < minVolts)
            {
               minVolts = lastVolts;
            }
            if (lastVolts > maxVolts)
            {
               maxVolts = lastVolts;
            }
            if (minVolts < MIN_RIPPLE_FLOAT_VOLTS)
            {
               break;
            }
         }
      }

      float rawSpanVolts = maxVolts - minVolts;
      bool probeRises = (riseVolts > disconnectThreshold);
      bool probeFalls = (fallVolts > disconnectThreshold);
      _lastWindowMinVolts = minVolts;
      _lastWindowMaxVolts = maxVolts;
      _lastWindowSpanVolts = rawSpanVolts;
      _lastWindowRiseVolts = riseVolts;
      _lastWindowFallVolts = fallVolts;
      _lastRippleVolts = probeRises ? rawSpanVolts : std::min(rawSpanVolts, riseVolts);
      _hasRippleMeasurement = true;
      float minPeakVolts = _isFuelGauge() ? FUEL_GAUGE_MIN_RIPPLE_PEAK_VOLTS : ADC_MIN_RIPPLE_PEAK_VOLTS;
      bool probeHasRipple = (minVolts >= MIN_RIPPLE_FLOAT_VOLTS)
         && (maxVolts >= minPeakVolts)
         && (rawSpanVolts > disconnectThreshold)
         && probeRises
         && probeFalls;
      _consecutiveRippleWindows = probeHasRipple ? RIPPLE_DISCONNECT_WINDOWS_REQUIRED : 0;
      _seenRippleRise = probeHasRipple;
      _seenRippleFall = probeHasRipple;
      _consecutiveCalmWindows = probeHasRipple ? 0 : RIPPLE_CONNECT_WINDOWS_REQUIRED;
      _rippleDisconnected = probeHasRipple;

      float probeAvgVolts = static_cast<float>(sumVolts / sampleCount);
      _voltsAverage.set(probeAvgVolts);
      _updateAdcChargingTrend(probeAvgVolts);

      _windowStartMs = millis();
      _windowMinVolts = lastVolts;
      _windowMaxVolts = lastVolts;
      _windowLastVolts = lastVolts;
      _windowRiseVolts = 0.0f;
      _windowFallVolts = 0.0f;
      _windowSumVolts = lastVolts;
      _windowSampleCount = 1;

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
      _loadCalibration();

      if (!_hasIna219)
      {
         _hasIna219 = _ina219.begin();
      }

      if (_hasIna219)
      {
#if defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S3_TFT)
         if (_pin < 0 && !_hasFuelGauge)
         {
            _hasFuelGauge = _fuelGauge.begin();
            if (_hasFuelGauge)
            {
               _fuelGauge.wake();
            }
         }
#endif
         _ready = true;
         _startTimer();
         return true;
      }

      _ready = false;
      return false;
   }

   ///
   /// <summary>
   /// Reads the current battery voltage on demand.
   /// </summary>
   /// <returns>Battery voltage in volts, or NAN if unavailable.</returns>
   ///
   float readVolts()
   {
      _lock();
      if (!_ready && !begin())
      {
         _unlock();
         return NAN;
      }

      float volts = _sampleVolts();
      _recordAdcSample(volts);
      _unlock();
      return volts;
   }

   ///
   /// <summary>
   /// Reads the time-averaged battery voltage over the 1-second smoothing window.
   /// </summary>
   /// <returns>Time-averaged battery voltage in volts, or NAN if unavailable.</returns>
   ///
   float readAverageVolts()
   {
      _lock();
      float volts = readVolts();
      float avg = _voltsAverage.average();
      _unlock();
      return isfinite(avg) ? avg : volts;
   }

   ///
   /// <summary>
   /// Checks whether a battery is currently connected (and switched on) by verifying that
   /// the measured battery rail voltage is within the valid 1S LiPo operating window
   /// (2.5V to 4.5V) and is not exhibiting the charger no-battery sawtooth ripple (~4.08V-4.20V).
   /// </summary>
   /// <returns>True if a battery is detected; false if disconnected or unavailable.</returns>
   ///
   bool isConnected()
   {
      _lock();
      if (!_ready && !begin())
      {
         _unlock();
         return false;
      }

      float volts = readVolts();
      if (!_isVoltsConnected(volts))
      {
         _resetAdcChargingTrend();
         _percentAverage.reset();
         _unlock();
         return false;
      }

      if (_hasIna219)
      {
         // When the battery is switched off or unplugged on the VIN+ side, or when the
         // charger rail sits idle with ~0 mA and sawtooth ripple on VIN-, check current:
         // any real charging (>= +5 mA) or discharging (<= -5 mA) current proves a battery
         // is physically connected. If current is near zero (< 2 mA), only consider it
         // connected if the rail is calm (not oscillating like an unloaded charger cap).
         float currentMa = readAverageCurrentMa();
         if (isfinite(currentMa) && fabsf(currentMa) >= INA219_IDLE_CURRENT_THRESHOLD_MA)
         {
            _unlock();
            return true;
         }

         if (_hasNoBatteryRipple())
         {
            _resetAdcChargingTrend();
            _percentAverage.reset();
            _unlock();
            return false;
         }

         _unlock();
         return true;
      }

      if (_hasNoBatteryRipple())
      {
         _resetAdcChargingTrend();
         _percentAverage.reset();
         _unlock();
         return false;
      }

      _unlock();
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
      _lock();
      bool bad = _isVoltsBadCharger(readVolts());
      _unlock();
      return bad;
   }

   ///
   /// <summary>
   /// Reads the instantaneous signed battery current in milliamps from the INA219 sensor
   /// (positive when charging into the battery, negative when discharging from the battery).
   /// </summary>
   /// <returns>Signed battery current in mA, or NAN if no INA219 is present.</returns>
   ///
   float readCurrentMa()
   {
      _lock();
      if (!_ready && !begin())
      {
         _unlock();
         return NAN;
      }

      if (!_hasIna219)
      {
         _unlock();
         return NAN;
      }

      float currentMa = -_ina219.getCurrent_mA();
      if (isfinite(currentMa))
      {
         _currentAverage.set(currentMa);
      }
      _unlock();
      return currentMa;
   }

   ///
   /// <summary>
   /// Reads the time-averaged signed battery current in milliamps over the 1-second smoothing
   /// window (positive when charging, negative when discharging).
   /// </summary>
   /// <returns>Time-averaged signed battery current in mA, or NAN if no INA219 is present.</returns>
   ///
   float readAverageCurrentMa()
   {
      _lock();
      float currentMa = readCurrentMa();
      float avg = _currentAverage.average();
      _unlock();
      return isfinite(avg) ? avg : currentMa;
   }

   ///
   /// <summary>
   /// Returns the charging state indicator (Yes, No, Full, Unknown, or BadCharger).
   /// When an INA219 current sensor is present, uses signed battery current directly.
   /// </summary>
   /// <returns>BatteryChargingState::Yes, No, Full, Unknown, or BadCharger.</returns>
   ///
   BatteryChargingState readChargingState()
   {
      _lock();
      if (!_ready && !begin())
      {
         _unlock();
         return BatteryChargingState::Unknown;
      }

      float volts = readVolts();
      if (_isVoltsBadCharger(volts))
      {
         _resetAdcChargingTrend();
         _unlock();
         return BatteryChargingState::BadCharger;
      }

      if (!isConnected())
      {
         _unlock();
         return BatteryChargingState::Unknown;
      }

      if (_hasIna219)
      {
         float currentMa = readAverageCurrentMa();
         float avgVolts = readAverageVolts();
         float checkVolts = isfinite(avgVolts) ? avgVolts : volts;

         if (isfinite(currentMa))
         {
            if (currentMa >= INA219_CHARGE_CURRENT_THRESHOLD_MA)
            {
               _unlock();
               return BatteryChargingState::Yes;
            }

            if (currentMa <= INA219_DISCHARGE_CURRENT_THRESHOLD_MA)
            {
               _unlock();
               return BatteryChargingState::No;
            }

            if (checkVolts >= INA219_FULL_MIN_VOLTS)
            {
               _saveFullChargeVolts(checkVolts);
               _unlock();
               return BatteryChargingState::Full;
            }
         }

         _unlock();
         return BatteryChargingState::Unknown;
      }

#if defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S3_TFT)
      if (_pin < 0)
      {
         float rate = readChargeRate();
         if (!isfinite(rate))
         {
            _unlock();
            return BatteryChargingState::Unknown;
         }

         if (rate >= MIN_FUEL_GAUGE_CHARGE_RATE)
         {
            _unlock();
            return BatteryChargingState::Yes;
         }

         float avgVolts = _voltsAverage.average();
         float checkVolts = isfinite(avgVolts) ? avgVolts : volts;
         float percent = readAveragePercent();
         bool standbyFull = (checkVolts >= FUEL_GAUGE_STANDBY_FULL_VOLTS)
            && (_lastWindowSpanVolts > 0.0f)
            && (_lastWindowSpanVolts <= FUEL_GAUGE_STANDBY_MAX_SPAN_VOLTS)
            && (_adcChargingState != BatteryChargingState::No);
         bool atFullCharge = (checkVolts >= _fullChargeVolts)
            || standbyFull
            || (isfinite(percent) && percent >= FULL_CHARGE_PERCENT);

         if (_adcChargingState == BatteryChargingState::No && checkVolts < _fullChargeVolts && !standbyFull)
         {
            // Voltage has measurably dropped off the charger plateau -> discharging.
            _unlock();
            return BatteryChargingState::No;
         }

         if (atFullCharge)
         {
            // On the charger plateau (>= 4.185V) or steady at 100% SoC without a voltage drop:
            // ignore negative MAX17048 CRATE slope lag after voltage steps or charge termination.
            _unlock();
            return BatteryChargingState::Full;
         }

         if (rate <= -MIN_FUEL_GAUGE_CHARGE_RATE)
         {
            _unlock();
            return BatteryChargingState::No;
         }

         BatteryChargingState state = _adcChargingState;
         _unlock();
         return state;
      }
#endif

      BatteryChargingState state = _adcChargingState;
      _unlock();
      return state;
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
      _lock();
      if (!_ready && !begin())
      {
         _unlock();
         return NAN;
      }

      float volts = readVolts();
      if (!_isVoltsConnected(volts))
      {
         _percentAverage.reset();
         _unlock();
         return NAN;
      }

      if (_hasNoBatteryRipple())
      {
         _percentAverage.reset();
         _unlock();
         return NAN;
      }

      float percent = NAN;
#if defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S3_TFT)
      if (_isFuelGauge())
      {
         float rawPercent = _fuelGauge.cellPercent();
         float curvePercent = _voltsToPercent(volts, _fullChargeVolts);
         if (isfinite(rawPercent))
         {
            // If this battery/board's learned full-charge plateau is lower than the
            // MAX17048's factory 4.20V model (e.g. ~4.13V after MCP73831 termination),
            // scale rawPercent up so reaching the saved _fullChargeVolts reports 100%.
            percent = constrain(std::max(rawPercent, curvePercent), 0.0f, 100.0f);
         }
         else
         {
            percent = curvePercent;
         }
      }
      else
#endif
      {
         percent = _voltsToPercent(volts, _fullChargeVolts);
      }

      if (isfinite(percent))
      {
         _percentAverage.set(percent);
      }

      _unlock();
      return percent;
   }

   ///
   /// <summary>
   /// Reads the time-averaged battery charge percentage over the 1-second smoothing window.
   /// </summary>
   /// <returns>Time-averaged battery charge percentage (0.0 to 100.0), or NAN if disconnected or unavailable.</returns>
   ///
   float readAveragePercent()
   {
      _lock();
      float percent = readPercent();
      if (!isfinite(percent))
      {
         _unlock();
         return NAN;
      }

      float avg = _percentAverage.average();
      _unlock();
      return isfinite(avg) ? avg : percent;
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
      if (_isFuelGauge())
      {
         _lock();
         if (!_ready && !begin())
         {
            _unlock();
            return NAN;
         }

         float rate = _fuelGauge.chargeRate();
         _unlock();
         return rate;
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
      _lock();
      float ripple = _lastRippleVolts;
      _unlock();
      return ripple;
   }

   ///
   /// <summary>
   /// Returns the minimum voltage recorded during the most recently completed ripple window.
   /// </summary>
   /// <returns>Minimum window voltage in volts, or NAN if no window has completed yet.</returns>
   ///
   float readWindowMinVolts() const
   {
      _lock();
      float val = _lastWindowMinVolts;
      _unlock();
      return val;
   }

   ///
   /// <summary>
   /// Returns the maximum voltage recorded during the most recently completed ripple window.
   /// </summary>
   /// <returns>Maximum window voltage in volts, or NAN if no window has completed yet.</returns>
   ///
   float readWindowMaxVolts() const
   {
      _lock();
      float val = _lastWindowMaxVolts;
      _unlock();
      return val;
   }

   ///
   /// <summary>
   /// Returns the raw peak-to-peak voltage span (max - min) recorded during the most
   /// recently completed ripple window, prior to directional sawtooth gating.
   /// </summary>
   /// <returns>Raw window voltage span in volts.</returns>
   ///
   float readWindowSpanVolts() const
   {
      _lock();
      float val = _lastWindowSpanVolts;
      _unlock();
      return val;
   }

   ///
   /// <summary>
   /// Returns the cumulative upward voltage step sum recorded during the most recently
   /// completed ripple window.
   /// </summary>
   /// <returns>Cumulative upward voltage change in volts.</returns>
   ///
   float readWindowRiseVolts() const
   {
      _lock();
      float val = _lastWindowRiseVolts;
      _unlock();
      return val;
   }

   ///
   /// <summary>
   /// Returns the cumulative downward voltage step sum recorded during the most recently
   /// completed ripple window.
   /// </summary>
   /// <returns>Cumulative downward voltage change in volts.</returns>
   ///
   float readWindowFallVolts() const
   {
      _lock();
      float val = _lastWindowFallVolts;
      _unlock();
      return val;
   }

   ///
   /// <summary>
   /// Reads the raw state-of-charge percentage from the fuel gauge or voltage curve without
   /// gating on isConnected(), for diagnostic logging.
   /// </summary>
   /// <returns>Raw battery percentage (0.0 to 100.0), or NAN if hardware is unavailable.</returns>
   ///
   float readRawPercent()
   {
      _lock();
      if (!_ready && !begin())
      {
         _unlock();
         return NAN;
      }

#if defined(ARDUINO_ADAFRUIT_FEATHER_ESP32S3_TFT)
      if (_isFuelGauge())
      {
         float rawPercent = _fuelGauge.cellPercent();
         _unlock();
         return isfinite(rawPercent) ? constrain(rawPercent, 0.0f, 100.0f) : NAN;
      }
#endif

      float volts = _sampleVolts();
      float fullVolts = _fullChargeVolts;
      _unlock();
      return isfinite(volts) ? _voltsToPercent(volts, fullVolts) : NAN;
   }

   ///
   /// <summary>
   /// Returns the currently calibrated 100% full-charge plateau voltage (in volts) for this
   /// battery and board.
   /// </summary>
   /// <returns>Calibrated full-charge plateau voltage in volts.</returns>
   ///
   float readFullChargeVolts() const
   {
      _lock();
      float val = _fullChargeVolts;
      _unlock();
      return val;
   }
};
