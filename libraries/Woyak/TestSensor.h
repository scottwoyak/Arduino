#pragma once

#include <Arduino.h>
#include <math.h>
#include <MS5837.h>

#include "TempSensor.h"
#include "CapacitorSensor.h"

class TempSensorTestSensor;
class ConstantTestSensor;
class RandomTestSensor;
class NormalTestSensor;
class SinTestSensor;
class SinWithNormalNoiseTestSensor;
class MS5837PressureTestSensor;
class CapacitiveTestSensor;

// One-line sensor source switch used by all sketches.
using TestSensor = SinWithNormalNoiseTestSensor;
// using TestSensor = TempSensorTestSensor;
// using TestSensor = ConstantTestSensor;
// using TestSensor = RandomTestSensor;
// using TestSensor = NormalTestSensor;
// using TestSensor = SinTestSensor;
// using TestSensor = SinWithNormalNoiseTestSensor;
// using TestSensor = MS5837PressureTestSensor;
// using TestSensor = CapacitiveTestSensor;

namespace TestSensorConfig
{
   // ----- constant sensor
   static constexpr uint16_t CONSTANT_SAMPLING_RATE_PER_SEC = 0;
   static constexpr uint8_t CONSTANT_NUM_DECIMALS = 3;
   static constexpr float CONSTANT_VALUE = 100.0f;

   // ----- random sensor
   static constexpr uint16_t RANDOM_SAMPLING_RATE_PER_SEC = 0;
   static constexpr uint8_t RANDOM_NUM_DECIMALS = 3;
   static constexpr float RANDOM_MIN_VALUE = 90.0f;
   static constexpr float RANDOM_MAX_VALUE = 110.0f;

   // ----- normal sensor
   static constexpr uint16_t NORMAL_SAMPLING_RATE_PER_SEC = 0;
   static constexpr uint8_t NORMAL_NUM_DECIMALS = 3;
   static constexpr float NORMAL_MEAN = 100.0f;
   static constexpr float NORMAL_STDDEV = 3.0f;

   // ----- sin sensor
   static constexpr uint16_t SIN_SAMPLING_RATE_PER_SEC = 0;
   static constexpr uint8_t SIN_NUM_DECIMALS = 3;
   static constexpr float SIN_MEAN = 0.0f;
   static constexpr float SIN_AMPLITUDE = 10.0f;
   static constexpr float SIN_PERIOD_S = 10.0f;

   // ----- sin with normal noise sensor
   static constexpr uint16_t SIN_NOISE_SAMPLING_RATE_PER_SEC = 100;
   static constexpr uint8_t SIN_NOISE_NUM_DECIMALS = 3;
   static constexpr float SIN_NOISE_MEAN = 0.0f;
   static constexpr float SIN_NOISE_AMPLITUDE = 1.0f;
   static constexpr float SIN_NOISE_PERIOD_S = 10.0f;
   static constexpr float SIN_NOISE_STDDEV = 2.0f;

   // ----- ms5837 pressure sensor
   static const uint8_t MS5837_MODEL = MS5837::MS5837_02BA;

   // ----- capacitive sensor
   // Charge pin to resistor mapping used in capacitor calibration/wiring sketches:
   // - 6  => 1M
   // - 9  => 470K
   // - 10 => 100K
   // - 11 => 47K
   // Alternate sense pin used in calibration/wiring sketches: 5
   static constexpr uint8_t CAPACITIVE_CHARGE_PIN = 14;
   static constexpr uint8_t CAPACITIVE_SENSE_PIN = 27;
   static constexpr uint16_t CAPACITIVE_DISCHARGE_DELAY_US = CapacitorSensor::DEFAULT_DISCHARGE_DELAY_MICROS;
   static constexpr uint32_t CAPACITIVE_PROCESS_PERIOD_US = CapacitorSensor::DEFAULT_DEFERRED_PROCESSING_PERIOD_MICROS;
   static constexpr size_t CAPACITIVE_BUFFER_SIZE = CapacitorSensor::DEFAULT_BUFFER_SIZE;
}

///
/// <summary>
/// Common test sensor contract used by sketches.
/// </summary>
///
class ITestSensor
{
public:
   virtual ~ITestSensor() = default;

   /// <summary>
   /// Initializes the sensor instance.
   /// </summary>
   /// <returns>True when initialization succeeds; otherwise false.</returns>
   virtual bool begin() = 0;

   /// <summary>
   /// Gets one sensor reading.
   /// </summary>
   /// <returns>The current reading for this sensor implementation.</returns>
   virtual float get() = 0;
};

///
/// <summary>
/// Shared timing/distribution behavior for mock test sensor implementations.
/// </summary>
///
class MockTestSensorBase : public ITestSensor
{
protected:
   unsigned long _startMs = 0;

   uint16_t _throttleDelayMs(uint16_t samplingRatePerSec) const
   {
      if (samplingRatePerSec == 0)
      {
         return 0;
      }

      uint16_t delayMs = 1000U / samplingRatePerSec;
      return (delayMs == 0) ? 1 : delayMs;
   }

   virtual uint16_t _samplingRatePerSec() const
   {
      return 0;
   }

   virtual bool _beginImpl()
   {
      return true;
   }

   virtual uint8_t _numDecimals() const
   {
      return 3;
   }

   virtual float _getValue() = 0;

   long _decimalScale(uint8_t numDecimals) const
   {
      long scale = 1;
      for (uint8_t decimalIndex = 0; decimalIndex < numDecimals; decimalIndex++)
      {
         scale *= 10;
      }

      return scale;
   }

   float _roundToDecimals(float value, uint8_t numDecimals) const
   {
      long scale = _decimalScale(numDecimals);
      return roundf(value * static_cast<float>(scale)) / static_cast<float>(scale);
   }

   float _normalDistributed(float mean, float stddev, long randomScale) const
   {
      if ((stddev <= 0.0f) || (randomScale <= 1))
      {
         return mean;
      }

      float u1 = static_cast<float>(random(1, randomScale + 1)) / randomScale;
      float u2 = static_cast<float>(random(0, randomScale)) / randomScale;

      float magnitude = sqrtf(-2.0f * logf(u1));
      float phase = 2.0f * PI * u2;
      float z0 = magnitude * cosf(phase);
      return mean + (z0 * stddev);
   }

   float _sinValue(float midpoint, float amplitude, float periodS) const
   {
      float periodMs = periodS * 1000.0f;
      if (periodMs <= 0.0f)
      {
         return midpoint;
      }

      float elapsedMs = static_cast<float>(millis() - _startMs);
      float radians = (elapsedMs / periodMs) * (2.0f * PI);
      return midpoint + (sinf(radians) * amplitude);
   }

public:
   bool begin() override
   {
      _startMs = millis();
      return _beginImpl();
   }

   float get() override
   {
      uint16_t delayMs = _throttleDelayMs(_samplingRatePerSec());
      if (delayMs > 0)
      {
         delay(delayMs);
      }

      return _roundToDecimals(_getValue(), _numDecimals());
   }
};

///
/// <summary>
/// Uses the physical temperature sensor source.
/// </summary>
///
class TempSensorTestSensor : public ITestSensor
{
private:
   TempSensor _sensor;

public:
   /// <summary>
   /// Initializes the physical temperature sensor.
   /// </summary>
   /// <returns>True when sensor initialization succeeds; otherwise false.</returns>
   bool begin() override
   {
      return _sensor.begin();
   }

   /// <summary>
   /// Reads one temperature sample in Fahrenheit.
   /// </summary>
   /// <returns>The measured temperature in Fahrenheit.</returns>
   float get() override
   {
      return _sensor.readTemperatureF();
   }
};

///
/// <summary>
/// Returns a fixed constant value for repeatable tests.
/// </summary>
///
class ConstantTestSensor : public MockTestSensorBase
{
private:
   uint16_t _samplingRatePerSec() const override
   {
      return TestSensorConfig::CONSTANT_SAMPLING_RATE_PER_SEC;
   }

   uint8_t _numDecimals() const override
   {
      return TestSensorConfig::CONSTANT_NUM_DECIMALS;
   }

   float _getValue() override
   {
      return TestSensorConfig::CONSTANT_VALUE;
   }
};

///
/// <summary>
/// Returns uniformly distributed random values within a configured range.
/// </summary>
///
class RandomTestSensor : public MockTestSensorBase
{
private:
   uint16_t _samplingRatePerSec() const override
   {
      return TestSensorConfig::RANDOM_SAMPLING_RATE_PER_SEC;
   }

   uint8_t _numDecimals() const override
   {
      return TestSensorConfig::RANDOM_NUM_DECIMALS;
   }

   bool _beginImpl() override
   {
      randomSeed(micros());
      return true;
   }

   float _getValue() override
   {
      long decimalScale = _decimalScale(TestSensorConfig::RANDOM_NUM_DECIMALS);
      long span = static_cast<long>((TestSensorConfig::RANDOM_MAX_VALUE - TestSensorConfig::RANDOM_MIN_VALUE) * decimalScale);
      if (span <= 0)
      {
         return TestSensorConfig::RANDOM_MIN_VALUE;
      }

      long offset = random(0, span + 1);
      return TestSensorConfig::RANDOM_MIN_VALUE + (static_cast<float>(offset) / static_cast<float>(decimalScale));
   }
};

///
/// <summary>
/// Returns normally distributed random values using Box-Muller transform.
/// </summary>
///
class NormalTestSensor : public MockTestSensorBase
{
private:
   uint16_t _samplingRatePerSec() const override
   {
      return TestSensorConfig::NORMAL_SAMPLING_RATE_PER_SEC;
   }

   uint8_t _numDecimals() const override
   {
      return TestSensorConfig::NORMAL_NUM_DECIMALS;
   }

   bool _beginImpl() override
   {
      randomSeed(micros());
      return true;
   }

   float _getValue() override
   {
      return _normalDistributed(
         TestSensorConfig::NORMAL_MEAN,
         TestSensorConfig::NORMAL_STDDEV,
         _decimalScale(TestSensorConfig::NORMAL_NUM_DECIMALS));
   }
};

///
/// <summary>
/// Returns a deterministic sinusoidal signal.
/// </summary>
///
class SinTestSensor : public MockTestSensorBase
{
private:
   uint16_t _samplingRatePerSec() const override
   {
      return TestSensorConfig::SIN_SAMPLING_RATE_PER_SEC;
   }

   uint8_t _numDecimals() const override
   {
      return TestSensorConfig::SIN_NUM_DECIMALS;
   }

   float _getValue() override
   {
      return _sinValue(
         TestSensorConfig::SIN_MEAN,
         TestSensorConfig::SIN_AMPLITUDE,
         TestSensorConfig::SIN_PERIOD_S);
   }
};

///
/// <summary>
/// Returns a sinusoidal signal with additive normal-distributed noise.
/// </summary>
///
class SinWithNormalNoiseTestSensor : public MockTestSensorBase
{
private:
   uint16_t _samplingRatePerSec() const override
   {
      return TestSensorConfig::SIN_NOISE_SAMPLING_RATE_PER_SEC;
   }

   uint8_t _numDecimals() const override
   {
      return TestSensorConfig::SIN_NOISE_NUM_DECIMALS;
   }

   bool _beginImpl() override
   {
      randomSeed(micros());
      return true;
   }

   float _getValue() override
   {
      float sinValue = _sinValue(
         TestSensorConfig::SIN_NOISE_MEAN,
         TestSensorConfig::SIN_NOISE_AMPLITUDE,
         TestSensorConfig::SIN_NOISE_PERIOD_S);
      if (TestSensorConfig::SIN_NOISE_STDDEV <= 0.0f)
      {
         return sinValue;
      }

      float noise = _normalDistributed(
         0.0f,
         TestSensorConfig::SIN_NOISE_STDDEV,
         _decimalScale(TestSensorConfig::SIN_NOISE_NUM_DECIMALS));
      return sinValue + noise;
   }
};

///
/// <summary>
/// Uses MS5837 pressure as the test sensor source.
/// </summary>
///
class MS5837PressureTestSensor : public ITestSensor
{
private:
   /// <summary>
   /// Gets the MS5837 model configuration.
   /// </summary>
   /// <returns>The MS5837 model identifier.</returns>
   static uint8_t _ms5837Model()
   {
      return TestSensorConfig::MS5837_MODEL;
   }

   MS5837 _sensor;

public:
   /// <summary>
   /// Initializes the MS5837 pressure sensor.
   /// </summary>
   /// <returns>True when sensor initialization succeeds; otherwise false.</returns>
   bool begin() override
   {
      _sensor.setModel(_ms5837Model());
      return _sensor.init();
   }

   /// <summary>
   /// Reads one pressure sample from MS5837.
   /// </summary>
   /// <returns>The current pressure sample.</returns>
   float get() override
   {
      _sensor.read();
      return _sensor.pressure();
   }
};

///
/// <summary>
/// Uses the capacitive sensor source for timing-based charge measurements.
/// </summary>
///
class CapacitiveTestSensor : public ITestSensor
{
private:
   CapacitorSensor _sensor{
      TestSensorConfig::CAPACITIVE_CHARGE_PIN,
      TestSensorConfig::CAPACITIVE_SENSE_PIN,
      TestSensorConfig::CAPACITIVE_DISCHARGE_DELAY_US,
      TestSensorConfig::CAPACITIVE_BUFFER_SIZE };

public:
   /// <summary>
   /// Initializes the capacitive sensor source.
   /// </summary>
   /// <returns>True when initialization succeeds.</returns>
   bool begin() override
   {
      _sensor.setDeferredProcessingPeriodMicros(TestSensorConfig::CAPACITIVE_PROCESS_PERIOD_US);
      _sensor.begin();
      return true;
   }

   /// <summary>
   /// Reads one capacitive charge-time sample.
   /// </summary>
   /// <returns>The measured charge time in micros.</returns>
   float get() override
   {
      return _sensor.chargeTimeMicros();
   }
};

