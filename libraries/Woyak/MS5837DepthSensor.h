#pragma once

#include <MS5837.h>
#include "DepthSensorBase.h"

/// <summary>
/// Wraps a Blue Robotics MS5837 pressure/temperature sensor to report water depth, in
/// centimeters, relative to an adjustable baseline.
/// </summary>
class MS5837DepthSensor : public DepthSensorBase
{
public:
   static constexpr float FRESH_WATER_DENSITY_KG_PER_M3 = 997.0f;
   static constexpr unsigned long DEFAULT_AVERAGE_DURATION_M = 2UL;

private:
   static constexpr float METERS_TO_CENTIMETERS = 100.0f;

   MS5837 _sensor;
   float _fluidDensityKgPerM3;
   float _baselineCm;

public:
   /// <summary>
   /// Initializes an MS5837 depth sensor wrapper. Only the MS5837_02BA model is supported.
   /// </summary>
   /// <param name="fluidDensityKgPerM3">Density of the working fluid in kg/m^3. Defaults to fresh water.</param>
   /// <param name="averageDurationM">Length of the running average window used by getWaveHeight(), in minutes.</param>
   MS5837DepthSensor(float fluidDensityKgPerM3 = FRESH_WATER_DENSITY_KG_PER_M3, unsigned long averageDurationM = DEFAULT_AVERAGE_DURATION_M) :
      DepthSensorBase(averageDurationM)
   {
      _fluidDensityKgPerM3 = fluidDensityKgPerM3;
      _baselineCm = 0.0f;
   }

   /// <summary>
   /// Initializes the underlying MS5837 sensor over I2C and applies the MS5837_02BA model
   /// and configured fluid density.
   /// </summary>
   /// <returns>True if the sensor initialized successfully; otherwise false.</returns>
   bool begin() override
   {
      if (!_sensor.init())
      {
         return false;
      }

      _sensor.setModel(MS5837::MS5837_02BA);
      _sensor.setFluidDensity(_fluidDensityKgPerM3);
      return true;
   }

protected:
   /// <summary>
   /// Reads the sensor and returns depth in centimeters relative to the current baseline.
   /// </summary>
   /// <returns>The current depth, in centimeters, minus the configured baseline.</returns>
   float readRawDepth() override
   {
      return rawDepthCm() - _baselineCm;
   }

public:
   /// <summary>
   /// Reads the sensor and returns the raw depth in centimeters, unaffected by baseline.
   /// </summary>
   /// <returns>The current raw depth in centimeters.</returns>
   float rawDepthCm()
   {
      _sensor.read();
      return _sensor.depth() * METERS_TO_CENTIMETERS;
   }

   /// <summary>
   /// Sets the baseline depth (in centimeters) that is subtracted from raw readings by getDepth().
   /// </summary>
   /// <param name="baselineCm">The new baseline depth, in centimeters.</param>
   void setBaseline(float baselineCm)
   {
      _baselineCm = baselineCm;
   }

   /// <summary>
   /// Gets the currently configured baseline depth, in centimeters.
   /// </summary>
   /// <returns>The current baseline depth in centimeters.</returns>
   float getBaseline() const
   {
      return _baselineCm;
   }
};
