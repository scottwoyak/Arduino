#pragma once

#include <algorithm>
#include <cmath>

#include "TimedStats.h"

/// <summary>
/// Manages sensor calibration by collecting measurements and computing correction factors.
/// Assumes all sensors should read equal values and computes per-sensor offsets to align readings.
/// Also records historical samples at regular intervals for convergence analysis.
/// </summary>
class Calibrator
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

   /// <summary>
   /// Statistics result for a sensor against correction factors.
   /// </summary>
   struct SensorStats
   {
      float minCorrected;
      float maxCorrected;
      float rangeCorrected;
      float avgCorrected;
      int sampleCount;
   };

private:
   static constexpr uint8_t MAX_SENSORS = 8;
   static constexpr uint8_t MAX_SAMPLES = 100;
   static constexpr uint8_t SAMPLE_INTERVAL_DIVISOR = 10;  // Sample every historyMs / 10

   uint8_t _numSensors;
   TimedStats** _measurements;
   float* _corrections;
   BaselineMode _baselineMode;
   ulong _sampleIntervalMs;
   ulong _lastSampleTimeMs;

   // Historical samples: [sensorIndex][sampleIndex]
   float** _samples;
   uint8_t _sampleCount;

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

      // AVERAGE mode
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

public:
   /// <summary>
   /// Constructs a Calibrator for the specified number of sensors.
   /// </summary>
   /// <param name="numSensors">Number of sensors (1-8).</param>
   /// <param name="historyMs">History window in milliseconds for collecting measurements.</param>
   /// <param name="mode">Baseline computation mode (default: AVERAGE).</param>
   Calibrator(uint8_t numSensors, ulong historyMs, BaselineMode mode = BaselineMode::AVERAGE)
      : _numSensors(std::min(numSensors, MAX_SENSORS)), _baselineMode(mode), 
        _sampleIntervalMs(historyMs / SAMPLE_INTERVAL_DIVISOR), _lastSampleTimeMs(0), _sampleCount(0)
   {
      _measurements = new TimedStats*[_numSensors];
      _corrections = new float[_numSensors];
      _samples = new float*[_numSensors];

      for (uint8_t i = 0; i < _numSensors; i++)
      {
         _measurements[i] = new TimedStats(historyMs);
         _corrections[i] = 0.0f;
         _samples[i] = new float[MAX_SAMPLES];
         for (uint8_t j = 0; j < MAX_SAMPLES; j++)
         {
            _samples[i][j] = NAN;
         }
      }
   }

   /// <summary>
   /// Destructs the Calibrator and releases all resources.
   /// </summary>
   ~Calibrator()
   {
      for (uint8_t i = 0; i < _numSensors; i++)
      {
         delete _measurements[i];
         delete[] _samples[i];
      }
      delete[] _measurements;
      delete[] _corrections;
      delete[] _samples;
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
         _recordSample(sensorIndex, value);
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
   float getMeasurement(uint8_t sensorIndex) const
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
   /// Computes and returns the correction factor for a sensor.
   /// Correction = (baseline - sensorValue), so applying this offset makes the sensor read baseline.
   /// </summary>
   /// <param name="sensorIndex">Zero-based sensor index (0-7).</param>
   /// <returns>Correction factor, or NaN if measurement unavailable.</returns>
   float computeCorrection(uint8_t sensorIndex) const
   {
      if (sensorIndex >= _numSensors)
         return NAN;

      float baseline = _getBaseline();
      float measurement = _measurements[sensorIndex]->average();

      if (std::isnan(baseline) || std::isnan(measurement))
         return NAN;

      return baseline - measurement;
   }

   /// <summary>
   /// Computes and stores the correction factors for all sensors.
   /// </summary>
   void computeAllCorrections()
   {
      for (uint8_t i = 0; i < _numSensors; i++)
      {
         _corrections[i] = computeCorrection(i);
      }
   }

   /// <summary>
   /// Gets the stored correction factor for a sensor.
   /// </summary>
   /// <param name="sensorIndex">Zero-based sensor index (0-7).</param>
   /// <returns>Stored correction factor, or 0 if not computed.</returns>
   float getCorrection(uint8_t sensorIndex) const
   {
      if (sensorIndex >= _numSensors)
         return 0.0f;

      return _corrections[sensorIndex];
   }

   /// <summary>
   /// Resets all measurements, corrections, and historical samples.
   /// </summary>
   void reset()
   {
      for (uint8_t i = 0; i < _numSensors; i++)
      {
         _measurements[i]->reset();
         _corrections[i] = 0.0f;
         for (uint8_t j = 0; j < MAX_SAMPLES; j++)
         {
            _samples[i][j] = NAN;
         }
      }
      _sampleCount = 0;
      _lastSampleTimeMs = 0;
   }

   /// <summary>
   /// Gets the history window duration in milliseconds.
   /// </summary>
   /// <param name="sensorIndex">Zero-based sensor index (0-7).</param>
   /// <returns>History duration in milliseconds.</returns>
   ulong getHistoryMs(uint8_t sensorIndex) const
   {
      if (sensorIndex >= _numSensors)
         return 0;

      return _measurements[sensorIndex]->durationMs();
   }

   /// <summary>
   /// Gets the number of historical samples recorded so far.
   /// </summary>
   /// <returns>Number of samples (0-100).</returns>
   uint8_t getSampleCount() const
   {
      return _sampleCount;
   }

   /// <summary>
   /// Gets statistics of historical samples with correction factors applied.
   /// </summary>
   /// <param name="sensorIndex">Zero-based sensor index (0-7).</param>
   /// <returns>SensorStats structure with min, max, range, and average of corrected values.</returns>
   SensorStats getStats(uint8_t sensorIndex) const
   {
      SensorStats stats;
      stats.minCorrected = NAN;
      stats.maxCorrected = NAN;
      stats.rangeCorrected = NAN;
      stats.avgCorrected = NAN;
      stats.sampleCount = 0;

      if (sensorIndex >= _numSensors)
         return stats;

      float correction = _corrections[sensorIndex];
      float sum = 0.0f;
      int validCount = 0;

      for (uint8_t i = 0; i < _sampleCount; i++)
      {
         float sample = _samples[sensorIndex][i];
         if (!std::isnan(sample))
         {
            float corrected = sample + correction;

            if (validCount == 0)
            {
               stats.minCorrected = corrected;
               stats.maxCorrected = corrected;
            }
            else
            {
               stats.minCorrected = std::min(stats.minCorrected, corrected);
               stats.maxCorrected = std::max(stats.maxCorrected, corrected);
            }

            sum += corrected;
            validCount++;
         }
      }

      if (validCount > 0)
      {
         stats.avgCorrected = sum / validCount;
         stats.rangeCorrected = stats.maxCorrected - stats.minCorrected;
         stats.sampleCount = validCount;
      }

      return stats;
   }

private:
   /// <summary>
   /// Records a sample if enough time has elapsed since the last sample.
   /// Samples are recorded at intervals of historyMs / 10, up to 100 samples.
   /// </summary>
   void _recordSample(uint8_t sensorIndex, float value)
   {
      ulong currentTimeMs = millis();

      // Check if we should record a sample (only check for sensor 0 to avoid redundant checks)
      if (sensorIndex == 0 && (currentTimeMs - _lastSampleTimeMs >= _sampleIntervalMs))
      {
         if (_sampleCount < MAX_SAMPLES)
         {
            _lastSampleTimeMs = currentTimeMs;

            // Record current measurement for all sensors
            for (uint8_t i = 0; i < _numSensors; i++)
            {
               _samples[i][_sampleCount] = _measurements[i]->average();
            }

            _sampleCount++;
         }
      }
   }
};
