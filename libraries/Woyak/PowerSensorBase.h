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
/// Abstract base class for multi-channel AC current and power sensors built from split-core
/// current transformers (CTs), optionally paired with a live AC line voltage sensor for true
/// real power (Watts) measurement.
///
/// The base class owns the per-channel configuration, RMS and real-power slice latching,
/// noise-floor handling, trace averaging, and reporting. Derived classes implement the
/// hardware-specific ADC sampling and record slices per channel as new measurements complete.
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
   static constexpr float CORE_40A = CTPowerSensor::CORE_40A;
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
      std::array<float, NUM_TRACE_SLICES> traceWatts = {};
      size_t traceWriteIndex = 0;
      size_t traceCount = 0;
      float dcVolts = 0.0f;
      float rmsVolts = 0.0f;
      float rawAmps = 0.0f;
      float latchedAmps = 0.0f;
      float rawSignedWatts = 0.0f;
      float latchedWatts = 0.0f;
      bool hasRealPower = false;
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
   /// Latches the latest per-channel current, real power, and connection states simultaneously
   /// at the end of each complete pass of the connected channels so display and telemetry reads
   /// see all channels update in lockstep on the same frame.
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
            ch.latchedWatts = 0.0f;
            continue;
         }

         float sumAmps = 0.0f;
         float sumWatts = 0.0f;
         for (size_t j = 0; j < ch.traceCount; j++)
         {
            sumAmps += ch.traceAmps[j];
            sumWatts += ch.traceWatts[j];
         }

         float avgAmps = sumAmps / ch.traceCount;
         if (avgAmps < _noiseFloorAmps)
         {
            ch.latchedAmps = 0.0f;
            ch.latchedWatts = 0.0f;
         }
         else
         {
            ch.latchedAmps = avgAmps;
            ch.latchedWatts = ch.hasRealPower ? (sumWatts / ch.traceCount) : (avgAmps * _lineVoltageV);
         }
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
   /// Records one RMS (and optional real-power) slice for a channel: applies the CT rating and
   /// noise floor, updates the trace buffers and connection state, and optionally latches the
   /// smoothed current and power immediately.
   /// </summary>
   /// <param name="channel">Channel index.</param>
   /// <param name="meanVolts">Mean (DC) voltage of the slice.</param>
   /// <param name="rmsVolts">RMS voltage of the AC component of the slice.</param>
   /// <param name="connected">True if a CT is detected on the channel.</param>
   /// <param name="latchImmediately">True to update latchedAmps/latchedWatts/latchedConnected immediately.</param>
   /// <param name="hasRealPower">True if signedRealWatts contains a synchronous V*I real-power measurement.</param>
   /// <param name="signedRealWatts">Signed real power in Watts for this slice when hasRealPower is true.</param>
   ///
   void _recordSlice(
      size_t channel,
      float meanVolts,
      float rmsVolts,
      bool connected,
      bool latchImmediately = true,
      bool hasRealPower = false,
      float signedRealWatts = 0.0f)
   {
      ChannelState& ch = _channels[channel];
      size_t windowSlices = _slicesPerChannelWindow();

      float sliceAmps = connected ? (rmsVolts * ch.ampsPerVolt) : 0.0f;
      if (sliceAmps < _noiseFloorAmps)
      {
         sliceAmps = 0.0f;
      }

      float cleanSignedWatts = (connected && hasRealPower && sliceAmps > 0.0f) ? signedRealWatts : 0.0f;
      float sliceWatts = std::fabs(cleanSignedWatts);

      ch.dcVolts = meanVolts;
      ch.connected = connected;
      ch.rmsVolts = connected ? rmsVolts : 0.0f;
      ch.rawAmps = sliceAmps;
      ch.hasRealPower = hasRealPower;
      ch.rawSignedWatts = cleanSignedWatts;

      if (connected)
      {
         if (ch.traceCount > windowSlices)
         {
            ch.traceCount = windowSlices;
         }
         ch.traceWriteIndex = ch.traceWriteIndex % windowSlices;
         ch.traceAmps[ch.traceWriteIndex] = sliceAmps;
         ch.traceWatts[ch.traceWriteIndex] = sliceWatts;
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
            ch.latchedWatts = 0.0f;
            ch.latchedConnected = false;
            return;
         }

         float sumAmps = 0.0f;
         float sumWatts = 0.0f;
         for (size_t j = 0; j < ch.traceCount; j++)
         {
            sumAmps += ch.traceAmps[j];
            sumWatts += ch.traceWatts[j];
         }

         float avgAmps = sumAmps / ch.traceCount;
         if (avgAmps < _noiseFloorAmps)
         {
            ch.latchedAmps = 0.0f;
            ch.latchedWatts = 0.0f;
         }
         else
         {
            ch.latchedAmps = avgAmps;
            ch.latchedWatts = hasRealPower ? (sumWatts / ch.traceCount) : (avgAmps * _lineVoltageV);
         }
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
   /// Returns true if this power sensor includes a live AC voltage channel for real-power
   /// measurement rather than relying solely on a nominal voltage constant.
   /// </summary>
   /// <returns>True if hardware AC voltage sensing is supported.</returns>
   ///
   virtual bool hasVoltageSensor() const { return false; }

   ///
   /// <summary>
   /// Returns true if the live AC voltage sensor is currently connected and detecting a valid
   /// mains AC waveform.
   /// </summary>
   /// <returns>True if live AC voltage is valid.</returns>
   ///
   virtual bool voltageConnected() const { return false; }

   ///
   /// <summary>
   /// Gets the raw AC RMS signal voltage (in ADC volts) measured at the voltage sensor input.
   /// </summary>
   /// <returns>AC RMS voltage at the ADC pin in volts.</returns>
   ///
   virtual float voltageSensorRmsVolts() const { return 0.0f; }

   ///
   /// <summary>
   /// Gets the mean DC bias voltage (in ADC volts) measured at the voltage sensor input.
   /// </summary>
   /// <returns>DC bias voltage at the ADC pin in volts.</returns>
   ///
   virtual float voltageSensorDcVolts() const { return 0.0f; }

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
            ch.traceWatts[i] *= scale;
         }
         ch.rawAmps *= scale;
         ch.latchedAmps *= scale;
         ch.rawSignedWatts *= scale;
         ch.latchedWatts *= scale;
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
   /// Sets the nominal AC line voltage used when live AC voltage sensing is not active.
   /// </summary>
   /// <param name="lineVoltageV">AC line voltage in volts.</param>
   ///
   void setLineVoltage(float lineVoltageV)
   {
      _lineVoltageV = lineVoltageV;
   }

   ///
   /// <summary>
   /// Gets the active AC line voltage (live measured RMS line voltage when a voltage sensor is
   /// connected, otherwise the configured nominal voltage).
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
   /// Computes apparent power (VA) using RMS current and the active line voltage.
   /// </summary>
   /// <param name="channel">Channel index (default 0).</param>
   /// <returns>Apparent power in VA.</returns>
   ///
   float apparentPower(size_t channel = 0) const
   {
      return amps(channel) * _lineVoltageV;
   }

   ///
   /// <summary>
   /// Gets the smoothed real power magnitude in Watts when synchronous V*I sensing is active,
   /// or falls back to apparentPower() on current-only sensor implementations.
   /// </summary>
   /// <param name="channel">Channel index (default 0).</param>
   /// <returns>Power in Watts (or VA fallback when no voltage sensor is present).</returns>
   ///
   float realPower(size_t channel = 0) const
   {
      ASSERT(channel < MAX_CHANNELS);

      return _channels[channel].hasRealPower ? _channels[channel].latchedWatts : apparentPower(channel);
   }

   ///
   /// <summary>
   /// Gets the smoothed power in Watts for a channel (true real power when a voltage sensor is
   /// active, or apparent power otherwise).
   /// </summary>
   /// <param name="channel">Channel index (default 0).</param>
   /// <returns>Channel power in Watts.</returns>
   ///
   float watts(size_t channel = 0) const
   {
      return realPower(channel);
   }

   ///
   /// <summary>
   /// Gets the most recent slice's signed real power in Watts (preserving CT orientation /
   /// split-phase L1 vs L2 sign relative to the AC voltage reference).
   /// </summary>
   /// <param name="channel">Channel index (default 0).</param>
   /// <returns>Signed real power in Watts.</returns>
   ///
   float signedWatts(size_t channel = 0) const
   {
      ASSERT(channel < MAX_CHANNELS);

      return _channels[channel].rawSignedWatts;
   }

   ///
   /// <summary>
   /// Gets the power factor (0.0 to 1.0) for a channel when real power sensing is active.
   /// </summary>
   /// <param name="channel">Channel index (default 0).</param>
   /// <returns>Power factor between 0.0 and 1.0.</returns>
   ///
   float powerFactor(size_t channel = 0) const
   {
      float va = apparentPower(channel);
      if (va <= 0.0f)
      {
         return 0.0f;
      }

      float pf = realPower(channel) / va;
      return pf > 1.0f ? 1.0f : pf;
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
   /// Sums apparent power across all enabled channels.
   /// </summary>
   /// <returns>Total apparent power in VA.</returns>
   ///
   float totalApparentPower() const
   {
      return totalAmps() * _lineVoltageV;
   }

   ///
   /// <summary>
   /// Sums real power across all enabled channels.
   /// </summary>
   /// <returns>Total real power in Watts.</returns>
   ///
   float totalRealPower() const
   {
      float total = 0.0f;
      for (size_t i = 0; i < _numChannels; i++)
      {
         total += realPower(i);
      }
      return total;
   }

   ///
   /// <summary>
   /// Gets total power across all enabled channels (real power in Watts when a voltage sensor
   /// is present, otherwise apparent power in VA).
   /// </summary>
   /// <returns>Total power in Watts.</returns>
   ///
   float totalWatts() const
   {
      return hasVoltageSensor() ? totalRealPower() : totalApparentPower();
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

      if (hasVoltageSensor())
      {
         text << "voltageConnected=" << (voltageConnected() ? "true" : "false") << "\n";
         text.precision(1);
         text << "lineVoltage=" << lineVoltage() << " V\n";
         text.precision(4);
         text << "voltageSensorRms=" << voltageSensorRmsVolts() << " V\n"
              << "voltageSensorDc=" << voltageSensorDcVolts() << " V\n";
      }

      if (_numChannels > 1)
      {
         text.precision(1);
         if (hasVoltageSensor())
         {
            text << "totalPower=" << totalRealPower() << " W\n";
         }
         text << "totalApparentPower=" << totalApparentPower() << " VA\n"
              << "totalCurrent=" << totalAmps() << " A\n";
      }

      for (size_t chIdx = 0; chIdx < _numChannels; chIdx++)
      {
         const ChannelState& ch = _channels[chIdx];
         std::string prefix = _numChannels == 1
            ? ""
            : (std::string("ch") + std::to_string(chIdx + 1) + "(" + ch.name + ").");

         float chAmps = amps(chIdx);
         float chApparentPower = apparentPower(chIdx);

         text.precision(1);
         text << prefix << "connected=" << (ch.latchedConnected ? "true" : "false") << "\n";
         if (hasVoltageSensor())
         {
            text << prefix << "power=" << realPower(chIdx) << " W\n"
                 << prefix << "signedPower=" << ch.rawSignedWatts << " W\n";
         }
         text << prefix << "apparentPower=" << chApparentPower << " VA\n";
         if (hasVoltageSensor())
         {
            text.precision(2);
            text << prefix << "powerFactor=" << powerFactor(chIdx) << "\n";
            text.precision(1);
         }
         text << prefix << "current=" << chAmps << " A\n"
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
