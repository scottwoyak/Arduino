#pragma once

#include <cmath>
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
   // Sensor is never deployed deeper than ~10' (304.8 cm); anything outside this range
   // (with some margin for above-water mounting/waves) indicates a bad/corrupted I2C
   // reading rather than a real depth, since there's no error signal available from the
   // underlying MS5837 library's read() method.
   static constexpr float MIN_PLAUSIBLE_DEPTH_CM = -50.0f;
   static constexpr float MAX_PLAUSIBLE_DEPTH_CM = 350.0f;

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
   /// <returns>The current depth, in centimeters, minus the configured baseline, or NAN if the sensor read failed.</returns>
   float readRawDepth() override
   {
      float rawCm = rawDepthCm();
      if (isnan(rawCm))
      {
         return NAN;
      }

      return rawCm - _baselineCm;
   }

public:
   /// <summary>
   /// Reads the sensor and returns the raw depth in centimeters, unaffected by baseline.
   /// </summary>
   /// <returns>The current raw depth in centimeters, or NAN if the reading is implausible
   /// (indicating a corrupted I2C read, since MS5837::read() has no error return).</returns>
   float rawDepthCm()
   {
      _sensor.read();
      float depthCm = _sensor.depth() * METERS_TO_CENTIMETERS;
      if (depthCm < MIN_PLAUSIBLE_DEPTH_CM || depthCm > MAX_PLAUSIBLE_DEPTH_CM)
      {
         return NAN;
      }

      return depthCm;
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
