#pragma once

#include <Arduino.h>
#include <array>
#include <cmath>
#include <cstring>
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

   ///
   /// <summary>
   /// Maximum number of virtual split-phase channels synthesized from hot + White pairs.
   /// </summary>
   ///
   static constexpr size_t MAX_DERIVED_CHANNELS = MAX_CHANNELS / 2;
   static constexpr size_t MAX_DERIVED_NAME_LENGTH = 48;

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
   static constexpr float HOT_LEG_SIGN_LOCK_WATTS = 50.0f;
   static constexpr float DERIVED_POLARITY_MIN_WATTS = 50.0f;
   static constexpr float DERIVED_POLARITY_MIN_RATIO = 0.06f;
   static constexpr uint8_t DERIVED_POLARITY_STEADY_PASSES = 8;

   ///
   /// <summary>
   /// Split-phase conductor role parsed from channel name suffixes ("Red", "Black", "White").
   /// </summary>
   ///
   enum class SplitPhaseRole : uint8_t
   {
      NONE = 0,
      RED = 1,
      BLACK = 2,
      WHITE = 3,
   };

   ///
   /// <summary>
   /// Converts an ASCII character to lowercase.
   /// </summary>
   /// <param name="c">Character to convert.</param>
   /// <returns>Lowercase ASCII character.</returns>
   ///
   static char _toLowerAscii(char c)
   {
      if (c >= 'A' && c <= 'Z')
      {
         return static_cast<char>(c - 'A' + 'a');
      }
      return c;
   }

   ///
   /// <summary>
   /// Checks whether a string ends with the given lowercase ASCII suffix (case-insensitive).
   /// </summary>
   /// <param name="name">Channel name string.</param>
   /// <param name="nameLen">Length of name.</param>
   /// <param name="lowerSuffix">Lowercase suffix literal.</param>
   /// <param name="suffixLen">Length of lowerSuffix.</param>
   /// <returns>True if name ends with lowerSuffix (case-insensitive).</returns>
   ///
   static bool _endsWithIgnoreCase(const char* name, size_t nameLen, const char* lowerSuffix, size_t suffixLen)
   {
      ASSERT(name != nullptr);
      ASSERT(lowerSuffix != nullptr);

      if (nameLen < suffixLen)
      {
         return false;
      }

      size_t offset = nameLen - suffixLen;
      for (size_t i = 0; i < suffixLen; i++)
      {
         if (_toLowerAscii(name[offset + i]) != lowerSuffix[i])
         {
            return false;
         }
      }

      return true;
   }

   ///
   /// <summary>
   /// Parses a channel name for a trailing "Red", "Black", or "White" conductor suffix
   /// (case-insensitive) and outputs the length of the leading panel/circuit prefix.
   /// </summary>
   /// <param name="name">Channel name string.</param>
   /// <param name="outPrefixLen">Receives the prefix length preceding the suffix.</param>
   /// <returns>Detected SplitPhaseRole value.</returns>
   ///
   static SplitPhaseRole _parseSplitPhaseRole(const char* name, size_t* outPrefixLen)
   {
      ASSERT(outPrefixLen != nullptr);

      *outPrefixLen = 0;
      if (name == nullptr)
      {
         return SplitPhaseRole::NONE;
      }

      size_t len = 0;
      while (name[len] != '\0')
      {
         len++;
      }

      if (_endsWithIgnoreCase(name, len, "black", 5))
      {
         *outPrefixLen = len - 5;
         return SplitPhaseRole::BLACK;
      }
      if (_endsWithIgnoreCase(name, len, "white", 5))
      {
         *outPrefixLen = len - 5;
         return SplitPhaseRole::WHITE;
      }
      if (_endsWithIgnoreCase(name, len, "red", 3))
      {
         *outPrefixLen = len - 3;
         return SplitPhaseRole::RED;
      }

      return SplitPhaseRole::NONE;
   }

   ///
   /// <summary>
   /// Returns true if the given channel name identifies a bidirectional solar circuit
   /// (case-insensitive match for "solar").
   /// </summary>
   /// <param name="name">Channel name string.</param>
   /// <returns>True if the channel is a solar circuit.</returns>
   ///
   static bool _isSolarChannelName(const char* name)
   {
      if (name == nullptr)
      {
         return false;
      }

      constexpr const char* SOLAR_TAG = "solar";
      constexpr size_t SOLAR_TAG_LEN = 5;
      for (size_t i = 0; name[i] != '\0'; i++)
      {
         size_t matched = 0;
         while (matched < SOLAR_TAG_LEN && name[i + matched] != '\0')
         {
            if (_toLowerAscii(name[i + matched]) != SOLAR_TAG[matched])
            {
               break;
            }
            matched++;
         }
         if (matched == SOLAR_TAG_LEN)
         {
            return true;
         }
      }

      return false;
   }

   struct ChannelState
   {
      const char* name = "";
      float ampsPerVolt = CORE_100A;
      float phaseOffsetDeg = 0.0f;
      double sumV = 0.0;
      double sumV2 = 0.0;
      std::array<float, NUM_TRACE_SLICES> traceAmps = {};
      std::array<float, NUM_TRACE_SLICES> traceWatts = {};
      std::array<float, NUM_TRACE_SLICES> traceVars = {};
      size_t traceWriteIndex = 0;
      size_t traceCount = 0;
      size_t prefixLen = 0;
      float dcVolts = 0.0f;
      float rmsVolts = 0.0f;
      float rawAmps = 0.0f;
      float latchedAmps = 0.0f;
      float rawSignedWatts = 0.0f;
      float rawReactiveVars = 0.0f;
      float latchedWatts = 0.0f;
      SplitPhaseRole splitRole = SplitPhaseRole::NONE;
      int8_t polaritySign = 0;
      int8_t hotLegSign = 0;
      int8_t polarityCandidateSign = 0;
      uint8_t polarityCandidateCount = 0;
      bool isSolar = false;
      bool isDerived = false;
      bool hasRealPower = false;
      bool connected = false;
      bool latchedConnected = false;
   };

   ///
   /// <summary>
   /// Returns true if two channels share the exact same split-phase panel prefix
   /// (case-insensitive, e.g. "Panel1" for "Panel1Red" and "Panel1White").
   /// </summary>
   /// <param name="a">First channel state.</param>
   /// <param name="b">Second channel state.</param>
   /// <returns>True if prefixes match case-insensitively.</returns>
   ///
   static bool _sameSplitPhasePrefix(const ChannelState* a, const ChannelState* b)
   {
      ASSERT(a != nullptr);
      ASSERT(b != nullptr);

      if (a->prefixLen != b->prefixLen || a->name == nullptr || b->name == nullptr)
      {
         return false;
      }

      for (size_t i = 0; i < a->prefixLen; i++)
      {
         if (_toLowerAscii(a->name[i]) != _toLowerAscii(b->name[i]))
         {
            return false;
         }
      }

      return true;
   }

   ///
   /// <summary>
   /// Computes the smoothed RMS current, signed real power, and signed reactive power across
   /// the active trace slices of a connected physical channel.
   /// </summary>
   /// <param name="ch">Connected channel state.</param>
   /// <param name="outAmps">Receives smoothed RMS current in Amps.</param>
   /// <param name="outSignedWatts">Receives smoothed signed real power in Watts.</param>
   /// <param name="outSignedVars">Receives smoothed signed reactive power in VAR.</param>
   ///
   static void _computeSmoothedPhasor(
      const ChannelState* ch,
      float* outAmps,
      float* outSignedWatts,
      float* outSignedVars)
   {
      ASSERT(ch != nullptr);
      ASSERT(outAmps != nullptr);
      ASSERT(outSignedWatts != nullptr);
      ASSERT(outSignedVars != nullptr);

      if (ch->traceCount == 0)
      {
         *outAmps = 0.0f;
         *outSignedWatts = 0.0f;
         *outSignedVars = 0.0f;
         return;
      }

      float sumAmps = 0.0f;
      float sumWatts = 0.0f;
      float sumVars = 0.0f;
      for (size_t j = 0; j < ch->traceCount; j++)
      {
         sumAmps += ch->traceAmps[j];
         sumWatts += ch->traceWatts[j];
         sumVars += ch->traceVars[j];
      }

      float invCount = 1.0f / ch->traceCount;
      *outAmps = sumAmps * invCount;
      *outSignedWatts = sumWatts * invCount;
      *outSignedVars = sumVars * invCount;
   }

   struct DerivedChannelState
   {
      char name[MAX_DERIVED_NAME_LENGTH] = {};
      size_t hotChannel = 0;
      size_t whiteChannel = 0;
      float amps = 0.0f;
      float watts = 0.0f;
      float reactiveVars = 0.0f;
      bool connected = false;
   };

   ///
   /// <summary>
   /// Builds the synthesized missing hot-leg channel name (e.g. "Panel1Black" from "Panel1Red"),
   /// matching the capitalization style of the configured sibling hot leg.
   /// </summary>
   /// <param name="hotCh">Configured sibling hot-leg channel state.</param>
   /// <param name="outName">Destination buffer for the synthesized name.</param>
   /// <param name="outSize">Destination buffer capacity in bytes.</param>
   ///
   static void _buildDerivedChannelName(const ChannelState* hotCh, char* outName, size_t outSize)
   {
      ASSERT(hotCh != nullptr);
      ASSERT(outName != nullptr);
      ASSERT(outSize > 0);

      const char* src = hotCh->name != nullptr ? hotCh->name : "";
      size_t copyLen = hotCh->prefixLen;
      if (copyLen >= outSize)
      {
         copyLen = outSize - 1;
      }

      if (copyLen > 0)
      {
         std::memcpy(outName, src, copyLen);
      }

      const char* hotSuffix = src + hotCh->prefixLen;
      bool firstUpper = hotSuffix[0] >= 'A' && hotSuffix[0] <= 'Z';
      bool allUpper = firstUpper && hotSuffix[1] >= 'A' && hotSuffix[1] <= 'Z';

      const char* targetSuffix = "";
      if (hotCh->splitRole == SplitPhaseRole::RED)
      {
         targetSuffix = allUpper ? "BLACK" : (firstUpper ? "Black" : "black");
      }
      else
      {
         targetSuffix = allUpper ? "RED" : (firstUpper ? "Red" : "red");
      }

      size_t suffixLen = std::strlen(targetSuffix);
      size_t avail = (outSize - 1) - copyLen;
      if (suffixLen > avail)
      {
         suffixLen = avail;
      }
      if (suffixLen > 0)
      {
         std::memcpy(outName + copyLen, targetSuffix, suffixLen);
      }
      outName[copyLen + suffixLen] = '\0';
   }

   ///
   /// <summary>
   /// Rebuilds the list of virtual derived split-phase channels whenever channel names or
   /// active channel count change. When a "<Prefix>White" neutral channel is paired with
   /// exactly one configured hot leg ("<Prefix>Red" or "<Prefix>Black") and the opposite hot
   /// leg is not assigned to any physical channel, a virtual channel for the missing hot leg
   /// is synthesized automatically.
   /// </summary>
   ///
   void _rebuildDerivedChannels()
   {
      std::array<DerivedChannelState, MAX_DERIVED_CHANNELS> nextDerived = {};
      size_t nextCount = 0;

      for (size_t w = 0; w < _numChannels; w++)
      {
         const ChannelState& whiteCh = _channels[w];
         if (whiteCh.isSolar || whiteCh.splitRole != SplitPhaseRole::WHITE)
         {
            continue;
         }

         bool duplicateWhitePrefix = false;
         for (size_t prevW = 0; prevW < w; prevW++)
         {
            if (!_channels[prevW].isSolar &&
                _channels[prevW].splitRole == SplitPhaseRole::WHITE &&
                _sameSplitPhasePrefix(&whiteCh, &_channels[prevW]))
            {
               duplicateWhitePrefix = true;
               break;
            }
         }
         if (duplicateWhitePrefix)
         {
            continue;
         }

         int redIdx = -1;
         int blackIdx = -1;
         for (size_t h = 0; h < _numChannels; h++)
         {
            if (h == w)
            {
               continue;
            }
            const ChannelState& cand = _channels[h];
            if (cand.isSolar || !_sameSplitPhasePrefix(&whiteCh, &cand))
            {
               continue;
            }
            if (cand.splitRole == SplitPhaseRole::RED && redIdx < 0)
            {
               redIdx = static_cast<int>(h);
            }
            else if (cand.splitRole == SplitPhaseRole::BLACK && blackIdx < 0)
            {
               blackIdx = static_cast<int>(h);
            }
         }

         if ((redIdx >= 0) == (blackIdx >= 0))
         {
            continue;
         }
         if (nextCount >= MAX_DERIVED_CHANNELS)
         {
            break;
         }

         size_t hotChannel = static_cast<size_t>(redIdx >= 0 ? redIdx : blackIdx);
         DerivedChannelState& dst = nextDerived[nextCount];
         dst.hotChannel = hotChannel;
         dst.whiteChannel = w;
         _buildDerivedChannelName(&_channels[hotChannel], dst.name, sizeof(dst.name));

         for (size_t p = 0; p < _numDerivedChannels; p++)
         {
            if (_derivedChannels[p].hotChannel == hotChannel && _derivedChannels[p].whiteChannel == w)
            {
               dst.amps = _derivedChannels[p].amps;
               dst.watts = _derivedChannels[p].watts;
               dst.reactiveVars = _derivedChannels[p].reactiveVars;
               dst.connected = _derivedChannels[p].connected;
               break;
            }
         }

         nextCount++;
      }

      _numDerivedChannels = 0;
      _derivedChannels = nextDerived;
      _numDerivedChannels = nextCount;
   }

   size_t _numChannels = MAX_CHANNELS;
   std::array<ChannelState, MAX_CHANNELS> _channels = {};
   size_t _numDerivedChannels = 0;
   std::array<DerivedChannelState, MAX_DERIVED_CHANNELS> _derivedChannels = {};
   float _lineVoltageV = DEFAULT_LINE_VOLTAGE_V;
   float _noiseFloorAmps = DEFAULT_NOISE_FLOOR_AMPS;
   volatile bool _polarityDirty = false;

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
   /// Converts a channel's smoothed signed real power into its output watt reading: solar
   /// channels apply their learned polarity sign (daytime production positive, nighttime draw
   /// negative), while standard load circuits report unsigned consumption (>= 0 W).
   /// </summary>
   /// <param name="ch">Channel state.</param>
   /// <param name="avgAmps">Smoothed RMS current in Amps.</param>
   /// <param name="avgSignedWatts">Smoothed raw signed real power in Watts.</param>
   /// <returns>Output power in Watts.</returns>
   ///
   float _resolveLatchedWatts(const ChannelState& ch, float avgAmps, float avgSignedWatts) const
   {
      if (!ch.hasRealPower)
      {
         return avgAmps * _lineVoltageV;
      }

      if (ch.isSolar)
      {
         return ch.polaritySign != 0 ? (avgSignedWatts * ch.polaritySign) : std::fabs(avgSignedWatts);
      }

      return std::fabs(avgSignedWatts);
   }

   ///
   /// <summary>
   /// Computes the missing split-phase hot leg's RMS current, real power, and reactive power
   /// from its sibling hot leg and White neutral CT using KCL (i_Red + i_Black + i_White = 0),
   /// automatically learning and locking the White CT's relative winding polarity on imbalanced loads.
   /// </summary>
   /// <param name="hotCh">Connected sibling hot-leg channel state.</param>
   /// <param name="whiteCh">Connected White neutral channel state (polaritySign updated if learned).</param>
   /// <param name="outAmps">Receives derived RMS current in Amps.</param>
   /// <param name="outWatts">Receives derived real power in Watts.</param>
   /// <param name="outVars">Receives derived reactive power in VAR.</param>
   /// <returns>True if both physical CTs have active real-power readings and derivation succeeded.</returns>
   ///
   bool _deriveMissingHotLeg(
      const ChannelState* hotCh,
      ChannelState* whiteCh,
      float* outAmps,
      float* outWatts,
      float* outVars)
   {
      ASSERT(hotCh != nullptr);
      ASSERT(whiteCh != nullptr);
      ASSERT(outAmps != nullptr);
      ASSERT(outWatts != nullptr);
      ASSERT(outVars != nullptr);

      if (!hotCh->connected || hotCh->traceCount == 0 || !hotCh->hasRealPower ||
          !whiteCh->connected || whiteCh->traceCount == 0 || !whiteCh->hasRealPower)
      {
         *outAmps = 0.0f;
         *outWatts = 0.0f;
         *outVars = 0.0f;
         return false;
      }

      float avgAmpsH = 0.0f;
      float avgPH = 0.0f;
      float avgQH = 0.0f;
      _computeSmoothedPhasor(hotCh, &avgAmpsH, &avgPH, &avgQH);

      float avgAmpsW = 0.0f;
      float avgPW = 0.0f;
      float avgQW = 0.0f;
      _computeSmoothedPhasor(whiteCh, &avgAmpsW, &avgPW, &avgQW);

      int8_t sH = hotCh->hotLegSign;
      if (sH == 0 && std::fabs(avgPH) >= 5.0f)
      {
         sH = avgPH >= 0.0f ? 1 : -1;
      }

      int8_t sRel = whiteCh->polaritySign != 0 ? whiteCh->polaritySign : 1;
      if (hotCh->hotLegSign != 0)
      {
         float pPlus = sH * (avgPH + avgPW);
         float pMinus = sH * (avgPH - avgPW);
         float minImbalanceWatts = DERIVED_POLARITY_MIN_RATIO * (std::fabs(avgPH) + std::fabs(avgPW));
         int8_t observedRel = 0;
         if (pPlus <= -DERIVED_POLARITY_MIN_WATTS &&
             (-pPlus) >= minImbalanceWatts &&
             pMinus >= DERIVED_POLARITY_MIN_WATTS)
         {
            observedRel = -1;
         }
         else if (pMinus <= -DERIVED_POLARITY_MIN_WATTS &&
                  (-pMinus) >= minImbalanceWatts &&
                  pPlus >= DERIVED_POLARITY_MIN_WATTS)
         {
            observedRel = 1;
         }

         if (observedRel != 0 && whiteCh->polaritySign == 0)
         {
            sRel = observedRel;
         }

         if (observedRel != 0 && observedRel != whiteCh->polaritySign)
         {
            if (whiteCh->polarityCandidateSign == observedRel)
            {
               if (whiteCh->polarityCandidateCount < DERIVED_POLARITY_STEADY_PASSES)
               {
                  whiteCh->polarityCandidateCount++;
               }
            }
            else
            {
               whiteCh->polarityCandidateSign = observedRel;
               whiteCh->polarityCandidateCount = 1;
            }

            if (whiteCh->polarityCandidateCount >= DERIVED_POLARITY_STEADY_PASSES)
            {
               whiteCh->polaritySign = observedRel;
               whiteCh->polarityCandidateCount = 0;
               whiteCh->polarityCandidateSign = 0;
               sRel = observedRel;
               _polarityDirty = true;
            }
         }
         else
         {
            whiteCh->polarityCandidateCount = 0;
            whiteCh->polarityCandidateSign = 0;
         }
      }

      float derivedWatts = 0.0f;
      float derivedVars = 0.0f;
      if (sH != 0)
      {
         float rawPM = sH * (avgPH + sRel * avgPW);
         derivedWatts = rawPM > 0.0f ? rawPM : 0.0f;
         derivedVars = sH * (avgQH + sRel * avgQW);
      }
      else
      {
         derivedWatts = std::fabs(avgPW);
         derivedVars = avgQW;
      }

      float vaM = std::hypot(derivedWatts, derivedVars);
      float derivedAmps = _lineVoltageV > 1.0f ? (vaM / _lineVoltageV) : 0.0f;
      float minAmps = std::fabs(avgAmpsW - avgAmpsH);
      float maxAmps = avgAmpsW + avgAmpsH;
      if (derivedAmps < minAmps)
      {
         derivedAmps = minAmps;
      }
      if (derivedAmps > maxAmps)
      {
         derivedAmps = maxAmps;
      }

      if (derivedAmps < _noiseFloorAmps)
      {
         *outAmps = 0.0f;
         *outWatts = 0.0f;
         *outVars = 0.0f;
      }
      else
      {
         *outAmps = derivedAmps;
         *outWatts = derivedWatts;
         *outVars = derivedVars;
      }

      return true;
   }

   ///
   /// <summary>
   /// Latches the latest per-channel current, real power, and connection states simultaneously
   /// at the end of each complete pass of the connected channels, then derives any unclamped
   /// "<Prefix>Red" or "<Prefix>Black" split-phase hot leg (both explicitly named empty ports
   /// and auto-synthesized virtual channels) when its sibling opposite hot leg and "<Prefix>White"
   /// neutral CT are both physically connected with live AC voltage.
   /// </summary>
   ///
   void _latchPassSnapshot()
   {
      for (size_t i = 0; i < _numChannels; i++)
      {
         ChannelState& ch = _channels[i];
         ch.isDerived = false;
         ch.latchedConnected = ch.connected;
         if (!ch.connected || ch.traceCount == 0)
         {
            ch.hotLegSign = 0;
            ch.latchedAmps = 0.0f;
            ch.latchedWatts = 0.0f;
            continue;
         }

         float avgAmps = 0.0f;
         float avgSignedWatts = 0.0f;
         float avgSignedVars = 0.0f;
         _computeSmoothedPhasor(&ch, &avgAmps, &avgSignedWatts, &avgSignedVars);

         if (!ch.isSolar &&
             (ch.splitRole == SplitPhaseRole::RED || ch.splitRole == SplitPhaseRole::BLACK) &&
             std::fabs(avgSignedWatts) >= HOT_LEG_SIGN_LOCK_WATTS)
         {
            ch.hotLegSign = avgSignedWatts >= 0.0f ? 1 : -1;
         }

         if (avgAmps < _noiseFloorAmps)
         {
            ch.latchedAmps = 0.0f;
            ch.latchedWatts = 0.0f;
         }
         else
         {
            ch.latchedAmps = avgAmps;
            ch.latchedWatts = _resolveLatchedWatts(ch, avgAmps, avgSignedWatts);
         }
      }

      for (size_t i = 0; i < _numChannels; i++)
      {
         ChannelState& targetCh = _channels[i];
         if (targetCh.connected ||
             targetCh.isSolar ||
             (targetCh.splitRole != SplitPhaseRole::RED && targetCh.splitRole != SplitPhaseRole::BLACK))
         {
            continue;
         }

         SplitPhaseRole neededHotRole = targetCh.splitRole == SplitPhaseRole::RED
            ? SplitPhaseRole::BLACK
            : SplitPhaseRole::RED;
         int hotIdx = -1;
         int whiteIdx = -1;

         for (size_t j = 0; j < _numChannels; j++)
         {
            if (j == i)
            {
               continue;
            }
            const ChannelState& cand = _channels[j];
            if (!cand.connected || cand.traceCount == 0 || !cand.hasRealPower)
            {
               continue;
            }
            if (!_sameSplitPhasePrefix(&targetCh, &cand))
            {
               continue;
            }

            if (cand.splitRole == neededHotRole && !cand.isSolar && hotIdx < 0)
            {
               hotIdx = static_cast<int>(j);
            }
            else if (cand.splitRole == SplitPhaseRole::WHITE && whiteIdx < 0)
            {
               whiteIdx = static_cast<int>(j);
            }
         }

         if (hotIdx < 0 || whiteIdx < 0)
         {
            continue;
         }

         float derivedAmps = 0.0f;
         float derivedWatts = 0.0f;
         float derivedVars = 0.0f;
         if (_deriveMissingHotLeg(
                &_channels[static_cast<size_t>(hotIdx)],
                &_channels[static_cast<size_t>(whiteIdx)],
                &derivedAmps,
                &derivedWatts,
                &derivedVars))
         {
            targetCh.isDerived = true;
            targetCh.latchedConnected = true;
            targetCh.hasRealPower = true;
            targetCh.rawAmps = derivedAmps;
            targetCh.rawSignedWatts = derivedWatts;
            targetCh.rawReactiveVars = derivedVars;
            targetCh.latchedAmps = derivedAmps;
            targetCh.latchedWatts = derivedWatts;
         }
      }

      for (size_t d = 0; d < _numDerivedChannels; d++)
      {
         DerivedChannelState& derived = _derivedChannels[d];
         derived.connected = _deriveMissingHotLeg(
            &_channels[derived.hotChannel],
            &_channels[derived.whiteChannel],
            &derived.amps,
            &derived.watts,
            &derived.reactiveVars);
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
   /// <param name="signedReactiveVars">Signed reactive power in VAR for this slice when hasRealPower is true.</param>
   ///
   void _recordSlice(
      size_t channel,
      float meanVolts,
      float rmsVolts,
      bool connected,
      bool latchImmediately = true,
      bool hasRealPower = false,
      float signedRealWatts = 0.0f,
      float signedReactiveVars = 0.0f)
   {
      ChannelState& ch = _channels[channel];
      size_t windowSlices = _slicesPerChannelWindow();

      float sliceAmps = connected ? (rmsVolts * ch.ampsPerVolt) : 0.0f;
      if (sliceAmps < _noiseFloorAmps)
      {
         sliceAmps = 0.0f;
      }

      float cleanSignedWatts = (connected && hasRealPower && sliceAmps > 0.0f) ? signedRealWatts : 0.0f;
      float cleanReactiveVars = (connected && hasRealPower && sliceAmps > 0.0f) ? signedReactiveVars : 0.0f;

      ch.dcVolts = meanVolts;
      ch.connected = connected;
      ch.rmsVolts = connected ? rmsVolts : 0.0f;
      ch.rawAmps = sliceAmps;
      ch.hasRealPower = hasRealPower;
      ch.rawSignedWatts = cleanSignedWatts;
      ch.rawReactiveVars = cleanReactiveVars;

      if (connected)
      {
         if (ch.traceCount > windowSlices)
         {
            ch.traceCount = windowSlices;
         }
         ch.traceWriteIndex = ch.traceWriteIndex % windowSlices;
         ch.traceAmps[ch.traceWriteIndex] = sliceAmps;
         ch.traceWatts[ch.traceWriteIndex] = cleanSignedWatts;
         ch.traceVars[ch.traceWriteIndex] = cleanReactiveVars;
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
            ch.latchedWatts = _resolveLatchedWatts(ch, avgAmps, sumWatts / ch.traceCount);
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
   /// Returns true if this sensor applies quadrature phase compensation between AC voltage
   /// and CT current.
   /// </summary>
   /// <returns>True if phase compensation is active.</returns>
   ///
   virtual bool hasPhaseCompensation() const { return false; }

   ///
   /// <summary>
   /// Gets the active board-level AC voltage phase compensation angle in degrees.
   /// </summary>
   /// <returns>Phase compensation angle in degrees.</returns>
   ///
   virtual float phaseShiftDeg() const { return 0.0f; }

   ///
   /// <summary>
   /// Gets a short label describing how the board-level phase compensation angle was obtained.
   /// </summary>
   /// <returns>Phase calibration mode string.</returns>
   ///
   virtual const char* phaseCalibrationMode() const { return "none"; }

   ///
   /// <summary>
   /// Gets the active phase compensation angle in degrees for a specific channel.
   /// </summary>
   /// <param name="channel">Channel index.</param>
   /// <returns>Channel phase compensation angle in degrees.</returns>
   ///
   virtual float channelPhaseShiftDeg(size_t channel) const
   {
      (void)channel;
      return phaseShiftDeg();
   }

   ///
   /// <summary>
   /// Gets a short label describing how the specified channel's phase compensation was obtained.
   /// </summary>
   /// <param name="channel">Channel index.</param>
   /// <returns>Channel phase calibration mode string.</returns>
   ///
   virtual const char* channelPhaseCalibrationMode(size_t channel) const
   {
      (void)channel;
      return phaseCalibrationMode();
   }

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
      _rebuildDerivedChannels();
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
   /// Sets the name/suffix for the specified channel, marking channels containing "solar" as
   /// bidirectional solar circuits and parsing trailing "Red"/"Black"/"White" split-phase roles.
   /// </summary>
   /// <param name="channel">Channel index.</param>
   /// <param name="name">Channel name/suffix string.</param>
   ///
   void setChannelName(size_t channel, const char* name)
   {
      ASSERT(channel < MAX_CHANNELS);
      ASSERT(name != nullptr);

      ChannelState& ch = _channels[channel];
      ch.name = name;
      ch.isSolar = _isSolarChannelName(name);
      ch.splitRole = _parseSplitPhaseRole(name, &ch.prefixLen);
      if (!ch.isSolar && ch.splitRole != SplitPhaseRole::WHITE)
      {
         ch.polaritySign = 0;
         ch.polarityCandidateCount = 0;
         ch.polarityCandidateSign = 0;
      }

      _rebuildDerivedChannels();
   }

   ///
   /// <summary>
   /// Returns true if the specified channel is treated as a bidirectional solar circuit.
   /// </summary>
   /// <param name="channel">Channel index (default 0).</param>
   /// <returns>True if the channel is a solar circuit.</returns>
   ///
   bool isSolarChannel(size_t channel = 0) const
   {
      ASSERT(channel < MAX_CHANNELS);

      return _channels[channel].isSolar;
   }

   ///
   /// <summary>
   /// Returns true if the specified channel's name ends with "White" (case-insensitive),
   /// identifying a split-phase neutral return conductor.
   /// </summary>
   /// <param name="channel">Channel index (default 0).</param>
   /// <returns>True if the channel is a White neutral conductor.</returns>
   ///
   bool isWhiteChannel(size_t channel = 0) const
   {
      ASSERT(channel < MAX_CHANNELS);

      return _channels[channel].splitRole == SplitPhaseRole::WHITE;
   }

   ///
   /// <summary>
   /// Returns true if the specified channel's reading is currently derived from its sibling
   /// split-phase hot leg and White neutral rather than a direct physical CT on this port.
   /// </summary>
   /// <param name="channel">Channel index (default 0).</param>
   /// <returns>True if the channel is currently derived.</returns>
   ///
   bool isDerivedChannel(size_t channel = 0) const
   {
      ASSERT(channel < MAX_CHANNELS);

      return _channels[channel].isDerived;
   }

   ///
   /// <summary>
   /// Gets the learned polarity sign (+1, -1, or 0 if not yet learned) for a solar or White neutral channel.
   /// </summary>
   /// <param name="channel">Channel index (default 0).</param>
   /// <returns>Channel polarity multiplier (-1, 0, or +1).</returns>
   ///
   int8_t channelPolarity(size_t channel = 0) const
   {
      ASSERT(channel < MAX_CHANNELS);

      return _channels[channel].polaritySign;
   }

   ///
   /// <summary>
   /// Sets the persisted polarity sign (+1, -1, or 0 to auto-learn) for a solar or White neutral channel.
   /// </summary>
   /// <param name="channel">Channel index.</param>
   /// <param name="polaritySign">Polarity multiplier (-1, 0, or +1).</param>
   ///
   void setChannelPolarity(size_t channel, int8_t polaritySign)
   {
      ASSERT(channel < MAX_CHANNELS);

      ChannelState& ch = _channels[channel];
      if ((!ch.isSolar && ch.splitRole != SplitPhaseRole::WHITE) || polaritySign == 0)
      {
         ch.polaritySign = 0;
      }
      else
      {
         ch.polaritySign = polaritySign > 0 ? 1 : -1;
      }
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
            ch.traceVars[i] *= scale;
         }
         ch.rawAmps *= scale;
         ch.latchedAmps *= scale;
         ch.rawSignedWatts *= scale;
         ch.rawReactiveVars *= scale;
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
   /// Sets the phase offset for the physical CT model connected to a channel.
   /// </summary>
   /// <param name="channel">Channel index.</param>
   /// <param name="phaseOffsetDeg">CT-specific phase offset in degrees.</param>
   ///
   void setChannelPhaseOffsetDeg(size_t channel, float phaseOffsetDeg)
   {
      ASSERT(channel < MAX_CHANNELS);

      _channels[channel].phaseOffsetDeg = phaseOffsetDeg;
   }

   ///
   /// <summary>
   /// Gets the phase offset for the physical CT model connected to a channel.
   /// </summary>
   /// <param name="channel">Channel index.</param>
   /// <returns>CT-specific phase offset in degrees.</returns>
   ///
   float channelPhaseOffsetDeg(size_t channel) const
   {
      ASSERT(channel < MAX_CHANNELS);

      return _channels[channel].phaseOffsetDeg;
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
   /// Gets the most recent slice's signed reactive power in VAR when quadrature V*I sensing is active.
   /// </summary>
   /// <param name="channel">Channel index (default 0).</param>
   /// <returns>Signed reactive power in VAR.</returns>
   ///
   float reactivePower(size_t channel = 0) const
   {
      ASSERT(channel < MAX_CHANNELS);

      return _channels[channel].rawReactiveVars;
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

      float pf = std::fabs(realPower(channel)) / va;
      return pf > 1.0f ? 1.0f : pf;
   }

   ///
   /// <summary>
   /// Gets the number of auto-synthesized virtual split-phase channels (e.g. "Panel1Black"
   /// synthesized from "Panel1Red" + "Panel1White").
   /// </summary>
   /// <returns>Number of synthesized virtual derived channels.</returns>
   ///
   size_t derivedChannelCount() const
   {
      return _numDerivedChannels;
   }

   ///
   /// <summary>
   /// Gets the synthesized name of a virtual derived channel (e.g. "Panel1Black").
   /// </summary>
   /// <param name="index">Derived channel index (0 to derivedChannelCount() - 1).</param>
   /// <returns>Synthesized channel name string.</returns>
   ///
   const char* derivedChannelName(size_t index) const
   {
      ASSERT(index < MAX_DERIVED_CHANNELS);

      return _derivedChannels[index].name;
   }

   ///
   /// <summary>
   /// Returns true when both physical source CTs (sibling hot leg and White neutral) and the
   /// AC voltage reference are actively connected for the specified virtual derived channel.
   /// </summary>
   /// <param name="index">Derived channel index.</param>
   /// <returns>True if the virtual derived channel has live readings.</returns>
   ///
   bool isDerivedConnected(size_t index) const
   {
      ASSERT(index < MAX_DERIVED_CHANNELS);

      return _derivedChannels[index].connected;
   }

   ///
   /// <summary>
   /// Gets the derived RMS current in Amps for a virtual split-phase channel.
   /// </summary>
   /// <param name="index">Derived channel index.</param>
   /// <returns>Derived current in Amps.</returns>
   ///
   float derivedAmps(size_t index) const
   {
      ASSERT(index < MAX_DERIVED_CHANNELS);

      return _derivedChannels[index].amps;
   }

   ///
   /// <summary>
   /// Gets the derived apparent power in VA for a virtual split-phase channel.
   /// </summary>
   /// <param name="index">Derived channel index.</param>
   /// <returns>Derived apparent power in VA.</returns>
   ///
   float derivedApparentPower(size_t index) const
   {
      return derivedAmps(index) * _lineVoltageV;
   }

   ///
   /// <summary>
   /// Gets the derived real power in Watts for a virtual split-phase channel.
   /// </summary>
   /// <param name="index">Derived channel index.</param>
   /// <returns>Derived real power in Watts.</returns>
   ///
   float derivedRealPower(size_t index) const
   {
      ASSERT(index < MAX_DERIVED_CHANNELS);

      return _derivedChannels[index].watts;
   }

   ///
   /// <summary>
   /// Gets the derived power in Watts for a virtual split-phase channel.
   /// </summary>
   /// <param name="index">Derived channel index.</param>
   /// <returns>Derived power in Watts.</returns>
   ///
   float derivedWatts(size_t index) const
   {
      return hasVoltageSensor() ? derivedRealPower(index) : derivedApparentPower(index);
   }

   ///
   /// <summary>
   /// Gets the derived power factor (0.0 to 1.0) for a virtual split-phase channel.
   /// </summary>
   /// <param name="index">Derived channel index.</param>
   /// <returns>Derived power factor between 0.0 and 1.0.</returns>
   ///
   float derivedPowerFactor(size_t index) const
   {
      float va = derivedApparentPower(index);
      if (va <= 0.0f)
      {
         return 0.0f;
      }

      float pf = std::fabs(derivedRealPower(index)) / va;
      return pf > 1.0f ? 1.0f : pf;
   }

   ///
   /// <summary>
   /// Gets the total smoothed RMS current across all enabled non-neutral channels (including
   /// auto-synthesized virtual split-phase channels).
   /// </summary>
   /// <returns>Total current in Amps.</returns>
   ///
   float totalAmps() const
   {
      float total = 0.0f;
      for (size_t i = 0; i < _numChannels; i++)
      {
         if (_channels[i].splitRole != SplitPhaseRole::WHITE)
         {
            total += amps(i);
         }
      }
      for (size_t d = 0; d < _numDerivedChannels; d++)
      {
         total += derivedAmps(d);
      }
      return total;
   }

   ///
   /// <summary>
   /// Sums apparent power across all enabled non-neutral channels.
   /// </summary>
   /// <returns>Total apparent power in VA.</returns>
   ///
   float totalApparentPower() const
   {
      return totalAmps() * _lineVoltageV;
   }

   ///
   /// <summary>
   /// Sums real power across all enabled non-neutral channels (including auto-synthesized
   /// virtual split-phase channels).
   /// </summary>
   /// <returns>Total real power in Watts.</returns>
   ///
   float totalRealPower() const
   {
      float total = 0.0f;
      for (size_t i = 0; i < _numChannels; i++)
      {
         if (_channels[i].splitRole != SplitPhaseRole::WHITE)
         {
            total += realPower(i);
         }
      }
      for (size_t d = 0; d < _numDerivedChannels; d++)
      {
         total += derivedRealPower(d);
      }
      return total;
   }

   ///
   /// <summary>
   /// Gets total power across all enabled non-neutral channels (real power in Watts when a
   /// voltage sensor is present, otherwise apparent power in VA).
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
   /// Returns true if a channel has an active reading (either a physical CT sensor is detected
   /// or the channel is actively derived from a split-phase hot + White neutral pair).
   /// </summary>
   /// <param name="channel">Channel index (default 0).</param>
   /// <returns>True if connected or derived.</returns>
   ///
   bool isConnected(size_t channel = 0) const
   {
      ASSERT(channel < MAX_CHANNELS);

      return _channels[channel].latchedConnected;
   }

   ///
   /// <summary>
   /// Returns true only when a physical CT sensor is plugged into the specified port.
   /// </summary>
   /// <param name="channel">Channel index (default 0).</param>
   /// <returns>True if a physical CT is detected on the port.</returns>
   ///
   bool isPhysicallyConnected(size_t channel = 0) const
   {
      ASSERT(channel < MAX_CHANNELS);

      return _channels[channel].connected;
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
      text << "channels=" << _numChannels << "\n";
      if (_numDerivedChannels > 0)
      {
         text << "derivedChannels=" << _numDerivedChannels << "\n";
      }
      text << "sampleRate=" << sampleRate() << "/s\n";

      if (hasVoltageSensor())
      {
         text << "voltageConnected=" << (voltageConnected() ? "true" : "false") << "\n";
         text.precision(1);
         text << "lineVoltage=" << lineVoltage() << " V\n";
         if (hasPhaseCompensation())
         {
            text.precision(2);
            text << "phaseShift=" << phaseShiftDeg() << " deg (" << phaseCalibrationMode() << ")\n";
         }
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
         text << prefix << "connected=" << (ch.latchedConnected ? "true" : "false")
              << (ch.isDerived ? " (derived)" : "") << "\n";
         if (hasVoltageSensor())
         {
            text << prefix << "power=" << realPower(chIdx) << " W\n"
                 << prefix << "signedPower=" << ch.rawSignedWatts << " W\n"
                 << prefix << "reactivePower=" << ch.rawReactiveVars << " VAR\n";
            if (ch.isSolar || ch.splitRole == SplitPhaseRole::WHITE)
            {
               text << prefix << "polarity=" << (ch.polaritySign == 0 ? "auto" : (ch.polaritySign > 0 ? "+1" : "-1")) << "\n";
               if (ch.isSolar && hasPhaseCompensation())
               {
                  text.precision(2);
                  text << prefix << "phaseShift=" << channelPhaseShiftDeg(chIdx)
                       << " deg (" << channelPhaseCalibrationMode(chIdx) << ")\n";
                  text.precision(1);
               }
            }
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

         if (chIdx + 1 < _numChannels || _numDerivedChannels > 0)
         {
            text << "\n";
         }
      }

      for (size_t d = 0; d < _numDerivedChannels; d++)
      {
         const DerivedChannelState& derived = _derivedChannels[d];
         std::string prefix = std::string("derived") + std::to_string(d + 1) + "(" + derived.name + ").";
         text.precision(1);
         text << prefix << "connected=" << (derived.connected ? "true" : "false")
              << " (derived from ch" << (derived.hotChannel + 1)
              << "+ch" << (derived.whiteChannel + 1) << ")\n";
         if (hasVoltageSensor())
         {
            text << prefix << "power=" << derivedRealPower(d) << " W\n"
                 << prefix << "reactivePower=" << derived.reactiveVars << " VAR\n";
         }
         text << prefix << "apparentPower=" << derivedApparentPower(d) << " VA\n";
         if (hasVoltageSensor())
         {
            text.precision(2);
            text << prefix << "powerFactor=" << derivedPowerFactor(d) << "\n";
            text.precision(1);
         }
         text << prefix << "current=" << derivedAmps(d) << " A";
         if (d + 1 < _numDerivedChannels)
         {
            text << "\n";
         }
      }

      return text.str();
   }
};
