#pragma once

#include <Arduino.h>
#include <math.h>
#include <MS5837.h>

#include "Format.h"
#include "TempSensor.h"
#include "ESP32TempSensor.h"
#include "CapacitorSensor.h"

class ESP32TempTestSensor;
class TempSensorTestSensor;
class ConstantTestSensor;
class RandomTestSensor;
class NormalTestSensor;
class SinTestSensor;
class SinWithNormalNoiseTestSensor;
class MS5837PressureTestSensor;
class CapacitiveTestSensor;

// One-line sensor source switch used by all sketches. Change only TEST_SENSOR_TYPE below;
// TestSensor is derived from it automatically. Use sensor.sensorType() to display the
// active sensor's type name at runtime.
// #define TEST_SENSOR_TYPE TempSensorTestSensor
// #define TEST_SENSOR_TYPE ESP32TempTestSensor
// #define TEST_SENSOR_TYPE SinWithNormalNoiseTestSensor
// #define TEST_SENSOR_TYPE SinTestSensor
// #define TEST_SENSOR_TYPE ConstantTestSensor
// #define TEST_SENSOR_TYPE RandomTestSensor
// #define TEST_SENSOR_TYPE NormalTestSensor
#define TEST_SENSOR_TYPE MS5837PressureTestSensor
// #define TEST_SENSOR_TYPE CapacitiveTestSensor

using TestSensor = TEST_SENSOR_TYPE;

namespace TestSensorConfig
{
   // ----- constant sensor
   static constexpr uint16_t CONSTANT_SAMPLING_RATE_PER_SEC = 0;
   static constexpr uint8_t CONSTANT_NUM_DECIMALS = 3;
   static constexpr float CONSTANT_VALUE = 100.0f;
   static constexpr const char* CONSTANT_FORMAT = "##.#";
   static constexpr const char* CONSTANT_HIGH_RES_FORMAT = "##.##";

   // ----- random sensor
   static constexpr uint16_t RANDOM_SAMPLING_RATE_PER_SEC = 0;
   static constexpr uint8_t RANDOM_NUM_DECIMALS = 3;
   static constexpr float RANDOM_MIN_VALUE = 90.0f;
   static constexpr float RANDOM_MAX_VALUE = 110.0f;
   static constexpr const char* RANDOM_FORMAT = "###.##";
   static constexpr const char* RANDOM_HIGH_RES_FORMAT = "###.###";

   // ----- normal sensor
   static constexpr uint16_t NORMAL_SAMPLING_RATE_PER_SEC = 0;
   static constexpr uint8_t NORMAL_NUM_DECIMALS = 3;
   static constexpr float NORMAL_MEAN = 100.0f;
   static constexpr float NORMAL_STDDEV = 3.0f;
   static constexpr const char* NORMAL_FORMAT = "###.##";
   static constexpr const char* NORMAL_HIGH_RES_FORMAT = "###.###";

   // ----- sin sensor
   static constexpr uint16_t SIN_SAMPLING_RATE_PER_SEC = 0;
   static constexpr uint8_t SIN_NUM_DECIMALS = 3;
   static constexpr float SIN_MEAN = 0.0f;
   static constexpr float SIN_AMPLITUDE = 10.0f;
   static constexpr float SIN_PERIOD_S = 10.0f;
   static constexpr const char* SIN_FORMAT = "###.##";
   static constexpr const char* SIN_HIGH_RES_FORMAT = "###.###";

   // ----- sin with normal noise sensor
   static constexpr uint16_t SIN_NOISE_SAMPLING_RATE_PER_SEC = 100;
   static constexpr uint8_t SIN_NOISE_NUM_DECIMALS = 3;
   static constexpr float SIN_NOISE_MEAN = 0.0f;
   static constexpr float SIN_NOISE_AMPLITUDE = 1.0f;
   static constexpr float SIN_NOISE_PERIOD_S = 10.0f;
   static constexpr float SIN_NOISE_STDDEV = 2.0f;
   static constexpr const char* SIN_NOISE_FORMAT = "###.##";
   static constexpr const char* SIN_NOISE_HIGH_RES_FORMAT = "###.###";

   // ----- temp sensor (physical sensor)
   static constexpr const char* TEMP_FORMAT = "###.##";
   static constexpr const char* TEMP_HIGH_RES_FORMAT = "###.###";

   // ----- ms5837 pressure sensor
   static const uint8_t MS5837_MODEL = MS5837::MS5837_02BA;
   static constexpr const char* MS5837_FORMAT = "####.##";
   static constexpr const char* MS5837_HIGH_RES_FORMAT = "####.###";

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
   static constexpr const char* CAPACITIVE_FORMAT = "#########";
   static constexpr const char* CAPACITIVE_HIGH_RES_FORMAT = "##########";
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

   ///
   /// <summary>
   /// Initializes the sensor instance.
   /// </summary>
   /// <returns>True when initialization succeeds; otherwise false.</returns>
   ///
   virtual bool begin() = 0;

   ///
   /// <summary>
   /// Gets one sensor reading.
   /// </summary>
   /// <returns>The current reading for this sensor implementation.</returns>
   ///
   virtual float get() = 0;

   ///
   /// <summary>
   /// Gets the sensor type name, for display/diagnostic purposes.
   /// </summary>
   /// <returns>The sensor type name.</returns>
   ///
   virtual const char* sensorType() const = 0;

   ///
   /// <summary>
   /// Gets the format object for values (used for axis limits and current readings).
   /// </summary>
   /// <returns>A Format object configured for value display.</returns>
   ///
   virtual const Format& getFormat() const = 0;

   ///
   /// <summary>
   /// Gets the high-resolution format object for values (used for statistics like average, stddev, and range).
   /// Has one more decimal place than getFormat for finer-grained display.
   /// </summary>
   /// <returns>A Format object configured for high-resolution value display.</returns>
   ///
   virtual const Format& getHighResFormat() const = 0;
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
   Format* _format = nullptr;
   Format* _highResFormat = nullptr;

   ///
   /// <summary>
   /// Initializes the format objects based on the sensor's decimal precision.
   /// Must be called by derived class constructors.
   /// </summary>
   ///
   void _initializeFormats()
   {
      if (_format == nullptr)
      {
         uint8_t numDecimals = _numDecimals();
         String pattern = "####.";
         for (uint8_t i = 0; i < numDecimals; i++)
         {
            pattern += "#";
         }
         _format = new (std::nothrow) Format(pattern.c_str());
      }

      if (_highResFormat == nullptr)
      {
         uint8_t numDecimals = _numDecimals() + 1;
         String pattern = "####.";
         for (uint8_t i = 0; i < numDecimals; i++)
         {
            pattern += "#";
         }
         _highResFormat = new (std::nothrow) Format(pattern.c_str());
      }
   }

   ///
   /// <summary>
   /// Computes the delay between samples for a given sampling rate.
   /// </summary>
   /// <param name="samplingRatePerSec">Desired samples per second, or 0 for no throttling.</param>
   /// <returns>The delay in milliseconds between samples, or 0 when unthrottled.</returns>
   ///
   uint16_t _throttleDelayMs(uint16_t samplingRatePerSec) const
   {
      if (samplingRatePerSec == 0)
      {
         return 0;
      }

      uint16_t delayMs = 1000U / samplingRatePerSec;
      return (delayMs == 0) ? 1 : delayMs;
   }

   ///
   /// <summary>
   /// Gets the sampling rate used to throttle readings.
   /// </summary>
   /// <returns>Samples per second, or 0 for no throttling.</returns>
   ///
   virtual uint16_t _samplingRatePerSec() const
   {
      return 0;
   }

   ///
   /// <summary>
   /// Performs sensor-specific initialization.
   /// </summary>
   /// <returns>True when initialization succeeds; otherwise false.</returns>
   ///
   virtual bool _beginImpl()
   {
      return true;
   }

   ///
   /// <summary>
   /// Gets the number of decimal places to round readings to.
   /// </summary>
   /// <returns>The number of decimal places.</returns>
   ///
   virtual uint8_t _numDecimals() const
   {
      return 3;
   }

   ///
   /// <summary>
   /// Gets one raw, unrounded sensor reading.
   /// </summary>
   /// <returns>The raw sensor value.</returns>
   ///
   virtual float _getValue() = 0;

   ///
   /// <summary>
   /// Computes the power-of-ten scale factor for a given number of decimal places.
   /// </summary>
   /// <param name="numDecimals">Number of decimal places.</param>
   /// <returns>The scale factor (e.g. 3 decimals => 1000).</returns>
   ///
   long _decimalScale(uint8_t numDecimals) const
   {
      long scale = 1;
      for (uint8_t decimalIndex = 0; decimalIndex < numDecimals; decimalIndex++)
      {
         scale *= 10;
      }

      return scale;
   }

   ///
   /// <summary>
   /// Rounds a value to a specified number of decimal places.
   /// </summary>
   /// <param name="value">The value to round.</param>
   /// <param name="numDecimals">Number of decimal places to round to.</param>
   /// <returns>The rounded value.</returns>
   ///
   float _roundToDecimals(float value, uint8_t numDecimals) const
   {
      long scale = _decimalScale(numDecimals);
      return roundf(value * static_cast<float>(scale)) / static_cast<float>(scale);
   }

   ///
   /// <summary>
   /// Generates a normally distributed random value using the Box-Muller transform.
   /// </summary>
   /// <param name="mean">Mean of the distribution.</param>
   /// <param name="stddev">Standard deviation of the distribution.</param>
   /// <param name="randomScale">Scale used for the underlying uniform random draws.</param>
   /// <returns>A normally distributed random value.</returns>
   ///
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

   ///
   /// <summary>
   /// Computes a sinusoidal value based on elapsed time since sensor start.
   /// </summary>
   /// <param name="midpoint">Midpoint (mean) value of the sine wave.</param>
   /// <param name="amplitude">Amplitude of the sine wave.</param>
   /// <param name="periodS">Period of the sine wave, in seconds.</param>
   /// <returns>The current sine wave value.</returns>
   ///
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
   ///
   /// <summary>
   /// Initializes the mock sensor and records its start time.
   /// </summary>
   /// <returns>True when initialization succeeds; otherwise false.</returns>
   ///
   bool begin() override
   {
      _startMs = millis();
      return _beginImpl();
   }

   ///
   /// <summary>
   /// Gets one rounded, throttled sensor reading.
   /// </summary>
   /// <returns>The current reading, rounded to the configured decimal places.</returns>
   ///
   float get() override
   {
      uint16_t delayMs = _throttleDelayMs(_samplingRatePerSec());
      if (delayMs > 0)
      {
         delay(delayMs);
      }

      return _roundToDecimals(_getValue(), _numDecimals());
   }

   ///
   /// <summary>
   /// Gets the format object for values (axis limits and current readings).
   /// </summary>
   /// <returns>A Format object configured for value display.</returns>
   ///
   const Format& getFormat() const override
   {
      if (_format == nullptr)
      {
         const_cast<MockTestSensorBase*>(this)->_initializeFormats();
      }
      return *_format;
   }

   ///
   /// <summary>
   /// Gets the high-resolution format object for values (statistics display).
   /// Has one more decimal place than getFormat.
   /// </summary>
   /// <returns>A Format object configured for high-resolution value display.</returns>
   ///
   const Format& getHighResFormat() const override
   {
      if (_highResFormat == nullptr)
      {
         const_cast<MockTestSensorBase*>(this)->_initializeFormats();
      }
      return *_highResFormat;
   }

   ~MockTestSensorBase() override
   {
      delete _format;
      delete _highResFormat;
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
   Format* _format = nullptr;
   Format* _highResFormat = nullptr;

public:
   ///
   /// <summary>
   /// Initializes the physical temperature sensor.
   /// </summary>
   /// <returns>True when sensor initialization succeeds; otherwise false.</returns>
   ///
   bool begin() override
   {
      return _sensor.begin();
   }

   ///
   /// <summary>
   /// Reads one temperature sample in Fahrenheit.
   /// </summary>
   /// <returns>The measured temperature in Fahrenheit.</returns>
   ///
   float get() override
   {
      return _sensor.readTemperatureF();
   }

   ///
   /// <summary>
   /// Gets the actual detected physical sensor type (e.g. DS18B20), rather than this
   /// wrapper class own name, since the physical sensor is auto-detected at runtime.
   /// </summary>
   /// <returns>The detected sensor type name.</returns>
   ///
   const char* sensorType() const override
   {
      return _sensor.type();
   }

   ///
   /// <summary>
   /// Gets the format object for values.
   /// </summary>
   /// <returns>A Format object configured for value display.</returns>
   ///
   const Format& getFormat() const override
   {
      if (_format == nullptr)
      {
         const_cast<TempSensorTestSensor*>(this)->_format = new (std::nothrow) Format(TestSensorConfig::TEMP_FORMAT);
      }
      return *_format;
   }

   ///
   /// <summary>
   /// Gets the high-resolution format object for values.
   /// </summary>
   /// <returns>A Format object configured for high-resolution value display.</returns>
   ///
   const Format& getHighResFormat() const override
   {
      if (_highResFormat == nullptr)
      {
         const_cast<TempSensorTestSensor*>(this)->_highResFormat = new (std::nothrow) Format(TestSensorConfig::TEMP_HIGH_RES_FORMAT);
      }
      return *_highResFormat;
   }

   ~TempSensorTestSensor() override
   {
      delete _format;
      delete _highResFormat;
   }
};

///
/// <summary>
/// Uses the internal ESP32 CPU temperature sensor.
/// </summary>
///
class ESP32TempTestSensor : public ITestSensor
{
private:
   ESP32TempSensor _sensor;
   Format* _format = nullptr;
   Format* _highResFormat = nullptr;

public:
   ///
   /// <summary>
   /// Initializes the internal ESP32 temperature sensor.
   /// </summary>
   /// <returns>True when sensor initialization succeeds; otherwise false.</returns>
   ///
   bool begin() override
   {
      return _sensor.begin();
   }

   ///
   /// <summary>
   /// Reads one temperature sample in Fahrenheit.
   /// </summary>
   /// <returns>The internal CPU measured temperature in Fahrenheit.</returns>
   ///
   float get() override
   {
      return _sensor.readTemperatureF();
   }

   ///
   /// <summary>
   /// Gets the sensor type name.
   /// </summary>
   /// <returns>The sensor type name.</returns>
   ///
   const char* sensorType() const override
   {
      return "ESP32 CPU Temp";
   }

   ///
   /// <summary>
   /// Gets the format object for values.
   /// </summary>
   /// <returns>A Format object configured for value display.</returns>
   ///
   const Format& getFormat() const override
   {
      if (_format == nullptr)
      {
         const_cast<ESP32TempTestSensor*>(this)->_format = new (std::nothrow) Format(TestSensorConfig::TEMP_FORMAT);
      }
      return *_format;
   }

   ///
   /// <summary>
   /// Gets the high-resolution format object for values.
   /// </summary>
   /// <returns>A Format object configured for high-resolution value display.</returns>
   ///
   const Format& getHighResFormat() const override
   {
      if (_highResFormat == nullptr)
      {
         const_cast<ESP32TempTestSensor*>(this)->_highResFormat = new (std::nothrow) Format(TestSensorConfig::TEMP_HIGH_RES_FORMAT);
      }
      return *_highResFormat;
   }

   ~ESP32TempTestSensor() override
   {
      delete _format;
      delete _highResFormat;
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

public:
   ///
   /// <summary>
   /// Gets the sensor type name.
   /// </summary>
   /// <returns>The sensor type name.</returns>
   ///
   const char* sensorType() const override
   {
      return "Constant";
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

public:
   ///
   /// <summary>
   /// Gets the sensor type name.
   /// </summary>
   /// <returns>The sensor type name.</returns>
   ///
   const char* sensorType() const override
   {
      return "Random";
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

public:
   ///
   /// <summary>
   /// Gets the sensor type name.
   /// </summary>
   /// <returns>The sensor type name.</returns>
   ///
   const char* sensorType() const override
   {
      return "Normal";
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

public:
   ///
   /// <summary>
   /// Gets the sensor type name.
   /// </summary>
   /// <returns>The sensor type name.</returns>
   ///
   const char* sensorType() const override
   {
      return "Sine";
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

public:
   ///
   /// <summary>
   /// Gets the sensor type name.
   /// </summary>
   /// <returns>The sensor type name.</returns>
   ///
   const char* sensorType() const override
   {
      return "Sine + Noise";
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
   ///
   /// <summary>
   /// Gets the MS5837 model configuration.
   /// </summary>
   /// <returns>The MS5837 model identifier.</returns>
   ///
   static uint8_t _ms5837Model()
   {
      return TestSensorConfig::MS5837_MODEL;
   }

   MS5837 _sensor;
   Format* _format = nullptr;
   Format* _highResFormat = nullptr;

public:
   ///
   /// <summary>
   /// Initializes the MS5837 pressure sensor.
   /// </summary>
   /// <returns>True when sensor initialization succeeds; otherwise false.</returns>
   ///
   bool begin() override
   {
      _sensor.setModel(_ms5837Model());
      return _sensor.init();
   }

   ///
   /// <summary>
   /// Reads one pressure sample from MS5837.
   /// </summary>
   /// <returns>The current pressure sample.</returns>
   ///
   float get() override
   {
      _sensor.read();
      return _sensor.pressure();
   }

   ///
   /// <summary>
   /// Gets the sensor type name.
   /// </summary>
   /// <returns>The sensor type name.</returns>
   ///
       const char* sensorType() const override
       {
          return "MS5837 Pressure";
       }

       ///
       /// <summary>
       /// Gets the format object for values.
       /// </summary>
       /// <returns>A Format object configured for value display.</returns>
       ///
       const Format& getFormat() const override
       {
          if (_format == nullptr)
          {
             const_cast<MS5837PressureTestSensor*>(this)->_format = new (std::nothrow) Format(TestSensorConfig::MS5837_FORMAT);
          }
          return *_format;
       }

       ///
       /// <summary>
       /// Gets the high-resolution format object for values.
       /// </summary>
       /// <returns>A Format object configured for high-resolution value display.</returns>
       ///
       const Format& getHighResFormat() const override
       {
          if (_highResFormat == nullptr)
          {
             const_cast<MS5837PressureTestSensor*>(this)->_highResFormat = new (std::nothrow) Format(TestSensorConfig::MS5837_HIGH_RES_FORMAT);
          }
          return *_highResFormat;
       }

       ~MS5837PressureTestSensor() override
       {
          delete _format;
          delete _highResFormat;
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
   Format* _format = nullptr;
   Format* _highResFormat = nullptr;

public:
   ///
   /// <summary>
   /// Initializes the capacitive sensor source.
   /// </summary>
   /// <returns>True when initialization succeeds.</returns>
   ///
   bool begin() override
   {
      _sensor.setDeferredProcessingPeriodMicros(TestSensorConfig::CAPACITIVE_PROCESS_PERIOD_US);
      _sensor.begin();
      return true;
   }

   ///
   /// <summary>
   /// Reads one capacitive charge-time sample.
   /// </summary>
   /// <returns>The measured charge time in micros.</returns>
   ///
   float get() override
   {
      return _sensor.chargeTimeMicros();
   }

   ///
   /// <summary>
   /// Gets the sensor type name.
   /// </summary>
   /// <returns>The sensor type name.</returns>
   ///
   const char* sensorType() const override
   {
      return "Capacitive";
   }

   ///
   /// <summary>
   /// Gets the format object for values.
   /// </summary>
   /// <returns>A Format object configured for value display.</returns>
   ///
   const Format& getFormat() const override
   {
      if (_format == nullptr)
      {
         const_cast<CapacitiveTestSensor*>(this)->_format = new (std::nothrow) Format(TestSensorConfig::CAPACITIVE_FORMAT);
      }
      return *_format;
   }

   ///
   /// <summary>
   /// Gets the high-resolution format object for values.
   /// </summary>
   /// <returns>A Format object configured for high-resolution value display.</returns>
   ///
   const Format& getHighResFormat() const override
   {
      if (_highResFormat == nullptr)
      {
         const_cast<CapacitiveTestSensor*>(this)->_highResFormat = new (std::nothrow) Format(TestSensorConfig::CAPACITIVE_HIGH_RES_FORMAT);
      }
      return *_highResFormat;
   }

   ~CapacitiveTestSensor() override
   {
      delete _format;
      delete _highResFormat;
   }
};

