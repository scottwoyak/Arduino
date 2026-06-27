#pragma once

#include <Arduino.h>
#include <math.h>
#include <stddef.h>

#include "Histogram.h"
#include "RollingAverage.h"
#include "SensorCapture.h"
#include "Stats.h"

/// <summary>
/// Provides reusable statistics and histogram analysis helpers for captured sensor values.
/// </summary>
class SensorCaptureStats
{
private:
   const float* _values;
   size_t _valueCount;

public:
   /// <summary>
   /// Initializes stats over a fixed value buffer.
   /// </summary>
   /// <param name="values">Read-only value buffer.</param>
   /// <param name="valueCount">Number of values available in the buffer.</param>
   SensorCaptureStats(const float* values, size_t valueCount)
   {
      _values = values;
      _valueCount = valueCount;
   }

   /// <summary>
   /// Initializes stats from a SensorCapture instance.
   /// </summary>
   /// <param name="capture">Capture containing values to analyze.</param>
   SensorCaptureStats(const SensorCapture& capture)
   {
      _values = capture.values();
      _valueCount = capture.count();
   }

   /// <summary>
   /// Gets the number of values available for analysis.
   /// </summary>
   /// <returns>Value count.</returns>
   size_t count() const
   {
      return _valueCount;
   }

   /// <summary>
   /// Computes average, population standard deviation, minimum, and maximum for finite values.
   /// </summary>
   /// <param name="averageOut">Output average value.</param>
   /// <param name="stdDevOut">Output population standard deviation.</param>
   /// <param name="minOut">Output minimum value.</param>
   /// <param name="maxOut">Output maximum value.</param>
   /// <param name="finiteCountOut">Output finite value count used in calculations.</param>
   void computeBasicStats(
      float& averageOut,
      float& stdDevOut,
      float& minOut,
      float& maxOut,
      size_t& finiteCountOut) const
   {
      double valueSum = 0.0;
      double valueSumSquares = 0.0;
      float valueMin = NAN;
      float valueMax = NAN;
      size_t finiteValueCount = 0;

      for (size_t i = 0; i < _valueCount; i++)
      {
         float value = _values[i];
         if (!isfinite(value))
         {
            continue;
         }

         valueSum += value;
         valueSumSquares += static_cast<double>(value) * static_cast<double>(value);

         if (finiteValueCount == 0)
         {
            valueMin = value;
            valueMax = value;
         }
         else
         {
            valueMin = min(valueMin, value);
            valueMax = max(valueMax, value);
         }

         finiteValueCount++;
      }

      averageOut = NAN;
      stdDevOut = NAN;
      minOut = valueMin;
      maxOut = valueMax;
      finiteCountOut = finiteValueCount;

      if (finiteValueCount > 0)
      {
         const double count = static_cast<double>(finiteValueCount);
         const double mean = valueSum / count;
         const double variance = max(0.0, (valueSumSquares / count) - (mean * mean));
         averageOut = static_cast<float>(mean);
         stdDevOut = static_cast<float>(sqrt(variance));
      }
   }

   /// <summary>
   /// Computes min/max range for finite inputs.
   /// </summary>
   /// <param name="minValue">Minimum value.</param>
   /// <param name="maxValue">Maximum value.</param>
   /// <returns>Range (max-min), or NaN when either bound is non-finite.</returns>
   float computeRange(float minValue, float maxValue) const
   {
      if (!isfinite(minValue) || !isfinite(maxValue))
      {
         return NAN;
      }

      return maxValue - minValue;
   }

   /// <summary>
   /// Populates a histogram from captured values.
   /// </summary>
   /// <typeparam name="BIN_COUNT">Histogram bin count.</typeparam>
   /// <param name="histogram">Histogram instance to populate.</param>
   template<size_t BIN_COUNT>
   void buildHistogram(Histogram<BIN_COUNT>& histogram) const
   {
      for (size_t i = 0; i < _valueCount; i++)
      {
         histogram.add(_values[i]);
      }
   }

   /// <summary>
   /// Computes statistics of the moving-average series for a given window size.
   /// </summary>
   /// <param name="windowSize">Moving-average window size (N).</param>
   /// <param name="rangeOut">Output range of the moving-average series.</param>
   /// <param name="stdDevOut">Output standard deviation of the moving-average series.</param>
   /// <param name="countOut">Output number of average points evaluated.</param>
   void computeAverageSeriesStats(
      size_t windowSize,
      float& rangeOut,
      float& stdDevOut,
      size_t& countOut) const
   {
      rangeOut = NAN;
      stdDevOut = NAN;
      countOut = 0;

      if ((windowSize == 0) || (_valueCount < windowSize))
      {
         return;
      }

      RollingAverage blockAverage(windowSize);
      Stats averageStats;

      for (size_t valueIndex = 0; valueIndex < _valueCount; valueIndex++)
      {
         blockAverage.set(_values[valueIndex]);

         if (blockAverage.count() == windowSize)
         {
            averageStats.add(blockAverage.average());
         }
      }

      countOut = averageStats.count();
      float avgMin = averageStats.min();
      float avgMax = averageStats.max();
      rangeOut = computeRange(avgMin, avgMax);
      stdDevOut = averageStats.stdDev();
   }
};
