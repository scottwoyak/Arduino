#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "RollingAverage.h"
#include "SerialX.h"
#include "Stats.h"
#include "Timer.h"

///
/// <summary>
/// State flags emitted by SensorCapture state transitions.
/// </summary>
///
enum SensorCaptureState : uint8_t
{
   SENSOR_CAPTURE_STATE_NONE = 0,
   SENSOR_CAPTURE_STATE_VALUE_STORED = 1 << 0,
   SENSOR_CAPTURE_STATE_COMPLETED = 1 << 1,
};

///
/// <summary>
/// Captures finite sensor values on a fixed sampling cadence, then stops by duration or capacity.
/// </summary>
///
class SensorCapture
{
private:
   Timer _captureTimer;
   Timer _samplingTimer;

   size_t _numValues;

   float* _values;
   size_t _valueIndex;
   bool _captureComplete;

   bool _hasValues() const
   {
      return (_valueIndex > 0) && (_values != nullptr);
   }

public:
   ///
   /// <summary>
   /// Initializes capture session configuration.
   /// </summary>
   /// <param name="maxValues">Maximum number of values stored in RAM.</param>
   /// <param name="captureDurationMs">Maximum capture duration in milliseconds.</param>
   /// <param name="samplingIntervalMs">Collection interval in milliseconds.</param>
   ///
   SensorCapture(
      size_t maxValues,
      unsigned long captureDurationMs,
      unsigned long samplingIntervalMs)
      : _captureTimer(captureDurationMs),
        _samplingTimer(samplingIntervalMs),
        _numValues(maxValues),
        _values((_numValues > 0) ? new float[_numValues] : nullptr)
   {
      reset();
   }

   SensorCapture(const SensorCapture&) = delete;
   SensorCapture& operator=(const SensorCapture&) = delete;

   ~SensorCapture()
   {
      delete[] _values;
   }

   ///
   /// <summary>
   /// Resets internal capture state for a new capture cycle.
   /// </summary>
   ///
   void reset()
   {
      _valueIndex = 0;
      _captureComplete = false;
      _captureTimer.reset();
      _samplingTimer.reset();
   }

   ///
   /// <summary>
   /// Advances capture timeout state.
   /// </summary>
   /// <returns>State flags for transitions that occurred during this call.</returns>
   ///
   SensorCaptureState update()
   {
      SensorCaptureState states = SENSOR_CAPTURE_STATE_NONE;

      if (_captureComplete)
      {
         return states;
      }

      if ((_valueIndex >= _numValues) || _captureTimer.ready())
      {
         _captureComplete = true;
         states = static_cast<SensorCaptureState>(states | SENSOR_CAPTURE_STATE_COMPLETED);
      }

      return states;
   }

   ///
   /// <summary>
   /// Indicates whether a new value should be collected now.
   /// </summary>
   /// <returns>True when actively capturing and the value timer interval elapsed.</returns>
   ///
   bool readyForValue()
   {
      if (_captureComplete)
      {
         return false;
      }

      return _samplingTimer.ready();
   }

   ///
   /// <summary>
   /// Adds one sensor value to the capture pipeline.
   /// </summary>
   /// <param name="value">Value to evaluate/store.</param>
   /// <returns>State flags for storage/completion transitions.</returns>
   ///
   SensorCaptureState addValue(float value)
   {
      SensorCaptureState states = SENSOR_CAPTURE_STATE_NONE;

      if (_captureComplete || !isfinite(value))
      {
         return states;
      }

      if ((_valueIndex < _numValues) && (_values != nullptr))
      {
         _values[_valueIndex] = value;
         _valueIndex++;
         states = static_cast<SensorCaptureState>(states | SENSOR_CAPTURE_STATE_VALUE_STORED);
      }

      if (_valueIndex >= _numValues)
      {
         _captureComplete = true;
         states = static_cast<SensorCaptureState>(states | SENSOR_CAPTURE_STATE_COMPLETED);
      }

      return states;
   }

   ///
   /// <summary>
   /// Gets the number of stored values.
   /// </summary>
   /// <returns>Value count.</returns>
   ///
   size_t count() const
   {
      return _valueIndex;
   }

   ///
   /// <summary>
   /// Gets one stored value by index.
   /// </summary>
   /// <param name="index">Zero-based value index.</param>
   /// <returns>Stored value, or NaN when index is out of range.</returns>
   ///
   float operator[](size_t index) const
   {
      if ((index >= _valueIndex) || (_values == nullptr))
      {
         return NAN;
      }

      return _values[index];
   }

   ///
   /// <summary>
   /// Dumps all captured values to Serial in one operation.
   /// </summary>
   /// <param name="lineDelayMs">Delay between rows to avoid overwhelming Serial.</param>
   /// <param name="decimals">Decimal precision for value output.</param>
   ///
   void dumpToSerial(unsigned long lineDelayMs = 2, uint8_t decimals = 3) const
   {
      if (!_hasValues())
      {
         Serial.println("No values captured");
         return;
      }

      Serial.println("Dump start");
      SerialX::print("Index", 8);
      SerialX::println("Value", 12);
      SerialX::print("-----", 8);
      SerialX::println("-----------", 12);

      for (size_t i = 0; i < _valueIndex; i++)
      {
         SerialX::print(i, 8);
         SerialX::println(_values[i], decimals, 12);

         if ((lineDelayMs > 0) && (i + 1 < _valueIndex))
         {
            delay(lineDelayMs);
         }
      }

      Serial.println("Dump complete");
   }

   ///
   /// <summary>
   /// Prints the first and last captured values to Serial.
   /// </summary>
   /// <param name="count">Number of values to print from each end.</param>
   /// <param name="decimals">Decimal precision for value output.</param>
   ///
   void printFirstAndLastToSerial(size_t count = 10, uint8_t decimals = 3) const
   {
      if (!_hasValues())
      {
         Serial.println("No values captured");
         return;
      }

      size_t firstCount = min(count, _valueIndex);
      size_t tailStart = (_valueIndex > (count * 2)) ? (_valueIndex - count) : firstCount;

      Serial.println("First/Last Values");
      SerialX::print("Index", 8);
      SerialX::println("Value", 12);
      SerialX::print("-----", 8);
      SerialX::println("-----------", 12);

      for (size_t i = 0; i < firstCount; i++)
      {
         SerialX::print(i, 8);
         SerialX::println(_values[i], decimals, 12);
      }

      if (_valueIndex > (count * 2))
      {
         Serial.println("...");
      }

      for (size_t i = tailStart; i < _valueIndex; i++)
      {
         SerialX::print(i, 8);
         SerialX::println(_values[i], decimals, 12);
      }

      Serial.println();
   }

   ///
   /// <summary>
   /// Gets the internal value buffer pointer.
   /// </summary>
   /// <returns>Read-only pointer to stored value data.</returns>
   ///
   const float* values() const
   {
      return _values;
   }

   ///
   /// <summary>
   /// Indicates whether capture is complete.
   /// </summary>
   /// <returns>True when finished by duration or capacity.</returns>
   ///
   bool isCaptureComplete() const
   {
      return _captureComplete;
   }

   ///
   /// <summary>
   /// Computes average, population standard deviation, minimum, and maximum for finite captured values.
   /// </summary>
   /// <returns>Stats object populated from finite captured values.</returns>
   ///
   Stats computeBasicStats() const
   {
      Stats stats;

      for (size_t i = 0; i < _valueIndex; i++)
      {
         stats.add(_values[i]);
      }

      return stats;
   }

   ///
   /// <summary>
   /// Computes statistics of the moving-average series for a given window size.
   /// </summary>
   /// <param name="windowSize">Moving-average window size (N).</param>
   /// <returns>Stats object over the moving-average series for the requested window.</returns>
   ///
   Stats computeAverageSeriesStats(size_t windowSize) const
   {
      Stats averageStats;

      if ((windowSize == 0) || (_valueIndex < windowSize))
      {
         return averageStats;
      }

      RollingAverage blockAverage(windowSize);

      for (size_t valueIndex = 0; valueIndex < _valueIndex; valueIndex++)
      {
         blockAverage.set(_values[valueIndex]);

         if (blockAverage.count() == windowSize)
         {
            averageStats.add(blockAverage.average());
         }
      }

      return averageStats;
   }

   ///
   /// <summary>
   /// Computes min/max range for finite inputs.
   /// </summary>
   /// <param name="minValue">Minimum value.</param>
   /// <param name="maxValue">Maximum value.</param>
   /// <returns>Range (max-min), or NaN when either bound is non-finite.</returns>
   ///
   static float computeRange(float minValue, float maxValue)
   {
      if (!isfinite(minValue) || !isfinite(maxValue))
      {
         return NAN;
      }

      return maxValue - minValue;
   }
};
