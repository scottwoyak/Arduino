#pragma once

#include <Arduino.h>
#include <math.h>
#include <stddef.h>

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
   /// <returns>Stats object populated from finite captured values.</returns>
   Stats computeBasicStats() const
   {
      Stats stats;

      for (size_t i = 0; i < _valueCount; i++)
      {
         stats.add(_values[i]);
      }

      return stats;
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
   /// Computes statistics of the moving-average series for a given window size.
   /// </summary>
   /// <param name="windowSize">Moving-average window size (N).</param>
   /// <returns>Stats object over the moving-average series for the requested window.</returns>
   Stats computeAverageSeriesStats(size_t windowSize) const
   {
      Stats averageStats;

      if ((windowSize == 0) || (_valueCount < windowSize))
      {
         return averageStats;
      }

      RollingAverage blockAverage(windowSize);

      for (size_t valueIndex = 0; valueIndex < _valueCount; valueIndex++)
      {
         blockAverage.set(_values[valueIndex]);

         if (blockAverage.count() == windowSize)
         {
            averageStats.add(blockAverage.average());
         }
      }

      return averageStats;
   }
};
