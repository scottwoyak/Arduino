#pragma once

#include <algorithm>
#include <cmath>
#include <stdint.h>

#include "TimedStats.h"

/// <summary>
/// Manages sensor calibration by collecting measurements and computing correction factors.
/// Assumes all sensors should read equal values and computes per-sensor offsets to align readings.
/// </summary>
template<unsigned long (*TimeFunc)() = millis>
class CalibratorBase
{
public:
   /// <summary>
   /// Baseline computation mode.
   /// </summary>
   enum class BaselineMode
   {
      AVERAGE,      ///< Use average of all sensors as baseline
      FIRST_SENSOR  ///< Use first sensor as baseline reference
   };

private:
   static constexpr uint8_t MAX_SENSORS = 8;

   uint8_t _numSensors = 0;
   TimedStatsBase<TimeFunc>** _measurements = nullptr;
   BaselineMode _baselineMode = BaselineMode::AVERAGE;

   /// <summary>
   /// Computes the baseline according to the configured mode.
   /// </summary>
   /// <returns>Baseline value, or NaN if no measurements exist.</returns>
   float _getBaseline() const
   {
      if (_baselineMode == BaselineMode::FIRST_SENSOR)
      {
         return _measurements[0]->average();
      }

      float sum = 0.0f;
      int count = 0;

      for (uint8_t i = 0; i < _numSensors; i++)
      {
         float val = _measurements[i]->average();
         if (!std::isnan(val))
         {
            sum += val;
            count++;
         }
      }

      return (count > 0) ? (sum / count) : NAN;
   }

   /// <summary>
   /// Computes and returns the correction factor for a sensor.
   /// Correction = (baseline - sensorValue), so applying this offset makes the sensor read baseline.
   /// </summary>
   /// <param name="sensorIndex">Zero-based sensor index (0-7).</param>
   /// <returns>Correction factor, or NaN if measurement unavailable.</returns>
   float _computeCorrection(uint8_t sensorIndex) const
   {
      if (sensorIndex >= _numSensors)
         return NAN;

      float baseline = _getBaseline();
      float measurement = _measurements[sensorIndex]->average();

      if (std::isnan(baseline) || std::isnan(measurement))
         return NAN;

      return baseline - measurement;
   }

public:
   /// <summary>
   /// Constructs a Calibrator for the specified number of sensors.
   /// </summary>
   /// <param name="numSensors">Number of sensors (1-8).</param>
   /// <param name="durationMs">Duration window in milliseconds for collecting measurements.</param>
   /// <param name="mode">Baseline computation mode (default: AVERAGE).</param>
    CalibratorBase(uint8_t numSensors, ulong durationMs, BaselineMode mode = BaselineMode::AVERAGE)
      : _numSensors(std::min(numSensors, MAX_SENSORS)), _baselineMode(mode)
   {
      _measurements = new TimedStatsBase<TimeFunc>*[_numSensors];

      for (uint8_t i = 0; i < _numSensors; i++)
      {
         _measurements[i] = new TimedStatsBase<TimeFunc>(durationMs);
      }
   }

   /// <summary>
   /// Calibrator is non-copyable.
   /// </summary>
   CalibratorBase(const CalibratorBase&) = delete;

   /// <summary>
   /// Calibrator is non-assignable.
   /// </summary>
   CalibratorBase& operator=(const CalibratorBase&) = delete;

   /// <summary>
   /// Destructs the Calibrator and releases all resources.
   /// </summary>
   ~CalibratorBase()
   {
      for (uint8_t i = 0; i < _numSensors; i++)
      {
         delete _measurements[i];
      }
      delete[] _measurements;
   }

   /// <summary>
   /// Adds a measurement for the specified sensor.
   /// </summary>
   /// <param name="sensorIndex">Zero-based sensor index (0-7).</param>
   /// <param name="value">Measurement value to add.</param>
   void set(uint8_t sensorIndex, float value)
   {
      if (sensorIndex < _numSensors && !std::isnan(value))
      {
         _measurements[sensorIndex]->set(value);
      }
   }

   /// <summary>
   /// Gets the number of sensors managed by this Calibrator.
   /// </summary>
   /// <returns>Number of sensors.</returns>
   uint8_t getNumSensors() const
   {
      return _numSensors;
   }

   /// <summary>
   /// Gets the current averaged measurement for a sensor.
   /// </summary>
   /// <param name="sensorIndex">Zero-based sensor index (0-7).</param>
   /// <returns>Averaged measurement, or NaN if unavailable.</returns>
   float getAverage(uint8_t sensorIndex) const
   {
      if (sensorIndex >= _numSensors)
         return NAN;

      return _measurements[sensorIndex]->average();
   }

   /// <summary>
   /// Gets the current baseline value according to the configured mode.
   /// </summary>
   /// <returns>Baseline value, or NaN if no measurements exist.</returns>
   float getBaseline() const
   {
      return _getBaseline();
   }

   /// <summary>
   /// Gets the current correction factor for a sensor.
   /// </summary>
   /// <param name="sensorIndex">Zero-based sensor index (0-7).</param>
   /// <returns>Correction factor, or 0 if unavailable.</returns>
   float getCorrection(uint8_t sensorIndex) const
   {
      if (sensorIndex >= _numSensors)
         return 0.0f;

      float correction = _computeCorrection(sensorIndex);
      return std::isnan(correction) ? 0.0f : correction;
   }

   /// <summary>
   /// Resets all measurements.
   /// </summary>
   void reset()
   {
      for (uint8_t i = 0; i < _numSensors; i++)
      {
         _measurements[i]->reset();
      }
   }

   /// <summary>
   /// Gets the duration window in milliseconds.
   /// </summary>
   /// <param name="sensorIndex">Zero-based sensor index (0-7).</param>
   /// <returns>Duration in milliseconds.</returns>
   ulong getDurationMs(uint8_t sensorIndex) const
   {
      if (sensorIndex >= _numSensors)
         return 0;

      return _measurements[sensorIndex]->durationMs();
   }

       /// <summary>
       /// Gets the underlying timed statistics object for a sensor.
       /// </summary>
       /// <param name="sensorIndex">Zero-based sensor index (0-7).</param>
       /// <returns>TimedStats object for the sensor, or nullptr if index is invalid.</returns>
       TimedStatsBase<TimeFunc>* getStats(uint8_t sensorIndex) const
       {
          if (sensorIndex >= _numSensors)
             return nullptr;

          return _measurements[sensorIndex];
       }
   };

   using TempCalibrator = CalibratorBase<millis>;
