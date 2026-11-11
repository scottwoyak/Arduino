#pragma once

#include <Arduino.h>
#include <esp_timer.h>
#include <cmath>

#include "AD7606.h"
#include "PowerSensorBase.h"
#include "Util.h"

///
/// <summary>
/// Multi-channel AC current and apparent power sensor using an Analog Devices AD7606
/// 8-channel, 16-bit simultaneous-sampling bipolar ADC.
///
/// The AD7606 latches all 8 CT channels (V1..V8) at the exact same microsecond on every sample
/// tick and accepts true bipolar AC inputs (+/-5V) directly referenced to AGND without requiring
/// a mid-supply bias divider.
/// </summary>
///
class AD7606PowerSensor : public PowerSensorBase
{
public:
   static constexpr float UNPLUGGED_DC_OFFSET_THRESHOLD_V = 0.20f;

private:
   static constexpr uint32_t SAMPLE_INTERVAL_MICROS = 1042; // 960 Hz (16 samples per 60 Hz cycle)
   static constexpr uint8_t SAMPLES_PER_RMS_SLICE = 48;     // 48 samples = 50.0 ms = 3 cycles at 60 Hz

   AD7606 _adc;
   esp_timer_handle_t _sampleTimer = nullptr;

   uint8_t _sliceSampleCount = 0;
   uint32_t _rateSampleCount = 0;
   uint32_t _lastRateMillis = 0;
   uint32_t _samplesPerSec = 0;

   ///
   /// <summary>
   /// Static esp_timer callback that forwards to the sensor instance.
   /// </summary>
   /// <param name="arg">Pointer to the AD7606PowerSensor instance.</param>
   ///
   static void _timerCallback(void* arg)
   {
      static_cast<AD7606PowerSensor*>(arg)->_onSampleTick();
   }

   ///
   /// <summary>
   /// Reads all 8 channels simultaneously, accumulates sum(V) and sum(V^2), and every 48
   /// samples (3 full 60 Hz cycles) latches the RMS current for all channels.
   /// </summary>
   ///
   void _onSampleTick()
   {
      float volts[AD7606::NUM_CHANNELS];
      if (!_adc.readAllVolts(volts))
      {
         return;
      }

      _rateSampleCount++;
      uint32_t nowMs = millis();
      if ((nowMs - _lastRateMillis) >= 1000)
      {
         _samplesPerSec = (_rateSampleCount * 1000) / (nowMs - _lastRateMillis);
         _rateSampleCount = 0;
         _lastRateMillis = nowMs;
      }

      for (size_t i = 0; i < _numChannels; i++)
      {
         double v = static_cast<double>(volts[i]);
         _channels[i].sumV += v;
         _channels[i].sumV2 += v * v;
      }

      _sliceSampleCount++;
      if (_sliceSampleCount < SAMPLES_PER_RMS_SLICE)
      {
         return;
      }

      // An optional pull-up on an unplugged input lifts the DC mean above the threshold,
      // whereas a connected CT holds it near 0V.
      for (size_t i = 0; i < _numChannels; i++)
      {
         _finishSlice(i, _sliceSampleCount, [](float meanVolts)
         {
            return std::fabs(meanVolts) < UNPLUGGED_DC_OFFSET_THRESHOLD_V;
         });
      }

      _sliceSampleCount = 0;
   }

public:
   ///
   /// <summary>
   /// Creates an AD7606 CT power sensor using the specified GPIO pins and channel configurations.
   /// </summary>
   /// <param name="pinConvst">GPIO pin connected to CONVSTA+CONVSTB.</param>
   /// <param name="pinSclk">GPIO pin connected to RD/SCLK.</param>
   /// <param name="pinMiso">GPIO pin connected to DB7/DOUTA.</param>
   /// <param name="pinCs">GPIO pin connected to CS.</param>
   /// <param name="channelConfigs">Array of per-channel configurations.</param>
   /// <param name="numChannels">Number of active channels (1 to 8).</param>
   /// <param name="lineVoltageV">Nominal AC line voltage in volts.</param>
   /// <param name="range">AD7606 bipolar voltage range.</param>
   /// <param name="pinBusy">Optional GPIO pin connected to BUSY (-1 if not wired).</param>
   /// <param name="pinReset">Optional GPIO pin connected to RST (-1 if tied low).</param>
   ///
   AD7606PowerSensor(
      uint8_t pinConvst,
      uint8_t pinSclk,
      uint8_t pinMiso,
      uint8_t pinCs,
      const ChannelConfig* channelConfigs,
      size_t numChannels = AD7606::NUM_CHANNELS,
      float lineVoltageV = DEFAULT_LINE_VOLTAGE_V,
      AD7606::Range range = AD7606::Range::BIPOLAR_5V,
      int8_t pinBusy = -1,
      int8_t pinReset = -1)
      : _adc(pinConvst, pinSclk, pinMiso, pinCs, range, pinBusy, pinReset)
   {
      ASSERT(channelConfigs != nullptr);
      ASSERT(numChannels >= 1 && numChannels <= AD7606::NUM_CHANNELS);

      _numChannels = numChannels;
      _lineVoltageV = lineVoltageV;

      for (size_t i = 0; i < _numChannels; i++)
      {
         _channels[i].name = channelConfigs[i].name != nullptr ? channelConfigs[i].name : "";
         _channels[i].ampsPerVolt = channelConfigs[i].ampsPerVolt;
      }
   }

   ///
   /// <summary>
   /// Stops the background sampling timer if active.
   /// </summary>
   ///
   ~AD7606PowerSensor()
   {
      if (_sampleTimer != nullptr)
      {
         esp_timer_stop(_sampleTimer);
         esp_timer_delete(_sampleTimer);
         _sampleTimer = nullptr;
      }
   }

   ///
   /// <summary>
   /// Initializes the AD7606 and starts the periodic 960 Hz sampling timer.
   /// </summary>
   /// <returns>True if the AD7606 initialized and the timer started.</returns>
   ///
   bool begin() override
   {
      if (!_adc.begin())
      {
         return false;
      }

      _lastRateMillis = millis();

      if (_sampleTimer == nullptr)
      {
         esp_timer_create_args_t timerArgs = {};
         timerArgs.callback = &_timerCallback;
         timerArgs.arg = this;
         timerArgs.dispatch_method = ESP_TIMER_TASK;
         timerArgs.name = "AD7606PowerSensor";

         if (esp_timer_create(&timerArgs, &_sampleTimer) != ESP_OK)
         {
            _sampleTimer = nullptr;
            return false;
         }

         if (esp_timer_start_periodic(_sampleTimer, SAMPLE_INTERVAL_MICROS) != ESP_OK)
         {
            esp_timer_delete(_sampleTimer);
            _sampleTimer = nullptr;
            return false;
         }
      }

      return true;
   }

   ///
   /// <summary>
   /// Gets the measured simultaneous sample rate in samples per second per channel.
   /// </summary>
   /// <returns>Measured samples per second per channel.</returns>
   ///
   uint32_t sampleRate() const override
   {
      return _samplesPerSec;
   }
};
