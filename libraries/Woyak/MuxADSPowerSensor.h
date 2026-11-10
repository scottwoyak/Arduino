#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <cmath>
#include <esp_timer.h>

#include "PowerSensorBase.h"
#include "Util.h"

///
/// <summary>
/// Multi-channel AC current and apparent power sensor using a 74HC4067 16:1 analog multiplexer
/// feeding a single ADS1015 12-bit I2C ADC in differential mode (AIN0 vs AIN3).
///
/// Each CT has its own burden resistor (in the cable) and all channels share one 1.65V mid-rail
/// bias wired to both the CT return (S2) and ADS1015 AIN3, so the differential signal (AIN0-AIN3)
/// is centered at 0V. A periodic hardware timer samples at the ADS1015's 3300 SPS conversion rate:
/// connected channels are sampled for 3 complete 60 Hz cycles (50 ms) in a round-robin pass,
/// latched simultaneously at the end of the pass, and one unconnected channel is probed briefly
/// after each pass to detect newly plugged-in CTs.
///
/// The ADS1015 sits on its own dedicated I2C bus (Wire1) so it never shares the board's touch/audio bus.
/// </summary>
///
class MuxADSPowerSensor : public PowerSensorBase
{
public:
   static constexpr uint8_t DEFAULT_I2C_ADDRESS = 0x48;
   static constexpr float UNPLUGGED_DC_OFFSET_THRESHOLD_V = 0.20f;
   static constexpr float MAX_SIGNAL_PEAK_V = 1.60f;

private:
   static constexpr uint32_t I2C_FREQUENCY = 400000;
   static constexpr uint8_t REG_CONVERSION = 0x00;
   static constexpr uint8_t REG_CONFIG = 0x01;

   // OS=0, MUX=AIN0 vs AIN3 (001), PGA=+/-2.048V (010), MODE=continuous (0), DR=920 SPS (011), comparator disabled (11)
   static constexpr uint16_t CONFIG_VALUE = 0x1463;
   static constexpr float VOLTS_PER_COUNT = 2.048f / 2048.0f;

   static constexpr uint32_t SAMPLE_INTERVAL_MICROS = 1087; // 920 Hz (46 samples per 50 ms)
   static constexpr uint8_t SAMPLES_PER_RMS_SLICE = 46;     // 46 x 1.087 ms = 50 ms (3 full AC cycles at 60 Hz)
   static constexpr uint8_t SAMPLES_PER_PROBE = 3;          // 3 settled samples (~3.3 ms) to check DC offset on unplugged channels
   static constexpr uint8_t MUX_SETTLE_SAMPLES = 4;         // Discard 4 samples (~4.3 ms) after switching the external mux
   static constexpr uint8_t PROBE_PASS_INTERVAL = 4;        // Probe one unconnected channel every 4th pass (~200 ms)

   std::array<uint8_t, 4> _pinSelect = {};
   uint8_t _i2cAddress = DEFAULT_I2C_ADDRESS;
   uint8_t _pinSda = 0;
   uint8_t _pinScl = 0;

   esp_timer_handle_t _sampleTimer = nullptr;
   size_t _activeChannel = 0;
   size_t _probeCursor = 0;
   uint8_t _passesSinceProbe = 0;
   bool _probingUnconnected = false;
   uint8_t _settleSamplesRemaining = 0;
   uint8_t _sliceSampleCount = 0;

   uint32_t _sampleCounter = 0;
   uint32_t _lastRateMillis = 0;
   volatile uint32_t _samplesPerSec = 0;

   ///
   /// <summary>
   /// Drives the mux select lines for the given channel.
   /// </summary>
   /// <param name="channel">Mux channel (0 to 15).</param>
   ///
   void _applyMuxChannel(size_t channel) const
   {
      for (size_t bit = 0; bit < _pinSelect.size(); bit++)
      {
         digitalWrite(_pinSelect[bit], (channel >> bit) & 1);
      }
   }

   ///
   /// <summary>
   /// Switches the active multiplexer channel and sets the settling sample counter if the
   /// physical channel actually changed.
   /// </summary>
   /// <param name="nextChannel">Next channel index to select.</param>
   /// <param name="probeMode">True if the next channel is an unconnected DC-offset probe.</param>
   ///
   void _selectChannel(size_t nextChannel, bool probeMode)
   {
      bool changed = (nextChannel != _activeChannel);
      _activeChannel = nextChannel;
      _probingUnconnected = probeMode;
      if (changed)
      {
         _applyMuxChannel(nextChannel);
         _settleSamplesRemaining = MUX_SETTLE_SAMPLES;
      }
   }

   ///
   /// <summary>
   /// Advances to the next channel to sample. Connected channels are sampled in a round-robin
   /// loop with full 50 ms RMS slices; after a full pass of connected channels, all channels are
   /// latched together. Every PROBE_PASS_INTERVAL passes (or immediately if no channels are
   /// connected), one unconnected channel is probed to check if a CT was newly plugged in.
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

      if (!_probingUnconnected)
      {
         for (size_t next = _activeChannel + 1; next < _numChannels; next++)
         {
            if (_channels[next].connected)
            {
               _selectChannel(next, false);
               return;
            }
         }

         _latchPassSnapshot();
         _passesSinceProbe++;

         if (_passesSinceProbe < PROBE_PASS_INTERVAL)
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
         _passesSinceProbe = 0;
      }
      else
      {
         _latchPassSnapshot();
         for (size_t i = 0; i < _numChannels; i++)
         {
            if (_channels[i].connected)
            {
               _selectChannel(i, false);
               return;
            }
         }
      }

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

      _selectChannel(0, false);
   }

   ///
   /// <summary>
   /// Writes a 16-bit ADS1015 register.
   /// </summary>
   /// <param name="reg">Register pointer.</param>
   /// <param name="value">Value to write.</param>
   /// <returns>True if the device acknowledged.</returns>
   ///
   bool _writeRegister(uint8_t reg, uint16_t value) const
   {
      Wire1.beginTransmission(_i2cAddress);
      Wire1.write(reg);
      Wire1.write(static_cast<uint8_t>(value >> 8));
      Wire1.write(static_cast<uint8_t>(value & 0xFF));
      return Wire1.endTransmission() == 0;
   }

   ///
   /// <summary>
   /// Writes the ADS1015 configuration register for continuous differential conversion and leaves
   /// the register pointer aimed at the conversion register for single-transaction reads.
   /// </summary>
   /// <returns>True if the ADS1015 acknowledged both transactions.</returns>
   ///
   bool _configureAdc() const
   {
      if (!_writeRegister(REG_CONFIG, CONFIG_VALUE))
      {
         return false;
      }

      Wire1.beginTransmission(_i2cAddress);
      Wire1.write(REG_CONVERSION);
      return Wire1.endTransmission() == 0;
   }

   ///
   /// <summary>
   /// Reads the 16-bit value at the currently selected register pointer in a single
   /// I2C transaction.
   /// </summary>
   /// <param name="value">Receives the raw 16-bit value.</param>
   /// <returns>True if two bytes were read.</returns>
   ///
   bool _readCurrent(uint16_t* value) const
   {
      if (Wire1.requestFrom(_i2cAddress, static_cast<uint8_t>(2)) != 2)
      {
         return false;
      }

      uint8_t high = Wire1.read();
      uint8_t low = Wire1.read();
      *value = (static_cast<uint16_t>(high) << 8) | low;
      return true;
   }

   ///
   /// <summary>
   /// Reads one conversion and converts it to volts.
   /// </summary>
   /// <param name="volts">Receives the measured voltage.</param>
   /// <returns>True if the read succeeded.</returns>
   ///
   bool _readVolts(float* volts) const
   {
      uint16_t raw;
      if (!_readCurrent(&raw))
      {
         return false;
      }

      *volts = (static_cast<int16_t>(raw) >> 4) * VOLTS_PER_COUNT;
      return true;
   }

   ///
   /// <summary>
   /// Static esp_timer callback entry point.
   /// </summary>
   /// <param name="arg">Pointer to the MuxADSPowerSensor instance.</param>
   ///
   static void _onSampleTimerStatic(void* arg)
   {
      static_cast<MuxADSPowerSensor*>(arg)->_onSampleTick();
   }

   ///
   /// <summary>
   /// Reads one differential voltage sample from the ADS1015 and, every SAMPLES_PER_RMS_SLICE
   /// samples (50 ms) for connected channels (or SAMPLES_PER_PROBE samples for unconnected
   /// channels), updates channel state and advances the multiplexer.
   /// </summary>
   ///
   void _onSampleTick()
   {
      float volts = 0.0f;
      if (!_readVolts(&volts))
      {
         _configureAdc();
         _settleSamplesRemaining = MUX_SETTLE_SAMPLES;
         return;
      }

      _sampleCounter++;
      uint32_t nowMs = millis();
      if ((nowMs - _lastRateMillis) >= 1000)
      {
         _samplesPerSec = (_sampleCounter * 1000) / (nowMs - _lastRateMillis);
         _sampleCounter = 0;
         _lastRateMillis = nowMs;
         _configureAdc();
      }

      if (_settleSamplesRemaining > 0)
      {
         _settleSamplesRemaining--;
         return;
      }

      ChannelState& ch = _channels[_activeChannel];
      ch.sumV += volts;
      ch.sumV2 += static_cast<double>(volts) * volts;
      _sliceSampleCount++;

      if (_probingUnconnected && _sliceSampleCount >= SAMPLES_PER_PROBE)
      {
         float dcOffsetVolts = static_cast<float>(ch.sumV / _sliceSampleCount);
         bool isPluggedIn = std::fabs(dcOffsetVolts) < UNPLUGGED_DC_OFFSET_THRESHOLD_V;

         if (isPluggedIn)
         {
            ch.connected = true;
            _probingUnconnected = false;
            return;
         }

         ch.sumV = 0.0;
         ch.sumV2 = 0.0;
         _sliceSampleCount = 0;
         _recordSlice(_activeChannel, dcOffsetVolts, 0.0f, false, false);
         _advanceChannel();
         return;
      }

      if (_sliceSampleCount >= SAMPLES_PER_RMS_SLICE)
      {
         uint8_t samples = _sliceSampleCount;
         _sliceSampleCount = 0;
         _finishSlice(
            _activeChannel,
            samples,
            [](float meanVolts)
            {
               return std::fabs(meanVolts) < UNPLUGGED_DC_OFFSET_THRESHOLD_V;
            },
            false);
         _advanceChannel();
      }
   }

public:
   ///
   /// <summary>
   /// Creates a mux + ADS1015 CT power sensor.
   /// </summary>
   /// <param name="pinS0">GPIO connected to 74HC4067 S0.</param>
   /// <param name="pinS1">GPIO connected to 74HC4067 S1.</param>
   /// <param name="pinS2">GPIO connected to 74HC4067 S2.</param>
   /// <param name="pinS3">GPIO connected to 74HC4067 S3.</param>
   /// <param name="pinSda">GPIO for the dedicated ADS1015 I2C data line (Wire1).</param>
   /// <param name="pinScl">GPIO for the dedicated ADS1015 I2C clock line (Wire1).</param>
   /// <param name="channelConfigs">Array of per-channel configurations (mux C0..C7).</param>
   /// <param name="numChannels">Number of active channels (1 to 8).</param>
   /// <param name="lineVoltageV">Nominal AC line voltage in volts.</param>
   /// <param name="i2cAddress">ADS1015 I2C address (default 0x48, ADDR tied to GND).</param>
   ///
   MuxADSPowerSensor(
      uint8_t pinS0,
      uint8_t pinS1,
      uint8_t pinS2,
      uint8_t pinS3,
      uint8_t pinSda,
      uint8_t pinScl,
      const ChannelConfig* channelConfigs,
      size_t numChannels = MAX_CHANNELS,
      float lineVoltageV = DEFAULT_LINE_VOLTAGE_V,
      uint8_t i2cAddress = DEFAULT_I2C_ADDRESS)
   {
      ASSERT(channelConfigs != nullptr);
      ASSERT(numChannels >= 1 && numChannels <= MAX_CHANNELS);

      _pinSelect = { pinS0, pinS1, pinS2, pinS3 };
      _i2cAddress = i2cAddress;
      _pinSda = pinSda;
      _pinScl = pinScl;
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
   /// Stops the background sampling timer if running.
   /// </summary>
   ///
   ~MuxADSPowerSensor()
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
   /// Configures the mux select pins, verifies and configures the ADS1015, and starts the
   /// periodic 920 Hz background sampling timer.
   /// </summary>
   /// <returns>True if the ADS1015 responded and sampling started.</returns>
   ///
   bool begin() override
   {
      for (uint8_t pin : _pinSelect)
      {
         pinMode(pin, OUTPUT);
      }
      _applyMuxChannel(0);
      _activeChannel = 0;
      _probingUnconnected = false;
      _settleSamplesRemaining = MUX_SETTLE_SAMPLES;

      if (!Wire1.begin(_pinSda, _pinScl, I2C_FREQUENCY))
      {
         return false;
      }

      if (!_configureAdc())
      {
         return false;
      }

      float volts;
      if (!_readVolts(&volts))
      {
         return false;
      }

      _lastRateMillis = millis();
      if (_sampleTimer == nullptr)
      {
         esp_timer_create_args_t timerArgs = {};
         timerArgs.callback = &_onSampleTimerStatic;
         timerArgs.arg = this;
         timerArgs.dispatch_method = ESP_TIMER_TASK;
         timerArgs.name = "MuxADSPowerSensor";

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
   /// Gets the measured ADC sample rate in samples per second (total across all channels).
   /// </summary>
   /// <returns>Measured samples per second.</returns>
   ///
   uint32_t sampleRate() const override
   {
      return _samplesPerSec;
   }
};
