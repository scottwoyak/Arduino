#pragma once

#include <cmath>

#include "IDepthSensor.h"
#include "TimedAverage.h"

///
/// <summary>
/// Base class for IDepthSensor implementations that provides a common running-average based
/// wave height calculation on top of a sensor-specific raw depth reading.
/// </summary>
///
class DepthSensorBase : public IDepthSensor
{
public:
   static constexpr unsigned long DEFAULT_AVERAGE_DURATION_M = 5UL;

private:
   TimedAverage _average;
   float _lastRawDepth = NAN;

protected:
   ///
   /// <summary>
   /// Reads and returns the sensor's current raw depth value, in whatever unit the concrete
   /// implementation reports (see the implementation's documentation).
   /// </summary>
   /// <returns>The current raw depth reading.</returns>
   ///
   virtual float readRawDepth() = 0;

public:
   ///
   /// <summary>
   /// Initializes the running average window used by getWaveHeight().
   /// </summary>
   /// <param name="averageDurationM">Length of the running average window, in minutes.</param>
   ///
   explicit DepthSensorBase(unsigned long averageDurationM = DEFAULT_AVERAGE_DURATION_M) :
      _average(averageDurationM * 60UL * 1000UL)
   {
   }

   ///
   /// <summary>
   /// Gets the sensor's current raw depth value. Also updates the running average used by
   /// getWaveHeight(), so getWaveHeight() should be called after getDepth() to reflect the
   /// same underlying reading rather than triggering a second sensor read.
   /// </summary>
   /// <returns>The current raw depth reading.</returns>
   ///
   float getDepth() override
   {
      _lastRawDepth = readRawDepth();
      _average.set(_lastRawDepth);
      return _lastRawDepth;
   }

   ///
   /// <summary>
   /// Gets the offset of the most recent raw depth reading (from the last getDepth() call)
   /// from the running average of readings taken over the configured averaging window.
   /// </summary>
   /// <returns>The last reading minus the running average depth.</returns>
   ///
   float getWaveHeight()
   {
      return _lastRawDepth - _average.average();
   }

   ///
   /// <summary>
   /// Gets the running average depth over the configured averaging window.
   /// </summary>
   /// <returns>The average depth reading.</returns>
   ///
   float getAverageDepth()
   {
      return _average.average();
   }
};
