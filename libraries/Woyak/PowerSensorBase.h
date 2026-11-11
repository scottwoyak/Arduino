#pragma once

#include <Arduino.h>
#include <array>
#include <cmath>
#include <sstream>
#include <string>

#include "CTPowerSensor.h"
#include "Util.h"

///
/// <summary>
/// Abstract base class for multi-channel AC current and apparent power sensors built from
/// split-core current transformers (CTs).
///
/// The base class owns the per-channel configuration, RMS slice latching, noise-floor handling,
/// trace averaging and reporting. Derived classes implement the hardware-specific ADC sampling
/// and call _latchSlice() once per channel every time a new RMS slice is available.
/// </summary>
///
class PowerSensorBase
{
public:
   ///
   /// <summary>
   /// Maximum number of CT channels supported by any implementation.
   /// </summary>
   ///
   static constexpr size_t MAX_CHANNELS = 16;

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

   ///
   /// <summary>
   /// Per-channel configuration.
   /// </summary>
   ///
   struct ChannelConfig
   {
      const char* name = nullptr;
      float ampsPerVolt = CORE_100A;
   };

protected:
   static constexpr size_t NUM_TRACE_SLICES = 4;

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
      bool connected = false;
      bool latchedConnected = false;
   };

   size_t _numChannels = MAX_CHANNELS;
   std::array<ChannelState, MAX_CHANNELS> _channels = {};
   float _lineVoltageV = DEFAULT_LINE_VOLTAGE_V;
   float _noiseFloorAmps = DEFAULT_NOISE_FLOOR_AMPS;

   ///
   /// <summary>
   /// Returns the number of slices retained per channel for trace smoothing.
   /// </summary>
   /// <returns>Number of slices per channel in a window.</returns>
   ///
   size_t _slicesPerChannelWindow() const
   {
      return NUM_TRACE_SLICES;
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
   /// Computes the RMS (AC component) and mean of accumulated samples, then records the slice
   /// into the channel's trace buffer (and optionally latches immediately). Clears the channel
   /// accumulators.
   /// </summary>
   /// <param name="channel">Channel index.</param>
   /// <param name="numSamples">Number of samples accumulated in sumV/sumV2.</param>
   /// <param name="isConnectedFn">Receives the mean volts; returns true if the channel looks connected.</param>
   /// <param name="latchImmediately">True to update latchedAmps/latchedConnected immediately; false to defer to _latchPassSnapshot().</param>
   ///
   template <typename ConnectedFn>
   void _finishSlice(size_t channel, uint32_t numSamples, ConnectedFn isConnectedFn, bool latchImmediately = true)
   {
      ChannelState& ch = _channels[channel];
      if (numSamples == 0)
      {
         return;
      }

      double meanV = ch.sumV / numSamples;
      double varianceV = (ch.sumV2 / numSamples) - (meanV * meanV);
      if (varianceV < 0.0)
      {
         varianceV = 0.0;
      }

      ch.sumV = 0.0;
      ch.sumV2 = 0.0;

      _recordSlice(
         channel,
         static_cast<float>(meanV),
         static_cast<float>(std::sqrt(varianceV)),
         isConnectedFn(static_cast<float>(meanV)),
         latchImmediately);
   }

   ///
   /// <summary>
   /// Records one RMS slice for a channel: applies the CT rating and noise floor, updates the
   /// trace buffer and connection state, and optionally latches the smoothed current immediately.
   /// </summary>
   /// <param name="channel">Channel index.</param>
   /// <param name="meanVolts">Mean (DC) voltage of the slice.</param>
   /// <param name="rmsVolts">RMS voltage of the AC component of the slice.</param>
   /// <param name="connected">True if a CT is detected on the channel.</param>
   /// <param name="latchImmediately">True to update latchedAmps/latchedConnected immediately.</param>
   ///
   void _recordSlice(size_t channel, float meanVolts, float rmsVolts, bool connected, bool latchImmediately = true)
   {
      ChannelState& ch = _channels[channel];
      size_t windowSlices = _slicesPerChannelWindow();

      float sliceAmps = connected ? (rmsVolts * ch.ampsPerVolt) : 0.0f;
      if (sliceAmps < _noiseFloorAmps)
      {
         sliceAmps = 0.0f;
      }

      ch.dcVolts = meanVolts;
      ch.connected = connected;
      ch.rmsVolts = connected ? rmsVolts : 0.0f;
      ch.rawAmps = sliceAmps;

      if (connected)
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

      if (latchImmediately)
      {
         if (!connected || ch.traceCount == 0)
         {
            ch.latchedAmps = 0.0f;
            ch.latchedConnected = false;
            return;
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
   }

   ///
   /// <summary>
   /// Latches one RMS slice for a channel immediately.
   /// </summary>
   /// <param name="channel">Channel index.</param>
   /// <param name="meanVolts">Mean (DC) voltage of the slice.</param>
   /// <param name="rmsVolts">RMS voltage of the AC component of the slice.</param>
   /// <param name="connected">True if a CT is detected on the channel.</param>
   ///
   void _latchSlice(size_t channel, float meanVolts, float rmsVolts, bool connected)
   {
      _recordSlice(channel, meanVolts, rmsVolts, connected, true);
   }

public:
   virtual ~PowerSensorBase() {}

   ///
   /// <summary>
   /// Initializes the hardware and starts background sampling.
   /// </summary>
   /// <returns>True if the hardware initialized and sampling started.</returns>
   ///
   virtual bool begin() = 0;

   ///
   /// <summary>
   /// Gets the measured sample rate in samples per second (per channel for simultaneous
   /// ADCs, total for multiplexed ADCs).
   /// </summary>
   /// <returns>Measured samples per second.</returns>
   ///
   virtual uint32_t sampleRate() const = 0;

   ///
   /// <summary>
   /// Gets the number of enabled CT channels.
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
   /// Gets the configured name/suffix for the specified channel.
   /// </summary>
   /// <param name="channel">Channel index.</param>
   /// <returns>Channel name string.</returns>
   ///
   const char* channelName(size_t channel) const
   {
      ASSERT(channel < MAX_CHANNELS);

      return _channels[channel].name;
   }

   ///
   /// <summary>
   /// Sets the name/suffix for the specified channel.
   /// </summary>
   /// <param name="channel">Channel index.</param>
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
   /// Sets the split-core CT rating in Amps per 1.0V RMS for a channel. Buffered readings
   /// are rescaled immediately if the rating changes.
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
   /// Gets the configured CT rating in Amps per 1.0V RMS for a channel.
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
   /// Sets the nominal AC line voltage used to compute apparent power.
   /// </summary>
   /// <param name="lineVoltageV">AC line voltage in volts.</param>
   ///
   void setLineVoltage(float lineVoltageV)
   {
      _lineVoltageV = lineVoltageV;
   }

   ///
   /// <summary>
   /// Gets the configured AC line voltage.
   /// </summary>
   /// <returns>AC line voltage in volts.</returns>
   ///
   float lineVoltage() const
   {
      return _lineVoltageV;
   }

   ///
   /// <summary>
   /// Sets the minimum RMS current below which readings are clamped to 0.0 A.
   /// </summary>
   /// <param name="noiseFloorAmps">Noise floor in Amps.</param>
   ///
   void setNoiseFloorAmps(float noiseFloorAmps)
   {
      _noiseFloorAmps = noiseFloorAmps;
   }

   ///
   /// <summary>
   /// Gets the smoothed RMS current for a channel.
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
   /// Gets the smoothed apparent power for a channel.
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
   /// Gets the total smoothed RMS current across all enabled channels.
   /// </summary>
   /// <returns>Total current in Amps.</returns>
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
   /// Gets the total smoothed apparent power across all enabled channels.
   /// </summary>
   /// <returns>Total apparent power in Watts.</returns>
   ///
   float totalWatts() const
   {
      return totalAmps() * _lineVoltageV;
   }

   ///
   /// <summary>
   /// Gets the RMS current from the most recent slice for a channel.
   /// </summary>
   /// <param name="channel">Channel index (default 0).</param>
   /// <returns>Most recent slice RMS current in Amps.</returns>
   ///
   float rawAmps(size_t channel = 0) const
   {
      ASSERT(channel < MAX_CHANNELS);

      return _channels[channel].rawAmps;
   }

   ///
   /// <summary>
   /// Gets the sensor RMS voltage from the most recent slice for a channel.
   /// </summary>
   /// <param name="channel">Channel index (default 0).</param>
   /// <returns>Sensor RMS voltage in volts.</returns>
   ///
   float rmsVolts(size_t channel = 0) const
   {
      ASSERT(channel < MAX_CHANNELS);

      return _channels[channel].rmsVolts;
   }

   ///
   /// <summary>
   /// Gets the mean DC voltage from the most recent slice for a channel.
   /// </summary>
   /// <param name="channel">Channel index (default 0).</param>
   /// <returns>Mean DC voltage in volts.</returns>
   ///
   float dcVolts(size_t channel = 0) const
   {
      ASSERT(channel < MAX_CHANNELS);

      return _channels[channel].dcVolts;
   }

   ///
   /// <summary>
   /// Returns true if a CT sensor is detected on the specified channel.
   /// </summary>
   /// <param name="channel">Channel index (default 0).</param>
   /// <returns>True if connected.</returns>
   ///
   bool isConnected(size_t channel = 0) const
   {
      ASSERT(channel < MAX_CHANNELS);

      return _channels[channel].latchedConnected;
   }

   ///
   /// <summary>
   /// Formats the latest readings for the "GetReadings" command.
   /// </summary>
   /// <returns>Formatted multi-line readings string.</returns>
   ///
   std::string formatReadings() const
   {
      std::ostringstream text;
      text << std::fixed;
      text << "channels=" << _numChannels << "\n"
           << "sampleRate=" << sampleRate() << "/s\n";

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
         size_t windowSlices = _slicesPerChannelWindow();
         size_t count = ch.traceCount <= windowSlices ? ch.traceCount : windowSlices;
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
