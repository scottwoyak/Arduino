#pragma once

#include <Arduino.h>
#include <stdint.h>

#include "SerialX.h"
#include "Timer.h"

/// <summary>
/// State flags emitted by SensorCapture state transitions.
/// </summary>
enum SensorCaptureState : uint8_t
{
   SENSOR_CAPTURE_STATE_NONE = 0,
   SENSOR_CAPTURE_STATE_CAPTURE_STARTED = 1 << 0,
   SENSOR_CAPTURE_STATE_STABILIZED = 1 << 1,
   SENSOR_CAPTURE_STATE_VALUE_STORED = 1 << 2,
   SENSOR_CAPTURE_STATE_COMPLETED = 1 << 3,
};

/// <summary>
/// Captures finite sensor values after warm-up and stabilization, then stops by duration or capacity.
/// </summary>
class SensorCapture
{
private:
   Timer _warmUpTimer;
   Timer _captureTimer;
   Timer _samplingTimer;

   size_t _numValues;
   float _stabilityDeltaPercent;
   size_t _stabilityRequiredValues;
   bool _stabilizationEnabled;

   float* _values;
   size_t _valueIndex;

   bool _captureStarted;
   bool _captureStabilized;
   bool _captureComplete;

   float _lastValue;
   size_t _stableValueCount;
   size_t _maxStableValueCount;
   size_t _stabilityComparisons;
   float _lastStabilityDelta;
   float _lastAllowedDelta;
   float _lastAverageMagnitude;

   void resetStabilizationState()
   {
      _captureStabilized = !_stabilizationEnabled;
      _lastValue = NAN;
      _stableValueCount = 0;
      _maxStableValueCount = 0;
      _stabilityComparisons = 0;
      _lastStabilityDelta = NAN;
      _lastAllowedDelta = NAN;
      _lastAverageMagnitude = NAN;
   }

   bool hasValues() const
   {
      return (_valueIndex > 0) && (_values != nullptr);
   }

public:
   /// <summary>
   /// Initializes capture session configuration.
   /// </summary>
   /// <param name="maxValues">Maximum number of values stored in RAM.</param>
   /// <param name="warmUpMs">Warm-up delay before value collection can start.</param>
   /// <param name="captureDurationMs">Maximum capture duration in milliseconds.</param>
   /// <param name="samplingIntervalMs">Collection interval in milliseconds.</param>
   /// <param name="stabilityDeltaPercent">Allowed delta as a fraction of average consecutive value magnitude (for example, 0.004 = 0.4%).</param>
   /// <param name="stabilityRequiredValues">Consecutive stable value count required before storage starts.</param>
   /// <param name="stabilizationEnabled">When false, values are stored immediately after warm-up without stabilization gating.</param>
   SensorCapture(
      size_t maxValues,
      unsigned long warmUpMs,
      unsigned long captureDurationMs,
      unsigned long samplingIntervalMs,
      float stabilityDeltaPercent,
      size_t stabilityRequiredValues,
      bool stabilizationEnabled = true)
      : _warmUpTimer(warmUpMs),
      _captureTimer(captureDurationMs),
      _samplingTimer(samplingIntervalMs)
   {
      _numValues = maxValues;
      _stabilityDeltaPercent = stabilityDeltaPercent;
      _stabilityRequiredValues = stabilityRequiredValues;
      _stabilizationEnabled = stabilizationEnabled;
      _values = (_numValues > 0) ? new float[_numValues] : nullptr;
      startWarmUp();
   }

   SensorCapture(const SensorCapture&) = delete;
   SensorCapture& operator=(const SensorCapture&) = delete;

   ~SensorCapture()
   {
      delete[] _values;
   }

   /// <summary>
   /// Resets internal capture state for a new warm-up/capture cycle.
   /// </summary>
   void startWarmUp()
   {
      _valueIndex = 0;
      _captureStarted = false;
      _captureComplete = false;
      resetStabilizationState();
   }

   /// <summary>
   /// Advances warm-up/start/timeout state.
   /// </summary>
   /// <returns>State flags for transitions that occurred during this call.</returns>
   SensorCaptureState update()
   {
      SensorCaptureState states = SENSOR_CAPTURE_STATE_NONE;

      if (_captureComplete)
      {
         return states;
      }

      if (!_captureStarted)
      {
         if (_warmUpTimer.ready())
         {
            _captureStarted = true;
            resetStabilizationState();
            states = static_cast<SensorCaptureState>(states | SENSOR_CAPTURE_STATE_CAPTURE_STARTED);
         }

         return states;
      }

      if ((_valueIndex >= _numValues) || _captureTimer.ready())
      {
         _captureComplete = true;
         states = static_cast<SensorCaptureState>(states | SENSOR_CAPTURE_STATE_COMPLETED);
      }

      return states;
   }

   /// <summary>
   /// Indicates whether a new value should be collected now.
   /// </summary>
   /// <returns>True when actively capturing and the value timer interval elapsed.</returns>
   bool readyForValue()
   {
      if (!_captureStarted || _captureComplete)
      {
         return false;
      }

      return _samplingTimer.ready();
   }

   /// <summary>
   /// Adds one sensor value to the capture pipeline.
   /// </summary>
   /// <param name="value">Value to evaluate/store.</param>
   /// <returns>State flags for stabilization/storage/completion transitions.</returns>
   SensorCaptureState addValue(float value)
   {
      SensorCaptureState states = SENSOR_CAPTURE_STATE_NONE;

      if (!_captureStarted || _captureComplete || !isfinite(value))
      {
         return states;
      }

      bool stabilizedNow = false;
      if (!_captureStabilized)
      {
         if (!isfinite(_lastValue))
         {
            _lastValue = value;
            _stableValueCount = 0;
            return states;
         }

         float delta = fabsf(value - _lastValue);
         float averageMagnitude = (fabsf(value) + fabsf(_lastValue)) / 2.0f;
         float allowedDelta = averageMagnitude * _stabilityDeltaPercent;
         _lastStabilityDelta = delta;
         _lastAllowedDelta = allowedDelta;
         _lastAverageMagnitude = averageMagnitude;
         _stabilityComparisons++;
         _lastValue = value;

         if (delta <= allowedDelta)
         {
            _stableValueCount++;
            _maxStableValueCount = max(_maxStableValueCount, _stableValueCount);
         }
         else
         {
            _stableValueCount = 0;
         }

         if (_stableValueCount < _stabilityRequiredValues)
         {
            return states;
         }

         _captureStabilized = true;
         stabilizedNow = true;
         states = static_cast<SensorCaptureState>(states | SENSOR_CAPTURE_STATE_STABILIZED);
      }

      if (stabilizedNow)
      {
         return states;
      }

      _lastValue = value;

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

   /// <summary>
   /// Gets the number of stored values.
   /// </summary>
   /// <returns>Value count.</returns>
   size_t count() const
   {
      return _valueIndex;
   }

   /// <summary>
   /// Gets one stored value by index.
   /// </summary>
   /// <param name="index">Zero-based value index.</param>
   /// <returns>Stored value, or NaN when index is out of range.</returns>
   float operator[](size_t index) const
   {
      if ((index >= _valueIndex) || (_values == nullptr))
      {
         return NAN;
      }

      return _values[index];
   }

   /// <summary>
   /// Dumps all captured values to Serial in one operation.
   /// </summary>
   /// <param name="lineDelayMs">Delay between rows to avoid overwhelming Serial.</param>
   /// <param name="decimals">Decimal precision for value output.</param>
   void dumpToSerial(unsigned long lineDelayMs = 2, uint8_t decimals = 3) const
   {
      if (!hasValues())
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
         SerialX::print((unsigned long)i, 8);
         SerialX::println(_values[i], decimals, 12);

         if ((lineDelayMs > 0) && (i + 1 < _valueIndex))
         {
            delay(lineDelayMs);
         }
      }

      Serial.println("Dump complete");
   }

   /// <summary>
   /// Prints the first and last captured values to Serial.
   /// </summary>
   /// <param name="count">Number of values to print from each end.</param>
   /// <param name="decimals">Decimal precision for value output.</param>
   void printFirstAndLastToSerial(size_t count = 10, uint8_t decimals = 3) const
   {
      if (!hasValues())
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
         SerialX::print((unsigned long)i, 8);
         SerialX::println(_values[i], decimals, 12);
      }

      if (_valueIndex > (count * 2))
      {
         Serial.println("...");
      }

      for (size_t i = tailStart; i < _valueIndex; i++)
      {
         SerialX::print((unsigned long)i, 8);
         SerialX::println(_values[i], decimals, 12);
      }

      Serial.println();
   }

   /// <summary>
   /// Gets the internal value buffer pointer.
   /// </summary>
   /// <returns>Read-only pointer to stored value data.</returns>
   const float* values() const
   {
      return _values;
   }

   /// <summary>
   /// Indicates whether capture start has occurred after warm-up.
   /// </summary>
   /// <returns>True when value collection phase has started.</returns>
   bool isCaptureStarted() const
   {
      return _captureStarted;
   }

   /// <summary>
   /// Indicates whether stabilization criteria were satisfied.
   /// </summary>
   /// <returns>True when stable capture has begun.</returns>
   bool isCaptureStabilized() const
   {
      return _captureStabilized;
   }

   /// <summary>
   /// Prints stabilization diagnostics when capture did not stabilize.
   /// </summary>
   /// <param name="decimals">Decimal precision for floating-point values.</param>
   void printStabilizationDiagnosticsToSerial(uint8_t decimals = 3) const
   {
      if (_captureStabilized)
      {
         return;
      }

      Serial.println("Stabilization not reached");
      SerialX::print("Stable needed", 20);
      SerialX::println((unsigned long)_stabilityRequiredValues, 20);
      SerialX::print("Stable max", 20);
      SerialX::println((unsigned long)_maxStableValueCount, 20);
      SerialX::print("Checks", 20);
      SerialX::println((unsigned long)_stabilityComparisons, 20);
      SerialX::print("Delta %", 20);
      SerialX::println(_stabilityDeltaPercent * 100.0f, 2, 20);

      if (_stabilityComparisons > 0)
      {
         SerialX::print("Last avg", 20);
         SerialX::println(_lastAverageMagnitude, decimals, 20);
         SerialX::print("Last delta", 20);
         SerialX::println(_lastStabilityDelta, decimals, 20);
         SerialX::print("Last allowed", 20);
         SerialX::println(_lastAllowedDelta, decimals, 20);
      }
      else
      {
         Serial.println("No stabilization comparisons were evaluated.");
      }
   }

   /// <summary>
   /// Indicates whether capture is complete.
   /// </summary>
   /// <returns>True when finished by duration or capacity.</returns>
   bool isCaptureComplete() const
   {
      return _captureComplete;
   }

     };
