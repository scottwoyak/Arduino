#pragma once

#include <cmath>

#include "Util.h"

///
/// <summary>
/// Keeps track of the number of events that occur over a designated amount of time.
/// </summary>
///
/// <remarks>
/// Divides the tracked duration into a fixed number of sub-bins and rotates them out as
/// time advances, blending the transitioning sub-bin so the count decays smoothly.
/// </remarks>
///
/// <typeparam name="TimeFunc">Function pointer returning current time in microseconds.</typeparam>
///
template<unsigned long (*TimeFunc)() = micros>
class TimedBinBase
{
private:
   uint16_t* _subBins;
   uint16_t _numSubBins;
   uint32_t _currentSubBinStartTimeMicros;
   uint32_t _subBinDurationMicros;
   uint16_t _index;
   uint16_t _transitionSubBin;

   ///
   /// <summary>
   /// Rotates expired sub-bins forward, clearing each as it becomes the active sub-bin.
   /// </summary>
   ///
   void _advanceIfNeeded()
   {
      while (Util::getSpan(_currentSubBinStartTimeMicros, TimeFunc()) > _subBinDurationMicros)
      {
         _currentSubBinStartTimeMicros += _subBinDurationMicros;
         _index++;
         if (_index >= _numSubBins)
         {
            _index = 0;
         }
         _transitionSubBin = _subBins[_index];
         _subBins[_index] = 0;
      }
   }

public:
   ///
   /// <summary>
   /// Initializes a timed bin counter for a duration divided into sub-bins.
   /// </summary>
   /// <param name="durationMs">Total duration in milliseconds.</param>
   /// <param name="numSubBins">Number of sub-bins used to smooth transitions.</param>
   ///
   TimedBinBase(uint32_t durationMs, uint16_t numSubBins = 10)
   {
      _numSubBins = numSubBins;
      _subBins = new uint16_t[numSubBins];
      _subBinDurationMicros = (1000 * durationMs) / numSubBins;
      reset();
   }

   TimedBinBase(const TimedBinBase&) = delete;
   TimedBinBase& operator=(const TimedBinBase&) = delete;

   ///
   /// <summary>
   /// Releases the allocated sub-bin storage.
   /// </summary>
   ///
   ~TimedBinBase()
   {
      delete[] _subBins;
   }

   ///
   /// <summary>
   /// Re-anchors the current sub-bin start time without clearing accumulated counts.
   /// </summary>
   ///
   void begin()
   {
      _currentSubBinStartTimeMicros = TimeFunc();
   }

   ///
   /// <summary>
   /// Clears all bin counts and restarts timing from now.
   /// </summary>
   ///
   void reset()
   {
      for (uint16_t i = 0; i < _numSubBins; i++)
      {
         _subBins[i] = 0;
      }
      _transitionSubBin = 0;
      _index = 0;
      _currentSubBinStartTimeMicros = TimeFunc();
   }

   ///
   /// <summary>
   /// Records an event in the current sub-bin.
   /// </summary>
   ///
   void add()
   {
      _advanceIfNeeded();
      _subBins[_index]++;
   }

   ///
   /// <summary>
   /// Gets the total event count across the tracked duration, blending the transitioning sub-bin.
   /// </summary>
   /// <returns>The estimated event count within the tracked duration.</returns>
   ///
   uint16_t getCount()
   {
      _advanceIfNeeded();

      uint16_t count = 0;
      for (uint16_t i = 0; i < _numSubBins; i++)
      {
         count += _subBins[i];
      }
      float partial = 1.0f - ((float)Util::getSpan(_currentSubBinStartTimeMicros, TimeFunc())) / _subBinDurationMicros;
      count += std::roundf(partial * _transitionSubBin);

      return count;
   }

   ///
   /// <summary>
   /// Gets the raw count for a specific sub-bin. Intended for debugging/diagnostics only.
   /// </summary>
   /// <param name="binIndex">Zero-based sub-bin index, or -1 for the transitioning sub-bin.</param>
   /// <returns>The raw count for the specified sub-bin.</returns>
   ///
   uint16_t getBinCount(int binIndex)
   {
      if (binIndex == -1)
      {
         return _transitionSubBin;
      }
      else
      {
         return _subBins[binIndex];
      }
   }

   ///
   /// <summary>
   /// Prints bin configuration and current counts to Serial. Intended for debugging only.
   /// </summary>
   ///
   void println()
   {
      Serial.println("TimedBin: ");
      Serial.println("  Num Sub Bins: " + String(_numSubBins));
      Serial.println("  Time / Bin (ms): " + String(_subBinDurationMicros / 1000.0));
      Serial.println("  Total Time (ms): " + String(_subBinDurationMicros * _numSubBins / 1000.0) + " ms");
      Serial.println("  Bin Counts:");
      for (uint16_t i = 0; i < _numSubBins; i++)
      {
         Serial.println("    Bin[" + String(i) + "]: " + String(_subBins[i]));
      }
      Serial.println("    Transition Bin: " + String(_transitionSubBin));
   }
};

using TimedBin = TimedBinBase<micros>;
