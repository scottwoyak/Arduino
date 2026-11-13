#pragma once

#include <Arduino.h>
#include <array>
#include <cmath>
#include <esp_timer.h>
#include <sstream>
#include <string>

#include "ADS1115.h"
#include "Util.h"

///
/// <summary>
/// Measures RMS current (A) and estimates apparent power (VA) across up to 3 split-core current
/// transformers (such as the SCT-013-030 30A/1V, SCT-013-100 100A/1V, or SCT023R 100A:50mA)
/// connected to a single ADS1115 16-bit ADC using AIN3 as the shared 1.65V mid-supply
/// differential reference (DIFF_A0_A3, DIFF_A1_A3, DIFF_A2_A3).
/// </summary>
/// <remarks>
/// Runs an ESP32 hardware periodic timer at the ADS1115's native 860 SPS rate (1163 us)
/// to sample the 60 Hz AC waveform continuously in the background regardless of main-loop,
/// display, or WiFi work. Each enabled channel is sampled for a full 50 ms (43 samples =
/// 3 complete 60 Hz AC cycles) before switching the multiplexer to the next enabled channel
/// (discarding the first settling sample after a mux change). Variance-based RMS
/// (V_rms = sqrt(E[V^2] - E[V]^2)) automatically strips any DC bias offset, scales by that
/// channel's split-core Amp/Volt rating, and stores the slice in a per-channel 1.0-second
/// ring buffer.
/// </remarks>
///
class CTPowerSensor
{
public:
   ///
   /// <summary>
   /// Maximum number of differential CT channels supported directly on a single ADS1115 using
   /// AIN3 as the shared reference pin (AIN0-AIN3, AIN1-AIN3, AIN2-AIN3).
   /// </summary>
   ///
   static constexpr size_t MAX_CHANNELS = 3;

   ///
   /// <summary>
   /// Maximum number of CT channels supported when using an external 16-channel analog
   /// multiplexer (e.g. CD74HC4067) routed into ADS1115 AIN0 with AIN3 as the shared reference.
   /// </summary>
   ///
   static constexpr size_t MAX_MUX_CHANNELS = 16;

   ///
   /// <summary>
   /// Standard 1V-output split-core CT current ratings (Amps per 1.0V RMS output).
   /// </summary>
   ///
   static constexpr float CORE_15A = 15.0f;
   static constexpr float CORE_20A = 20.0f;
   static constexpr float CORE_30A = 30.0f;
   static constexpr float CORE_50A = 50.0f;
   static constexpr float CORE_100A = 100.0f;
   static constexpr float CORE_150A = 150.0f;
   static constexpr float CORE_200A = 200.0f;
   static constexpr float CORE_213A = 10000.0f / 47.0f; // 500A:50mA (10000:1) CT with 47 ohm burden resistor (~212.77 A/V)
   static constexpr float CORE_500A = 500.0f;

   static constexpr float DEFAULT_LINE_VOLTAGE_V = 120.0f;
   static constexpr float DEFAULT_NOISE_FLOOR_AMPS = 0.05f;
   static constexpr float UNPLUGGED_DC_OFFSET_THRESHOLD_V = 0.20f;

   ///
   /// <summary>
   /// Per-channel configuration for a split-core CT connected between AINx and AIN3.
   /// </summary>
   ///
   struct ChannelConfig
   {
      const char* name = nullptr;
      float ampsPerVolt = CORE_100A;
      ADS1115::Mux mux = ADS1115::Mux::DIFF_A0_A3;
      uint8_t externalMuxChannel = 0;
   };

private:
   static constexpr uint8_t SAMPLES_PER_RMS_SLICE = 43; // 43 x 1.163 ms = 50 ms (3 full AC cycles at 60 Hz)
   static constexpr uint8_t SAMPLES_PER_PROBE = 2;      // 2 samples (~2.3 ms) to check DC offset on unplugged channels
   static constexpr size_t NUM_TRACE_SLICES = 20;       // Up to 20 x 50 ms = 1.0 second window (scaled by channel count)

   static constexpr std::array<ADS1115::Mux, MAX_CHANNELS> DEFAULT_SHARED_A3_MUXES = {
      ADS1115::Mux::DIFF_A0_A3,
      ADS1115::Mux::DIFF_A1_A3,
      ADS1115::Mux::DIFF_A2_A3,
   };

   struct ChannelState
   {
      const char* name = "";
      float ampsPerVolt = CORE_100A;
      ADS1115::Mux mux = ADS1115::Mux::DIFF_A0_A3;
      uint8_t externalMuxChannel = 0;
      std::array<float, NUM_TRACE_SLICES> traceAmps = {};
      size_t traceWriteIndex = 0;
      size_t traceCount = 0;
      float dcVolts = 0.0f;
      float rmsVolts = 0.0f;
      float rawAmps = 0.0f;
      float latchedAmps = 0.0f;
      bool connected = false;
      bool latchedConnected = false;
   };

   ADS1115 _adc;
   size_t _numChannels = 1;
   std::array<ChannelState, MAX_MUX_CHANNELS> _channels = {};
   size_t _activeChannel = 0;
   size_t _probeCursor = 0;
   bool _probingUnconnected = false;
   uint8_t _SettleSamplesRemaining = 0;
   bool _useExternalMux = false;
   std::array<uint8_t, 4> _muxSelectPins = { 0, 0, 0, 0 };

   float _lineVoltageV = DEFAULT_LINE_VOLTAGE_V;
   float _noiseFloorAmps = DEFAULT_NOISE_FLOOR_AMPS;

   esp_timer_handle_t _sampleTimer = nullptr;
   double _sliceSumVolts = 0.0;
   double _sliceSumVoltsSquared = 0.0;
   uint8_t _sliceSampleCount = 0;

   uint32_t _samplesPerSec = 0;
   uint32_t _sampleCounter = 0;
   uint32_t _lastRateMillis = 0;

   ///
   /// <summary>
   /// Static esp_timer callback bridge that forwards each tick to the owning CTPowerSensor instance.
   /// </summary>
   /// <param name="arg">Pointer to the CTPowerSensor instance.</param>
   ///
   static void _onSampleTimerStatic(void* arg)
   {
      static_cast<CTPowerSensor*>(arg)->_onSampleTick();
   }

   ///
   /// <summary>
   /// Counts how many enabled channels are currently detected as connected.
   /// </summary>
   /// <returns>Number of connected channels (at least 1 for window sizing).</returns>
   ///
   size_t _connectedChannelCount() const
   {
      size_t connectedCount = 0;
      for (size_t i = 0; i < _numChannels; i++)
      {
         if (_channels[i].connected)
         {
            connectedCount++;
         }
      }
      return connectedCount > 0 ? connectedCount : 1;
   }

   ///
   /// <summary>
   /// Returns the number of 50 ms slices retained per channel to span a ~200 ms window
   /// based on the number of actively connected channels.
   /// </summary>
   /// <returns>Number of slices per channel in a window.</returns>
   ///
   size_t _slicesPerChannelWindow() const
   {
      if (_numChannels <= 1)
      {
         return 4;
      }

      return 1;
   }

   ///
   /// <summary>
   /// Latches the latest per-channel current and connection states simultaneously at the end
   /// of each complete pass of the connected channels so display and telemetry reads see all
   /// channels update in lockstep on the same frame.
   /// </summary>
   ///
   void _latchPassSnapshot()
   {
      for (size_t i = 0; i < _numChannels; i++)
      {
         ChannelState& ch = _channels[i];
         ch.latchedConnected = ch.connected;
         if (!ch.connected || ch.traceCount == 0)
         {
            ch.latchedAmps = 0.0f;
            continue;
         }

         float sum = 0.0f;
         for (size_t j = 0; j < ch.traceCount; j++)
         {
            sum += ch.traceAmps[j];
         }
         float avg = sum / ch.traceCount;
         ch.latchedAmps = avg < _noiseFloorAmps ? 0.0f : avg;
      }
   }

   ///
   /// <summary>
   /// Drives the 4-bit CD74HC4067 address lines (S0..S3) to select the specified external
   /// multiplexer channel (0 to 15).
   /// </summary>
   /// <param name="muxChannel">External multiplexer channel index (0 to 15).</param>
   ///
   void _applyExternalMuxChannel(uint8_t muxChannel)
   {
      for (size_t bit = 0; bit < _muxSelectPins.size(); bit++)
      {
         digitalWrite(_muxSelectPins[bit], ((muxChannel >> bit) & 0x01) ? HIGH : LOW);
      }
   }

   ///
   /// <summary>
   /// Switches the active multiplexer channel and sets the settling sample counter.
   /// </summary>
   /// <param name="nextChannel">Next logical channel index to select.</param>
   /// <param name="probeMode">True if the next channel is an unconnected DC-offset probe.</param>
   ///
   void _selectChannel(size_t nextChannel, bool probeMode)
   {
      _activeChannel = nextChannel;
      _probingUnconnected = probeMode;
      if (_useExternalMux)
      {
         _applyExternalMuxChannel(_channels[_activeChannel].externalMuxChannel);
         _SettleSamplesRemaining = 1;
      }
      else
      {
         _adc.setMux(_channels[_activeChannel].mux);
         _SettleSamplesRemaining = 2;
      }
   }

   ///
   /// <summary>
   /// Advances to the next channel to sample. Connected channels are sampled in a tight round-robin
   /// loop with full 50 ms RMS slices; after a full pass of connected channels, all channels are
   /// latched together and one unconnected channel is probed for just 2 samples (~3.5 ms total)
   /// to check if a CT was newly plugged in.
   /// </summary>
   ///
   void _advanceChannel()
   {
      if (_numChannels <= 1)
      {
         _latchPassSnapshot();
         _probingUnconnected = false;
         return;
      }

      // If we just finished a quick probe on an unconnected channel (and it didn't just become
      // connected), return to the first connected channel for the next pass.
      if (_probingUnconnected)
      {
         for (size_t i = 0; i < _numChannels; i++)
         {
            if (_channels[i].connected)
            {
               _selectChannel(i, false);
               return;
            }
         }
      }
      else
      {
         // Look for the next connected channel in the current pass.
         for (size_t next = _activeChannel + 1; next < _numChannels; next++)
         {
            if (_channels[next].connected)
            {
               _selectChannel(next, false);
               return;
            }
         }

         // Completed a full pass of all connected channels: latch their readings simultaneously!
         _latchPassSnapshot();
      }

      // End of connected pass (or no channels connected yet): probe the next unconnected channel.
      for (size_t step = 0; step < _numChannels; step++)
      {
         size_t candidate = (_probeCursor + step) % _numChannels;
         if (!_channels[candidate].connected)
         {
            _probeCursor = (candidate + 1) % _numChannels;
            _selectChannel(candidate, true);
            return;
         }
      }

      // All channels are connected: wrap around to channel 0.
      _selectChannel(0, false);
   }

   ///
   /// <summary>
   /// Reads one differential voltage sample from the ADS1115 and, every SAMPLES_PER_RMS_SLICE
   /// samples (50 ms) for connected channels (or SAMPLES_PER_PROBE samples for unconnected
   /// channels), updates channel state and advances the multiplexer.
   /// </summary>
   ///
   void _onSampleTick()
   {
      float volts = 0.0f;
      if (!_adc.readVolts(&volts))
      {
         return;
      }

      _sampleCounter++;
      uint32_t nowMs = millis();
      if ((nowMs - _lastRateMillis) >= 1000)
      {
         _samplesPerSec = _sampleCounter;
         _sampleCounter = 0;
         _lastRateMillis = nowMs;
      }

      // After switching the ADS1115 mux in continuous mode, discard the in-flight conversion
      // from the previous channel so the new channel's slice only contains settled samples.
      if (_SettleSamplesRemaining > 0)
      {
         _SettleSamplesRemaining--;
         return;
      }

      _sliceSumVolts += volts;
      _sliceSumVoltsSquared += static_cast<double>(volts) * volts;
      _sliceSampleCount++;

      if (_probingUnconnected && _sliceSampleCount >= SAMPLES_PER_PROBE)
      {
         ChannelState& ch = _channels[_activeChannel];
         float dcOffsetVolts = static_cast<float>(_sliceSumVolts / _sliceSampleCount);
         bool isPluggedIn = std::fabs(dcOffsetVolts) < UNPLUGGED_DC_OFFSET_THRESHOLD_V;

         if (isPluggedIn)
         {
            // Newly plugged-in channel detected! Upgrade this probe directly into a full 50 ms RMS slice.
            ch.connected = true;
            _probingUnconnected = false;
            return;
         }

         ch.dcVolts = dcOffsetVolts;
         ch.connected = false;
         ch.rmsVolts = 0.0f;
         ch.rawAmps = 0.0f;
         ch.traceCount = 0;
         ch.traceWriteIndex = 0;

         _sliceSumVolts = 0.0;
         _sliceSumVoltsSquared = 0.0;
         _sliceSampleCount = 0;
         _advanceChannel();
         return;
      }

      if (_sliceSampleCount >= SAMPLES_PER_RMS_SLICE)
      {
         ChannelState& ch = _channels[_activeChannel];
         size_t windowSlices = _slicesPerChannelWindow();

         double meanVolts = _sliceSumVolts / _sliceSampleCount;
         double meanSquareVolts = _sliceSumVoltsSquared / _sliceSampleCount;
         double varianceVolts = meanSquareVolts - (meanVolts * meanVolts);

         float dcOffsetVolts = static_cast<float>(meanVolts);
         bool isPluggedIn = std::fabs(dcOffsetVolts) < UNPLUGGED_DC_OFFSET_THRESHOLD_V;

         float sliceRmsVolts = (isPluggedIn && varianceVolts > 0.0) ? static_cast<float>(std::sqrt(varianceVolts)) : 0.0f;
         float sliceAmps = sliceRmsVolts * ch.ampsPerVolt;
         if (sliceAmps < _noiseFloorAmps)
         {
            sliceAmps = 0.0f;
         }

         ch.dcVolts = dcOffsetVolts;
         ch.connected = isPluggedIn;
         ch.rmsVolts = sliceRmsVolts;
         ch.rawAmps = sliceAmps;

         if (isPluggedIn)
         {
            if (ch.traceCount > windowSlices)
            {
               ch.traceCount = windowSlices;
            }
            ch.traceWriteIndex = ch.traceWriteIndex % windowSlices;
            ch.traceAmps[ch.traceWriteIndex] = sliceAmps;
            ch.traceWriteIndex = (ch.traceWriteIndex + 1) % windowSlices;
            if (ch.traceCount < windowSlices)
            {
               ch.traceCount++;
            }
         }
         else
         {
            ch.traceCount = 0;
            ch.traceWriteIndex = 0;
         }

         _sliceSumVolts = 0.0;
         _sliceSumVoltsSquared = 0.0;
         _sliceSampleCount = 0;

         _advanceChannel();
      }
   }

public:
   ///
   /// <summary>
   /// Creates a single-channel split-core CT power sensor for the specified CT current rating
   /// (e.g. CTPowerSensor::CORE_30A or CTPowerSensor::CORE_100A) and AC line voltage.
   /// Defaults to DIFF_A0_A3 so AIN3 can serve as the common 1.65V reference across 1 to 3 CTs.
   /// </summary>
   /// <param name="ampsPerVolt">Rated AC amps corresponding to 1.0V RMS sensor output (e.g. 30.0f or 100.0f).</param>
   /// <param name="lineVoltageV">Nominal AC line voltage in volts (default 120.0f).</param>
   /// <param name="i2cAddress">ADS1115 I2C address (default 0x48).</param>
   /// <param name="mux">ADS1115 differential input channel pair (default AIN0 - AIN3).</param>
   ///
   explicit CTPowerSensor(
      float ampsPerVolt = CORE_30A,
      float lineVoltageV = DEFAULT_LINE_VOLTAGE_V,
      uint8_t i2cAddress = ADS1115::ADDRESS_GND,
      ADS1115::Mux mux = ADS1115::Mux::DIFF_A0_A3)
      : _adc(i2cAddress, mux, ADS1115::Gain::FS_2_048V, ADS1115::DataRate::SPS_860)
   {
      _numChannels = 1;
      _channels[0].name = "1";
      _channels[0].ampsPerVolt = ampsPerVolt;
      _channels[0].mux = mux;
      _lineVoltageV = lineVoltageV;
   }

   ///
   /// <summary>
   /// Creates a multi-channel (1 to 3 CTs) split-core power sensor on a single ADS1115 using
   /// AIN3 as the shared 1.65V mid-supply differential reference (AIN0-AIN3, AIN1-AIN3, AIN2-AIN3).
   /// </summary>
   /// <param name="channelConfigs">Array of per-channel configurations.</param>
   /// <param name="numChannels">Number of enabled channels (1 to 3).</param>
   /// <param name="lineVoltageV">Nominal AC line voltage in volts (default 120.0f).</param>
   /// <param name="i2cAddress">ADS1115 I2C address (default 0x48).</param>
   ///
   CTPowerSensor(
      const ChannelConfig* channelConfigs,
      size_t numChannels,
      float lineVoltageV = DEFAULT_LINE_VOLTAGE_V,
      uint8_t i2cAddress = ADS1115::ADDRESS_GND)
      : _adc(
           i2cAddress,
           (channelConfigs != nullptr && numChannels > 0) ? channelConfigs[0].mux : ADS1115::Mux::DIFF_A0_A3,
           ADS1115::Gain::FS_2_048V,
           ADS1115::DataRate::SPS_860)
   {
      ASSERT(channelConfigs != nullptr);
      ASSERT(numChannels >= 1 && numChannels <= MAX_CHANNELS);

      _numChannels = numChannels;
      _lineVoltageV = lineVoltageV;

      for (size_t i = 0; i < _numChannels; i++)
      {
         _channels[i].name = channelConfigs[i].name != nullptr ? channelConfigs[i].name : "";
         _channels[i].ampsPerVolt = channelConfigs[i].ampsPerVolt;
         _channels[i].mux = channelConfigs[i].mux;
         _channels[i].externalMuxChannel = channelConfigs[i].externalMuxChannel;
      }
   }

   ///
   /// <summary>
   /// Creates a multi-channel (1 to 16 CTs) split-core power sensor using an external 16-channel
   /// analog multiplexer (e.g. CD74HC4067) routed to ADS1115 AIN0 with AIN3 as the shared 1.65V reference.
   /// </summary>
   /// <param name="muxSelectPins">4 GPIO pins connected to the multiplexer S0, S1, S2, S3 address lines.</param>
   /// <param name="channelConfigs">Array of per-channel configurations.</param>
   /// <param name="numChannels">Number of enabled channels (1 to 16).</param>
   /// <param name="lineVoltageV">Nominal AC line voltage in volts (default 120.0f).</param>
   /// <param name="i2cAddress">ADS1115 I2C address (default 0x48).</param>
   /// <param name="adcMux">ADS1115 differential input channel pair (default AIN0 - AIN3).</param>
   ///
   CTPowerSensor(
      const std::array<uint8_t, 4>& muxSelectPins,
      const ChannelConfig* channelConfigs,
      size_t numChannels,
      float lineVoltageV = DEFAULT_LINE_VOLTAGE_V,
      uint8_t i2cAddress = ADS1115::ADDRESS_GND,
      ADS1115::Mux adcMux = ADS1115::Mux::DIFF_A0_A3)
      : _adc(
           i2cAddress,
           adcMux,
           ADS1115::Gain::FS_2_048V,
           ADS1115::DataRate::SPS_860),
        _useExternalMux(true),
        _muxSelectPins(muxSelectPins)
   {
      ASSERT(channelConfigs != nullptr);
      ASSERT(numChannels >= 1 && numChannels <= MAX_MUX_CHANNELS);

      _numChannels = numChannels;
      _lineVoltageV = lineVoltageV;

      for (size_t i = 0; i < MAX_MUX_CHANNELS; i++)
      {
         _channels[i].mux = adcMux;
         _channels[i].externalMuxChannel = static_cast<uint8_t>(i);
      }

      for (size_t i = 0; i < _numChannels; i++)
      {
         _channels[i].name = channelConfigs[i].name != nullptr ? channelConfigs[i].name : "";
         _channels[i].ampsPerVolt = channelConfigs[i].ampsPerVolt;
         _channels[i].mux = adcMux;
         _channels[i].externalMuxChannel = channelConfigs[i].externalMuxChannel;
      }
   }

   ///
   /// <summary>
   /// Initializes the underlying ADS1115 ADC (and external multiplexer address pins if used)
   /// and starts the 860 Hz background sampling timer.
   /// </summary>
   /// <returns>True if the ADS1115 initialized and the background timer started; false otherwise.</returns>
   ///
   bool begin()
   {
      if (_useExternalMux)
      {
         for (uint8_t pin : _muxSelectPins)
         {
            pinMode(pin, OUTPUT);
         }
         _applyExternalMuxChannel(_channels[0].externalMuxChannel);
      }

      if (!_adc.begin())
      {
         return false;
      }
      _adc.setMux(_channels[0].mux);

      _lastRateMillis = millis();
      esp_timer_create_args_t timerArgs = {};
      timerArgs.callback = &_onSampleTimerStatic;
      timerArgs.arg = this;
      timerArgs.name = "ct_adc_860hz";
      if (esp_timer_create(&timerArgs, &_sampleTimer) != ESP_OK)
      {
         return false;
      }

      return esp_timer_start_periodic(_sampleTimer, _adc.conversionIntervalMicros()) == ESP_OK;
   }

   ///
   /// <summary>
   /// Gets the number of enabled CT channels (1 to 16).
   /// </summary>
   /// <returns>Number of enabled channels.</returns>
   ///
   size_t channelCount() const
   {
      return _numChannels;
   }

   ///
   /// <summary>
   /// Sets the number of enabled CT channels.
   /// </summary>
   /// <param name="numChannels">Number of enabled channels (1 to MAX_MUX_CHANNELS).</param>
   ///
   void setChannelCount(size_t numChannels)
   {
      size_t maxCount = _useExternalMux ? MAX_MUX_CHANNELS : MAX_CHANNELS;
      ASSERT(numChannels >= 1 && numChannels <= maxCount);

      _numChannels = numChannels;
      if (_activeChannel >= _numChannels)
      {
         _activeChannel = 0;
      }
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
      ASSERT(channel < MAX_MUX_CHANNELS);

      return _channels[channel].name;
   }

   ///
   /// <summary>
   /// Sets the name/suffix for the specified channel index.
   /// </summary>
   /// <param name="channel">Channel index (0 to MAX_MUX_CHANNELS - 1).</param>
   /// <param name="name">Channel name/suffix string.</param>
   ///
   void setChannelName(size_t channel, const char* name)
   {
      ASSERT(channel < MAX_MUX_CHANNELS);
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
      ASSERT(channel < MAX_MUX_CHANNELS);

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
   /// Sets the ADS1115 differential input multiplexer channel for the specified logical channel.
   /// </summary>
   /// <param name="channel">Channel index (0 to MAX_MUX_CHANNELS - 1).</param>
   /// <param name="mux">ADS1115 differential mux setting.</param>
   ///
   void setChannelMux(size_t channel, ADS1115::Mux mux)
   {
      ASSERT(channel < MAX_MUX_CHANNELS);

      _channels[channel].mux = mux;
   }

   ///
   /// <summary>
   /// Sets the external 16-channel analog multiplexer index (0 to 15) for the specified logical channel.
   /// </summary>
   /// <param name="channel">Logical channel index (0 to MAX_MUX_CHANNELS - 1).</param>
   /// <param name="externalMuxChannel">Physical multiplexer input index (0 to 15).</param>
   ///
   void setExternalMuxChannel(size_t channel, uint8_t externalMuxChannel)
   {
      ASSERT(channel < MAX_MUX_CHANNELS);
      ASSERT(externalMuxChannel < MAX_MUX_CHANNELS);

      _channels[channel].externalMuxChannel = externalMuxChannel;
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
      ASSERT(channel < MAX_MUX_CHANNELS);

      return _channels[channel].ampsPerVolt;
   }

   ///
   /// <summary>
   /// Sets the nominal AC line voltage used to estimate apparent power in VA.
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
   /// synchronized pass snapshot.
   /// </summary>
   /// <param name="channel">Channel index (default 0).</param>
   /// <returns>Smoothed RMS current in Amps.</returns>
   ///
   float amps(size_t channel = 0) const
   {
      ASSERT(channel < MAX_MUX_CHANNELS);

      return _channels[channel].latchedAmps;
   }

   ///
   /// <summary>
   /// Estimates apparent power using RMS current and the configured nominal voltage.
   /// This is not a measurement of real power or power-flow direction.
   /// </summary>
   /// <param name="channel">Channel index (default 0).</param>
   /// <returns>Estimated apparent power in VA.</returns>
   ///
   float apparentPower(size_t channel = 0) const
   {
      return amps(channel) * _lineVoltageV;
   }

   ///
   /// <summary>
   /// Compatibility alias for apparentPower(); this value is VA, not watts.
   /// </summary>
   /// <param name="channel">Channel index (default 0).</param>
   /// <returns>Estimated apparent power in VA.</returns>
   ///
   float watts(size_t channel = 0) const
   {
      return apparentPower(channel);
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
   /// Sums estimated apparent power across all enabled channels using nominal voltage.
   /// </summary>
   /// <returns>Total estimated apparent power in VA.</returns>
   ///
   float totalApparentPower() const
   {
      return totalAmps() * _lineVoltageV;
   }

   ///
   /// <summary>
   /// Compatibility alias for totalApparentPower(); this value is VA, not watts.
   /// </summary>
   /// <returns>Total estimated apparent power in VA.</returns>
   ///
   float totalWatts() const
   {
      return totalApparentPower();
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
      ASSERT(channel < MAX_MUX_CHANNELS);

      return _channels[channel].rawAmps;
   }

   ///
   /// <summary>
   /// Gets the instantaneous RMS voltage at the ADS1115 inputs from the most recent 50 ms slice
   /// for the specified channel.
   /// </summary>
   /// <param name="channel">Channel index (default 0).</param>
   /// <returns>Most recent 50 ms sensor RMS voltage in volts.</returns>
   ///
   float rmsVolts(size_t channel = 0) const
   {
      ASSERT(channel < MAX_MUX_CHANNELS);

      return _channels[channel].rmsVolts;
   }

   ///
   /// <summary>
   /// Gets the mean differential DC offset voltage (AINx - AIN3) from the most recent 50 ms slice.
   /// Near 0.0 V when a CT is plugged in; near -1.65 V or +1.65 V when unplugged with a 1M pull resistor.
   /// </summary>
   /// <param name="channel">Channel index (default 0).</param>
   /// <returns>Mean DC offset voltage in volts.</returns>
   ///
   float dcVolts(size_t channel = 0) const
   {
      ASSERT(channel < MAX_MUX_CHANNELS);

      return _channels[channel].dcVolts;
   }

   ///
   /// <summary>
   /// Returns true if a CT sensor is connected on the specified channel (DC offset within threshold),
   /// or false if an open/unplugged input is detected via a pull-up or pull-down resistor.
   /// </summary>
   /// <param name="channel">Channel index (default 0).</param>
   /// <returns>True if connected; false if unplugged.</returns>
   ///
   bool isConnected(size_t channel = 0) const
   {
      ASSERT(channel < MAX_MUX_CHANNELS);

      return _channels[channel].latchedConnected;
   }

   ///
   /// <summary>
   /// Gets the measured background ADC sample rate in samples per second.
   /// </summary>
   /// <returns>Measured samples per second.</returns>
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
         text << "totalApparentPower=" << totalApparentPower() << " VA\n"
              << "totalCurrent=" << totalAmps() << " A\n";
      }

      size_t windowSlices = _slicesPerChannelWindow();
      for (size_t chIdx = 0; chIdx < _numChannels; chIdx++)
      {
         const ChannelState& ch = _channels[chIdx];
         std::string prefix = _numChannels == 1
            ? ""
            : (std::string("ch") + std::to_string(chIdx + 1) + "(" + ch.name + ").");

         float chAmps = amps(chIdx);
         float chApparentPower = apparentPower(chIdx);

         text.precision(1);
         text << prefix << "connected=" << (ch.connected ? "true" : "false") << "\n"
              << prefix << "apparentPower=" << chApparentPower << " VA\n"
              << prefix << "current=" << chAmps << " A\n"
              << prefix << "rawCurrent=" << ch.rawAmps << " A\n";
         text.precision(4);
         text << prefix << "sensorRms=" << ch.rmsVolts << " V\n"
              << prefix << "dcOffset=" << ch.dcVolts << " V\n"
              << prefix << "coreRating=" << static_cast<int>(ch.ampsPerVolt) << " A\n"
              << prefix << "trace50ms=";

         text.precision(1);
         size_t count = ch.traceCount;
         size_t startIdx = (ch.traceWriteIndex + windowSlices - count) % windowSlices;
         for (size_t i = 0; i < count; i++)
         {
            if (i > 0)
            {
               text << ",";
            }
            text << ch.traceAmps[(startIdx + i) % windowSlices];
         }

         if (chIdx + 1 < _numChannels)
         {
            text << "\n";
         }
      }

      return text.str();
   }
};
