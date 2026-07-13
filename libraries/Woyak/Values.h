#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "RollingAverage.h"
#include "SerialX.h"
#include "Stats.h"

///
/// <summary>
/// Captures finite values (each with its own storage timestamp) up to a fixed capacity.
/// </summary>
///
class Values
{
private:
   size_t _numValues;

   float* _values;
   unsigned long* _timestampsMs;
   size_t _valueIndex;
   bool _captureComplete;

   bool _hasValues() const
   {
      return (_valueIndex > 0) && (_values != nullptr);
   }

public:
   ///
   /// <summary>
   /// Initializes an empty Values object with no capacity. Call reset(maxValues) before use.
   /// </summary>
   ///
   Values()
      : _numValues(0),
        _values(nullptr),
        _timestampsMs(nullptr)
   {
      reset();
   }

   ///
   /// <summary>
   /// Initializes capture session configuration.
   /// </summary>
   /// <param name="maxValues">Maximum number of values stored in RAM.</param>
   ///
   Values(size_t maxValues)
      : _numValues(0),
        _values(nullptr),
        _timestampsMs(nullptr)
   {
      reset(maxValues);
   }

   Values(const Values&) = delete;
   Values& operator=(const Values&) = delete;

   ~Values()
   {
      delete[] _values;
      delete[] _timestampsMs;
   }

   ///
   /// <summary>
   /// (Re)configures the capacity for this capture session, reallocating the internal storage
   /// buffers, then resets capture state for a new capture cycle.
   /// </summary>
   /// <param name="maxValues">Maximum number of values stored in RAM.</param>
   ///
   void reset(size_t maxValues)
   {
      delete[] _values;
      delete[] _timestampsMs;

      _numValues = maxValues;
      _values = (_numValues > 0) ? new float[_numValues] : nullptr;
      _timestampsMs = (_numValues > 0) ? new unsigned long[_numValues] : nullptr;

      reset();
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
   }

   ///
   /// <summary>
   /// Adds one value to the capture pipeline, recording the current time as its timestamp.
   /// </summary>
   /// <param name="value">Value to evaluate/store.</param>
   /// <returns>True if the value was stored; false if capture is complete or the value is not finite.</returns>
   ///
   bool addValue(float value)
   {
      if (_captureComplete || !isfinite(value))
      {
         return false;
      }

      bool stored = false;

      if ((_valueIndex < _numValues) && (_values != nullptr))
      {
         _values[_valueIndex] = value;
         _timestampsMs[_valueIndex] = millis();
         _valueIndex++;
         stored = true;
      }

      if (_valueIndex >= _numValues)
      {
         _captureComplete = true;
      }

      return stored;
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
   /// Gets the storage timestamp for a stored value.
   /// </summary>
   /// <param name="index">Zero-based value index.</param>
   /// <returns>Timestamp in milliseconds when the value was stored, or 0 when index is out of range.</returns>
   ///
   unsigned long timestamp(size_t index) const
   {
      if ((index >= _valueIndex) || (_timestampsMs == nullptr))
      {
         return 0;
      }

      return _timestampsMs[index];
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
