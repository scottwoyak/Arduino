#pragma once

#include <Arduino.h>
#include <esp_timer.h>
#include <array>
#include <cmath>
#include <sstream>
#include <string>

#include "AD7606.h"
#include "CTPowerSensor.h"
#include "Util.h"

///
/// <summary>
/// Multi-channel AC current and apparent power sensor using an Analog Devices AD7606
/// 8-channel, 16-bit simultaneous-sampling bipolar ADC.
///
/// Unlike multiplexed ADCs, the AD7606 latches all 8 CT channels (V1..V8) at the exact
/// same microsecond on every sample tick and accepts true bipolar AC inputs (+/-5V)
/// directly referenced to AGND without requiring a 1.65V mid-supply bias divider.
/// </summary>
///
class AD7606PowerSensor
{
public:
   ///
   /// <summary>
   /// Number of simultaneous CT channels supported by a single AD7606.
   /// </summary>
   ///
   static constexpr size_t MAX_CHANNELS = AD7606::NUM_CHANNELS;

   ///
   /// <summary>
   /// Standard split-core CT current ratings (Amps per 1.0V RMS output).
   /// </summary>
   ///
   static constexpr float CORE_15A = CTPowerSensor::CORE_15A;
   static constexpr float CORE_20A = CTPowerSensor::CORE_20A;
   static constexpr float CORE_30A = CTPowerSensor::CORE_30A;
   static constexpr float CORE_50A = CTPowerSensor::CORE_50A;
   static constexpr float CORE_100A = CTPowerSensor::CORE_100A;
   static constexpr float CORE_150A = CTPowerSensor::CORE_150A;
   static constexpr float CORE_200A = CTPowerSensor::CORE_200A;
   static constexpr float CORE_213A = CTPowerSensor::CORE_213A;
   static constexpr float CORE_500A = CTPowerSensor::CORE_500A;

   static constexpr float DEFAULT_LINE_VOLTAGE_V = 120.0f;
   static constexpr float DEFAULT_NOISE_FLOOR_AMPS = 0.05f;
   static constexpr float UNPLUGGED_DC_OFFSET_THRESHOLD_V = 0.20f;

   ///
   /// <summary>
   /// Per-channel configuration for one of the 8 AD7606 inputs (V1..V8).
   /// </summary>
   ///
   struct ChannelConfig
   {
      const char* name = nullptr;
      float ampsPerVolt = CORE_100A;
   };

private:
   static constexpr uint32_t SAMPLE_INTERVAL_MICROS = 1042; // 960 Hz (16 samples per 60 Hz cycle; 48 samples = 50.0 ms = 3 cycles)
   static constexpr uint8_t SAMPLES_PER_RMS_SLICE = 48;     // 48 x 1.0417 ms = 50.0 ms (3 full AC cycles at 60 Hz across all 8 channels simultaneously)
   static constexpr size_t NUM_TRACE_SLICES = 4;            // 4 x 50 ms = 200 ms rolling window per channel

   struct ChannelState
   {
      const char* name = "";
      float ampsPerVolt = CORE_100A;
      double sumV = 0.0;
      double sumV2 = 0.0;
      std::array<float, NUM_TRACE_SLICES> traceAmps = {};
      size_t traceWriteIndex = 0;
      size_t traceCount = 0;
      float dcVolts = 0.0f;
      float rmsVolts = 0.0f;
      float rawAmps = 0.0f;
      float latchedAmps = 0.0f;
      bool connected = true;
      bool latchedConnected = true;
   };

   AD7606 _adc;
   size_t _numChannels = MAX_CHANNELS;
   std::array<ChannelState, MAX_CHANNELS> _channels = {};
   float _lineVoltageV = DEFAULT_LINE_VOLTAGE_V;
   float _noiseFloorAmps = DEFAULT_NOISE_FLOOR_AMPS;
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
   /// Reads all 8 AD7606 channels simultaneously on each 960 Hz timer tick, accumulates
   /// sum(V) and sum(V^2) for all 8 channels in parallel, and every 48 samples (50 ms =
   /// 3 full 60 Hz cycles) computes and latches the RMS current for all 8 channels at the
   /// exact same instant.
   /// </summary>
   ///
   void _onSampleTick()
   {
      float volts[MAX_CHANNELS];
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

      for (size_t i = 0; i < _numChannels; i++)
      {
         ChannelState& ch = _channels[i];
         double meanV = ch.sumV / _sliceSampleCount;
         double meanSquareV = ch.sumV2 / _sliceSampleCount;
         double varianceV = meanSquareV - (meanV * meanV);
         if (varianceV < 0.0)
         {
            varianceV = 0.0;
         }

         float sliceRmsVolts = static_cast<float>( std::sqrt(varianceV) );
         float sliceAmps = sliceRmsVolts * ch.ampsPerVolt;
         if (sliceAmps < _noiseFloorAmps)
         {
            sliceAmps = 0.0f;
         }

         ch.dcVolts = static_cast<float>(meanV);
         ch.rmsVolts = sliceRmsVolts;
         ch.rawAmps = sliceAmps;

         // On AD7606, an optional pull-up resistor (e.g. 2.2M to 3.3V) pulls an unplugged input
         // above UNPLUGGED_DC_OFFSET_THRESHOLD_V, whereas a connected CT holds the DC mean near 0V.
         // If no pull-up is fitted, dcVolts stays near 0V and the channel remains marked connected.
         ch.connected = std::fabs(ch.dcVolts) < UNPLUGGED_DC_OFFSET_THRESHOLD_V;
         if (!ch.connected)
         {
            ch.rmsVolts = 0.0f;
            ch.rawAmps = 0.0f;
            ch.traceCount = 0;
            ch.traceWriteIndex = 0;
            ch.latchedAmps = 0.0f;
            ch.latchedConnected = false;
         }
         else
         {
            ch.traceAmps[ch.traceWriteIndex] = sliceAmps;
            ch.traceWriteIndex = (ch.traceWriteIndex + 1) % NUM_TRACE_SLICES;
            if (ch.traceCount < NUM_TRACE_SLICES)
            {
               ch.traceCount++;
            }

            float sum = 0.0f;
            for (size_t j = 0; j < ch.traceCount; j++)
            {
               sum += ch.traceAmps[j];
            }
            float avg = sum / ch.traceCount;
            ch.latchedAmps = avg < _noiseFloorAmps ? 0.0f : avg;
            ch.latchedConnected = true;
         }

         ch.sumV = 0.0;
         ch.sumV2 = 0.0;
      }

      _sliceSampleCount = 0;
   }

public:
   ///
   /// <summary>
   /// Creates an 8-channel simultaneous AD7606 CT power sensor using the specified GPIO pins
   /// and per-channel configurations.
   /// </summary>
   /// <param name="pinConvst">GPIO pin connected to AD7606 CONVSTA+CONVSTB (default GPIO2).</param>
   /// <param name="pinSclk">GPIO pin connected to AD7606 RD/SCLK (default GPIO3).</param>
   /// <param name="pinMiso">GPIO pin connected to AD7606 DB7/DOUTA (default GPIO14).</param>
   /// <param name="pinCs">GPIO pin connected to AD7606 CS (default GPIO21).</param>
   /// <param name="channelConfigs">Array of per-channel configurations.</param>
   /// <param name="numChannels">Number of active channels (1 to 8).</param>
   /// <param name="lineVoltageV">Nominal AC line voltage in volts (default 120.0V).</param>
   /// <param name="range">AD7606 bipolar voltage range (default +/-5V).</param>
   /// <param name="pinBusy">Optional GPIO pin connected to AD7606 BUSY (-1 if not wired).</param>
   /// <param name="pinReset">Optional GPIO pin connected to AD7606 RST (-1 if tied low).</param>
   ///
   AD7606PowerSensor(
      uint8_t pinConvst,
      uint8_t pinSclk,
      uint8_t pinMiso,
      uint8_t pinCs,
      const ChannelConfig* channelConfigs,
      size_t numChannels = MAX_CHANNELS,
      float lineVoltageV = DEFAULT_LINE_VOLTAGE_V,
      AD7606::Range range = AD7606::Range::BIPOLAR_5V,
      int8_t pinBusy = -1,
      int8_t pinReset = -1)
      : _adc(pinConvst, pinSclk, pinMiso, pinCs, range, pinBusy, pinReset)
   {
      ASSERT(channelConfigs != nullptr);
      ASSERT(numChannels >= 1 && numChannels <= MAX_CHANNELS);

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
   /// Initializes the AD7606 hardware interface and starts the periodic 960 Hz esp_timer
   /// that simultaneously samples all 8 channels in the background.
   /// </summary>
   /// <returns>True if the AD7606 initialized and the background timer started.</returns>
   ///
   bool begin()
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
   /// Gets the number of enabled CT channels (1 to 8).
   /// </summary>
   /// <returns>Number of enabled channels.</returns>
   ///
   size_t channelCount() const
   {
      return _numChannels;
   }

   ///
   /// <summary>
   /// Sets the number of active CT channels (1 to MAX_CHANNELS).
   /// </summary>
   /// <param name="numChannels">Number of enabled channels.</param>
   ///
   void setChannelCount(size_t numChannels)
   {
      ASSERT(numChannels >= 1 && numChannels <= MAX_CHANNELS);

      _numChannels = numChannels;
   }

   ///
   /// <summary>
   /// Gets the configured name/suffix for the specified channel index.
   /// </summary>
   /// <param name="channel">Channel index (0 to channelCount() - 1).</param>
   /// <returns>Channel name string.</returns>
   ///
   const char* channelName(size_t channel) const
   {
      ASSERT(channel < MAX_CHANNELS);

      return _channels[channel].name;
   }

   ///
   /// <summary>
   /// Sets the name/suffix for the specified channel index.
   /// </summary>
   /// <param name="channel">Channel index (0 to MAX_CHANNELS - 1).</param>
   /// <param name="name">Channel name/suffix string.</param>
   ///
   void setChannelName(size_t channel, const char* name)
   {
      ASSERT(channel < MAX_CHANNELS);
      ASSERT(name != nullptr);

      _channels[channel].name = name;
   }

   ///
   /// <summary>
   /// Sets the split-core CT rating in Amps per 1.0V RMS (e.g. CORE_30A or CORE_100A) for a channel.
   /// If the rating changes, any buffered trace slices for that channel are rescaled immediately.
   /// </summary>
   /// <param name="ampsPerVolt">Amps per 1.0V RMS sensor output.</param>
   /// <param name="channel">Channel index (default 0).</param>
   ///
   void setCoreRating(float ampsPerVolt, size_t channel = 0)
   {
      ASSERT(channel < MAX_CHANNELS);

      ChannelState& ch = _channels[channel];
      if (ch.ampsPerVolt > 0.0f && ch.ampsPerVolt != ampsPerVolt)
      {
         float scale = ampsPerVolt / ch.ampsPerVolt;
         for (size_t i = 0; i < ch.traceCount; i++)
         {
            ch.traceAmps[i] *= scale;
         }
         ch.rawAmps *= scale;
         ch.latchedAmps *= scale;
      }

      ch.ampsPerVolt = ampsPerVolt;
   }

   ///
   /// <summary>
   /// Gets the configured split-core CT rating in Amps per 1.0V RMS for a channel.
   /// </summary>
   /// <param name="channel">Channel index (default 0).</param>
   /// <returns>Amps per 1.0V RMS.</returns>
   ///
   float coreRating(size_t channel = 0) const
   {
      ASSERT(channel < MAX_CHANNELS);

      return _channels[channel].ampsPerVolt;
   }

   ///
   /// <summary>
   /// Sets the nominal AC line voltage used to compute apparent power in Watts.
   /// </summary>
   /// <param name="lineVoltageV">AC line voltage in volts (e.g. 120.0f or 240.0f).</param>
   ///
   void setLineVoltage(float lineVoltageV)
   {
      _lineVoltageV = lineVoltageV;
   }

   ///
   /// <summary>
   /// Gets the configured AC line voltage in volts.
   /// </summary>
   /// <returns>AC line voltage in volts.</returns>
   ///
   float lineVoltage() const
   {
      return _lineVoltageV;
   }

   ///
   /// <summary>
   /// Sets the minimum RMS current threshold below which readings are clamped to 0.0 A.
   /// </summary>
   /// <param name="noiseFloorAmps">Noise floor in Amps.</param>
   ///
   void setNoiseFloorAmps(float noiseFloorAmps)
   {
      _noiseFloorAmps = noiseFloorAmps;
   }

   ///
   /// <summary>
   /// Gets the smoothed RMS current in Amps for the specified channel from the latest
   /// simultaneous 8-channel snapshot.
   /// </summary>
   /// <param name="channel">Channel index (default 0).</param>
   /// <returns>Smoothed RMS current in Amps.</returns>
   ///
   float amps(size_t channel = 0) const
   {
      ASSERT(channel < MAX_CHANNELS);

      return _channels[channel].latchedAmps;
   }

   ///
   /// <summary>
   /// Gets the smoothed apparent power in Watts (amps(channel) * lineVoltage()) for the
   /// specified channel.
   /// </summary>
   /// <param name="channel">Channel index (default 0).</param>
   /// <returns>Smoothed apparent power in Watts.</returns>
   ///
   float watts(size_t channel = 0) const
   {
      return amps(channel) * _lineVoltageV;
   }

   ///
   /// <summary>
   /// Gets the total smoothed RMS current in Amps summed across all enabled channels.
   /// </summary>
   /// <returns>Total smoothed RMS current in Amps.</returns>
   ///
   float totalAmps() const
   {
      float total = 0.0f;
      for (size_t i = 0; i < _numChannels; i++)
      {
         total += amps(i);
      }
      return total;
   }

   ///
   /// <summary>
   /// Gets the total smoothed apparent power in Watts summed across all enabled channels.
   /// </summary>
   /// <returns>Total smoothed apparent power in Watts.</returns>
   ///
   float totalWatts() const
   {
      return totalAmps() * _lineVoltageV;
   }

   ///
   /// <summary>
   /// Gets the instantaneous RMS current in Amps from the most recent 50 ms (3-cycle) slice
   /// for the specified channel.
   /// </summary>
   /// <param name="channel">Channel index (default 0).</param>
   /// <returns>Most recent 50 ms RMS current in Amps.</returns>
   ///
   float rawAmps(size_t channel = 0) const
   {
      ASSERT(channel < MAX_CHANNELS);

      return _channels[channel].rawAmps;
   }

   ///
   /// <summary>
   /// Gets the instantaneous RMS voltage at the AD7606 input from the most recent 50 ms slice
   /// for the specified channel.
   /// </summary>
   /// <param name="channel">Channel index (default 0).</param>
   /// <returns>Most recent 50 ms sensor RMS voltage in volts.</returns>
   ///
   float rmsVolts(size_t channel = 0) const
   {
      ASSERT(channel < MAX_CHANNELS);

      return _channels[channel].rmsVolts;
   }

   ///
   /// <summary>
   /// Gets the mean DC offset voltage from the most recent 50 ms slice.
   /// </summary>
   /// <param name="channel">Channel index (default 0).</param>
   /// <returns>Mean DC offset voltage in volts.</returns>
   ///
   float dcVolts(size_t channel = 0) const
   {
      ASSERT(channel < MAX_CHANNELS);

      return _channels[channel].dcVolts;
   }

   ///
   /// <summary>
   /// Returns true if a CT sensor is connected on the specified channel.
   /// </summary>
   /// <param name="channel">Channel index (default 0).</param>
   /// <returns>True if connected; false if an open-input pull-up offset is detected.</returns>
   ///
   bool isConnected(size_t channel = 0) const
   {
      ASSERT(channel < MAX_CHANNELS);

      return _channels[channel].latchedConnected;
   }

   ///
   /// <summary>
   /// Gets the measured background simultaneous 8-channel sample rate in samples per second per channel.
   /// </summary>
   /// <returns>Measured samples per second per channel.</returns>
   ///
   uint32_t sampleRate() const
   {
      return _samplesPerSec;
   }

   ///
   /// <summary>
   /// Formats the latest smoothed and instantaneous power/current, sensor RMS voltage,
   /// sample rate, and per-channel 50 ms trace for the "GetReadings" command.
   /// </summary>
   /// <returns>Formatted multi-line readings string.</returns>
   ///
   std::string formatReadings() const
   {
      std::ostringstream text;
      text << std::fixed;
      text << "channels=" << _numChannels << "\n"
           << "sampleRate=" << _samplesPerSec << "/s\n";

      if (_numChannels > 1)
      {
         text.precision(1);
         text << "totalPower=" << totalWatts() << " W\n"
              << "totalCurrent=" << totalAmps() << " A\n";
      }

      for (size_t chIdx = 0; chIdx < _numChannels; chIdx++)
      {
         const ChannelState& ch = _channels[chIdx];
         std::string prefix = _numChannels == 1
            ? ""
            : (std::string("ch") + std::to_string(chIdx + 1) + "(" + ch.name + ").");

         float chAmps = amps(chIdx);
         float chWatts = chAmps * _lineVoltageV;

         text.precision(1);
         text << prefix << "connected=" << (ch.latchedConnected ? "true" : "false") << "\n"
              << prefix << "power=" << chWatts << " W\n"
              << prefix << "current=" << chAmps << " A\n"
              << prefix << "rawCurrent=" << ch.rawAmps << " A\n";
         text.precision(4);
         text << prefix << "sensorRms=" << ch.rmsVolts << " V\n"
              << prefix << "dcOffset=" << ch.dcVolts << " V\n"
              << prefix << "coreRating=" << static_cast<int>(ch.ampsPerVolt + 0.5f) << " A\n"
              << prefix << "trace50ms=";

         text.precision(1);
         size_t count = ch.traceCount;
         size_t startIdx = (ch.traceWriteIndex + NUM_TRACE_SLICES - count) % NUM_TRACE_SLICES;
         for (size_t i = 0; i < count; i++)
         {
            if (i > 0)
            {
               text << ",";
            }
            text << ch.traceAmps[(startIdx + i) % NUM_TRACE_SLICES];
         }

         if (chIdx + 1 < _numChannels)
         {
            text << "\n";
         }
      }

      return text.str();
   }
};
