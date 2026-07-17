#pragma once

#include <Arduino.h>
#include <math.h>
#include <MS5837.h>

#include "Format.h"
#include "TempSensor.h"
#include "ESP32TempSensor.h"
#include "CapacitorSensor.h"
#include "CapacitorDepthSensor.h"

class ESP32TempTestSensor;
class TempSensorTestSensor;
class ConstantTestSensor;
class RandomTestSensor;
class NormalTestSensor;
class SinTestSensor;
class MS5837PressureTestSensor;
class CapacitiveTestSensor;
class DepthTestSensor;

// One-line sensor source switch used by all sketches. Change only TEST_SENSOR_TYPE below;
// TestSensor is derived from it automatically. Use sensor.sensorType() to display the
// active sensor's type name at runtime.
// #define TEST_SENSOR_TYPE TempSensorTestSensor
// #define TEST_SENSOR_TYPE ESP32TempTestSensor
// #define TEST_SENSOR_TYPE SinTestSensor
// #define TEST_SENSOR_TYPE ConstantTestSensor
// #define TEST_SENSOR_TYPE RandomTestSensor
// #define TEST_SENSOR_TYPE NormalTestSensor
#define TEST_SENSOR_TYPE DepthTestSensor
// #define TEST_SENSOR_TYPE MS5837PressureTestSensor
// #define TEST_SENSOR_TYPE CapacitiveTestSensor
// #define TEST_SENSOR_TYPE DepthTestSensor

using TestSensor = TEST_SENSOR_TYPE;

namespace TestSensorConfig
{
   // ----- noise (shared by all mock sensors, via MockTestSensorBase::noiseStdDev)
   static constexpr float NOISE_STDDEV = 1.0f; // applied when the Noise field is set to true
   static constexpr float NOISE_MIN_STDDEV = 0.0f;
   static constexpr float NOISE_MAX_STDDEV = 100.0f;
   static constexpr float NOISE_STDDEV_STEP = 0.1f;

   // ----- constant sensor
   static constexpr uint16_t CONSTANT_SAMPLING_RATE_PER_SEC = 0;
   static constexpr float CONSTANT_VALUE = 100.0f;
   static constexpr float CONSTANT_MIN_VALUE = 0.0f;
   static constexpr float CONSTANT_MAX_VALUE = 1000.0f;
   static constexpr float CONSTANT_STEP = 1.0f;
   static constexpr const char* CONSTANT_FORMAT = "###.#";
   static constexpr const char* CONSTANT_HIGH_RES_FORMAT = "###.##";

   // ----- random sensor
   static constexpr uint16_t RANDOM_SAMPLING_RATE_PER_SEC = 0;
   static constexpr float RANDOM_MIN_VALUE = 90.0f;
   static constexpr float RANDOM_MAX_VALUE = 110.0f;
   static constexpr const char* RANDOM_FORMAT = "###.#";
   static constexpr const char* RANDOM_HIGH_RES_FORMAT = "###.###";

   // ----- normal sensor
   static constexpr uint16_t NORMAL_SAMPLING_RATE_PER_SEC = 0;
   static constexpr float NORMAL_MEAN = 100.0f;
   static constexpr float NORMAL_STDDEV = 3.0f;
   static constexpr const char* NORMAL_FORMAT = "###.##";
   static constexpr const char* NORMAL_HIGH_RES_FORMAT = "###.###";

   // ----- sin sensor
   static constexpr uint16_t SIN_SAMPLING_RATE_PER_SEC = 0;
   static constexpr float SIN_MEAN = 0.0f;
   static constexpr float SIN_AMPLITUDE = 10.0f;
   static constexpr float SIN_PERIOD_S = 10.0f;
   static constexpr float SIN_MIN_PERIOD_S = 1.0f;
   static constexpr float SIN_MAX_PERIOD_S = 60.0f;
   static constexpr float SIN_PERIOD_STEP_S = 1.0f;
   static constexpr const char* SIN_FORMAT = "###.##";
   static constexpr const char* SIN_HIGH_RES_FORMAT = "###.###";
    // Fixed time increment applied per sample when SinTestSensor uses TimeSource::FIXED_STEP,
   // so the wave advances in perfectly even steps regardless of actual loop/sampling jitter.
   static constexpr float SIN_FIXED_STEP_S = 0.05f;
   // Period range/step used while TimeSource::FIXED_STEP is active, expressed in samples
   // (period / SIN_FIXED_STEP_S) rather than seconds, since a fixed-step period doesn't
   // correspond to real elapsed time.
   static constexpr long SIN_FIXED_MIN_PERIOD_SAMPLES = 400;
   static constexpr long SIN_FIXED_MAX_PERIOD_SAMPLES = 2000;
   static constexpr long SIN_FIXED_PERIOD_STEP_SAMPLES = 500;

   // ----- temp sensor (physical sensor)
   static constexpr const char* TEMP_FORMAT = "###.##";
   static constexpr const char* TEMP_HIGH_RES_FORMAT = "###.###";

   // ----- ms5837 pressure sensor
   static const uint8_t MS5837_MODEL = MS5837::MS5837_02BA;
   static constexpr const char* MS5837_FORMAT = "####.##";
   static constexpr const char* MS5837_HIGH_RES_FORMAT = "####.###";

   // ----- capacitive sensor
   // Uses the standard capacitor sensor prototype wiring defined by CapacitorSensor
   // (shared by the Capacitor_Playground, Capacitor_Wiring_Test, and Depth_Display sketches).
   static constexpr uint8_t CAPACITIVE_CHARGE_PIN = CapacitorSensor::CHARGE_PIN_100K;
   static constexpr uint8_t CAPACITIVE_SENSE_PIN = CapacitorSensor::SENSE_PIN;
   static constexpr uint16_t CAPACITIVE_DISCHARGE_DELAY_US = CapacitorSensor::DEFAULT_DISCHARGE_DELAY_MICROS;
   static constexpr uint32_t CAPACITIVE_PROCESS_PERIOD_US = CapacitorSensor::DEFAULT_DEFERRED_PROCESSING_PERIOD_MICROS;
   static constexpr size_t CAPACITIVE_BUFFER_SIZE = CapacitorSensor::DEFAULT_BUFFER_SIZE;
   static constexpr const char* CAPACITIVE_FORMAT = "#########";
   static constexpr const char* CAPACITIVE_HIGH_RES_FORMAT = "##########";

   // ----- depth sensor (capacitive-based)
   static constexpr uint8_t DEPTH_CHARGE_PIN = CAPACITIVE_CHARGE_PIN;
   static constexpr uint8_t DEPTH_SENSE_PIN = CAPACITIVE_SENSE_PIN;
   static constexpr float DEPTH_ZERO_CHARGE_TIME = 128.3f;
   static constexpr float DEPTH_CALIBRATION_CHARGE_TIME = 295.0f;
   static constexpr float DEPTH_CALIBRATION_DEPTH_CM = 45.72f; // 18 inches (half of full depth)
   static constexpr size_t DEPTH_BUFFER_SIZE = 130; // 0 = no averaging (buffer size of 1) during sensor tests
   static constexpr const char* DEPTH_FORMAT = "####.##";
   static constexpr const char* DEPTH_HIGH_RES_FORMAT = "####.###";
}

///
/// <summary>
/// Common test sensor contract used by sketches.
/// </summary>
///
class ITestSensor
{
private:
   Format _format;
   Format _highResFormat;

public:
   ///
   /// <summary>
   /// Initializes the format objects used for value display.
   /// </summary>
   /// <param name="format">Format pattern used for axis limits and current readings.</param>
   /// <param name="highResFormat">Format pattern used for statistics like average, stddev, and range.</param>
   ///
   ITestSensor(const char* format, const char* highResFormat)
      : _format(format), _highResFormat(highResFormat)
   {
   }

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
   const Format* getFormat() const
   {
      return &_format;
   }

   ///
   /// <summary>
   /// Gets the high-resolution format object for values (used for statistics like average, stddev, and range).
   /// Has one more decimal place than getFormat for finer-grained display.
   /// </summary>
   /// <returns>A Format object configured for high-resolution value display.</returns>
   ///
   const Format* getHighResFormat() const
   {
      return &_highResFormat;
   }

   ///
   /// <summary>
   /// Checks whether a new physical measurement has arrived since the last call to get(),
   /// for sensors whose underlying measurement rate can be slower than the polling rate.
   /// Callers can use this to avoid recording duplicate/stale readings when oversampling.
   /// </summary>
   /// <returns>True when a new measurement is available; sensors that always produce a fresh
   /// value on every call (e.g. mock sensors) should return true.</returns>
   ///
   virtual bool hasNewValue()
   {
      return true;
   }

   ///
   /// <summary>
   /// Sets the standard deviation of Gaussian noise added to readings, for sensors that
   /// support it (mock sensors). Sensors that don't support noise (e.g. physical sensors)
   /// ignore this call.
   /// </summary>
   /// <param name="stdDev">Standard deviation of the noise, or 0 to disable it.</param>
   ///
   virtual void setNoiseStdDev(float stdDev)
   {
   }
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

   // Counts how many samples have been produced since begin(), so sensors that support a
   // TimeSource::FIXED_STEP mode can advance by a constant time increment per sample instead
   // of relying on actual elapsed wall-clock time (millis()), which can drift if the loop's
   // real sampling rate varies (e.g. due to rendering/diffing overhead growing over time).
   uint32_t _sampleIndex = 0;

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
      // Equivalent to pow(10, numDecimals), but computed with integer multiplication
      // to guarantee an exact result (pow() can be off by a fraction due to floating-point
      // rounding, which would truncate incorrectly when cast to long).
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
   /// <param name="numDecimals">Number of decimal places used to scale the underlying uniform random draws.</param>
   /// <returns>A normally distributed random value.</returns>
   ///
   float _normalDistributed(float mean, float stddev, uint8_t numDecimals) const
   {
      long randomScale = _decimalScale(numDecimals);
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
   /// Computes a sinusoidal value using an explicit elapsed-time value.
   /// </summary>
   /// <param name="midpoint">Midpoint (mean) value of the sine wave.</param>
   /// <param name="amplitude">Amplitude of the sine wave.</param>
   /// <param name="periodS">Period of the sine wave, in seconds.</param>
   /// <param name="elapsedS">Elapsed time, in seconds, to evaluate the wave at.</param>
   /// <returns>The sine wave value at the given elapsed time.</returns>
   ///
   float _sinValue(float midpoint, float amplitude, float periodS, float elapsedS) const
   {
      if (periodS <= 0.0f)
      {
         return midpoint;
      }

      float radians = (elapsedS / periodS) * (2.0f * PI);
      return midpoint + (sinf(radians) * amplitude);
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
      float elapsedS = static_cast<float>(millis() - _startMs) / 1000.0f;
      return _sinValue(midpoint, amplitude, periodS, elapsedS);
   }

public:
   ///
   /// <summary>
   /// Standard deviation of Gaussian noise added to every reading returned by get(). A value
   /// of 0 (the default) disables noise entirely. Public and mutable so it can be bound
   /// directly to an editable field for live adjustment.
   /// </summary>
   ///
   float noiseStdDev = 0.0f;

   ///
   /// <summary>
   /// Initializes the mock sensor's format objects with the sensor-specific format patterns.
   /// </summary>
   /// <param name="format">Format pattern used for axis limits and current readings.</param>
   /// <param name="highResFormat">Format pattern used for statistics like average, stddev, and range.</param>
   ///
   MockTestSensorBase(const char* format, const char* highResFormat)
      : ITestSensor(format, highResFormat)
   {
   }

   ///
   /// <summary>
   /// Initializes the mock sensor and records its start time.
   /// </summary>
   /// <returns>True when initialization succeeds; otherwise false.</returns>
   ///
   bool begin() override
   {
      _startMs = millis();
      _sampleIndex = 0;
      return _beginImpl();
   }

   ///
   /// <summary>
   /// Gets one rounded, throttled sensor reading, with Gaussian noise (per noiseStdDev)
   /// added before rounding.
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

      float rawValue = _getValue() + _normalDistributed(0.0f, noiseStdDev, 3);
      float value = _roundToDecimals(rawValue, getFormat()->precision());
      _sampleIndex++;
      return value;
   }

   ///
   /// <summary>
   /// Sets the standard deviation of Gaussian noise added to readings.
   /// </summary>
   /// <param name="stdDev">Standard deviation of the noise, or 0 to disable it.</param>
   ///
   void setNoiseStdDev(float stdDev) override
   {
      noiseStdDev = stdDev;
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
   ///
   /// <summary>
   /// Initializes the sensor with its configured format patterns.
   /// </summary>
   ///
   TempSensorTestSensor()
      : ITestSensor(TestSensorConfig::TEMP_FORMAT, TestSensorConfig::TEMP_HIGH_RES_FORMAT)
   {
   }

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

public:
   ///
   /// <summary>
   /// Initializes the sensor with its configured format patterns.
   /// </summary>
   ///
   ESP32TempTestSensor()
      : ITestSensor(TestSensorConfig::TEMP_FORMAT, TestSensorConfig::TEMP_HIGH_RES_FORMAT)
   {
   }

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

   float _getValue() override
   {
      return value;
   }

public:
   ///
   /// <summary>
   /// The constant value returned by this sensor. Public and mutable so it can be bound
   /// directly to an editable field (e.g. DisplayEditableTable) for live adjustment.
   /// </summary>
   ///
   float value = TestSensorConfig::CONSTANT_VALUE;

   ///
   /// <summary>
   /// Initializes the sensor with its configured format patterns.
   /// </summary>
   ///
   ConstantTestSensor()
      : MockTestSensorBase(TestSensorConfig::CONSTANT_FORMAT, TestSensorConfig::CONSTANT_HIGH_RES_FORMAT)
   {
   }

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

   bool _beginImpl() override
   {
      randomSeed(micros());
      return true;
   }

   float _getValue() override
   {
      long decimalScale = _decimalScale(getFormat()->precision());
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
   /// Initializes the sensor with its configured format patterns.
   /// </summary>
   ///
   RandomTestSensor()
      : MockTestSensorBase(TestSensorConfig::RANDOM_FORMAT, TestSensorConfig::RANDOM_HIGH_RES_FORMAT)
   {
   }

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
         getFormat()->precision());
   }

public:
   ///
   /// <summary>
   /// Initializes the sensor with its configured format patterns.
   /// </summary>
   ///
   NormalTestSensor()
      : MockTestSensorBase(TestSensorConfig::NORMAL_FORMAT, TestSensorConfig::NORMAL_HIGH_RES_FORMAT)
   {
   }

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
public:
   ///
   /// <summary>
   /// Selects how the sine wave's elapsed time advances from one sample to the next. Backed
   /// by a plain long (rather than an enum class) so timeSource can be bound directly to an
   /// IntDisplayEditableField for live adjustment.
   /// </summary>
   ///
   enum TimeSource : long
   {
      // Uses actual wall-clock time (millis()) since begin(). Simple and realistic, but the
      // wave's apparent period will stretch or compress if the actual sampling rate varies
      // (e.g. rendering/diffing overhead growing as more points are plotted).
      TIME_SOURCE_CLOCK = 0,

      // Advances by a fixed time increment (TestSensorConfig::SIN_FIXED_STEP_S) each time
      // get() is called, regardless of actual elapsed wall-clock time. Keeps the wave's
      // period visually constant across a run even if the real sampling rate varies.
      TIME_SOURCE_FIXED_STEP = 1
   };

private:
   uint16_t _samplingRatePerSec() const override
   {
      return TestSensorConfig::SIN_SAMPLING_RATE_PER_SEC;
   }

   float _getValue() override
   {
      if (timeSource == TIME_SOURCE_FIXED_STEP)
      {
         float elapsedS = static_cast<float>(_sampleIndex) * TestSensorConfig::SIN_FIXED_STEP_S;
         return _sinValue(
            TestSensorConfig::SIN_MEAN,
            TestSensorConfig::SIN_AMPLITUDE,
            periodS,
            elapsedS);
      }

      return _sinValue(
         TestSensorConfig::SIN_MEAN,
         TestSensorConfig::SIN_AMPLITUDE,
         periodS);
   }

public:
   ///
   /// <summary>
   /// Selects which time source the wave currently uses. Public and mutable so it can be
   /// bound directly to an editable field for live adjustment.
   /// </summary>
   ///
   long timeSource;

   ///
   /// <summary>
   /// The wave's period, in seconds. Public and mutable so it can be bound directly to an
   /// editable field for live adjustment.
   /// </summary>
   ///
   float periodS = TestSensorConfig::SIN_PERIOD_S;

   ///
   /// <summary>
   /// Initializes the sensor with its configured format patterns.
   /// </summary>
   /// <param name="timeSource">Selects whether the wave advances using wall-clock time or a
   /// fixed time increment per sample. Defaults to TIME_SOURCE_CLOCK.</param>
   ///
   explicit SinTestSensor(long timeSource = TIME_SOURCE_CLOCK)
      : MockTestSensorBase(TestSensorConfig::SIN_FORMAT, TestSensorConfig::SIN_HIGH_RES_FORMAT), timeSource(timeSource)
   {
   }

   ///
   /// <summary>
   /// Gets the sensor type name.
   /// </summary>
   /// <returns>The sensor type name.</returns>
   ///
   const char* sensorType() const override
   {
      return "Sin";
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

public:
   ///
   /// <summary>
   /// Initializes the sensor with its configured format patterns.
   /// </summary>
   ///
   MS5837PressureTestSensor()
      : ITestSensor(TestSensorConfig::MS5837_FORMAT, TestSensorConfig::MS5837_HIGH_RES_FORMAT)
   {
   }

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
   uint32_t _lastCounter = 0;

public:
   ///
   /// <summary>
   /// Initializes the sensor with its configured format patterns.
   /// </summary>
   ///
   CapacitiveTestSensor()
      : ITestSensor(TestSensorConfig::CAPACITIVE_FORMAT, TestSensorConfig::CAPACITIVE_HIGH_RES_FORMAT)
   {
   }

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
   /// Checks whether a new physical measurement has been processed since the last check.
   /// </summary>
   /// <returns>True when the sensor's measurement counter has advanced.</returns>
   ///
   bool hasNewValue() override
   {
      uint32_t current = _sensor.counter();
      bool isNew = current != _lastCounter;
      _lastCounter = current;
      return isNew;
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
};

///
/// <summary>
/// Uses the capacitor-based depth sensor as the test sensor source, reporting depth in
/// centimeters (via CapacitorDepthSensor's charge-time-to-depth calibration) rather than the
/// raw charge time reported by CapacitiveTestSensor.
/// </summary>
///
class DepthTestSensor : public ITestSensor
{
private:
   CapacitorDepthSensor _sensor{
      TestSensorConfig::DEPTH_CHARGE_PIN,
      TestSensorConfig::DEPTH_SENSE_PIN,
      TestSensorConfig::DEPTH_ZERO_CHARGE_TIME,
      TestSensorConfig::DEPTH_CALIBRATION_CHARGE_TIME,
      TestSensorConfig::DEPTH_CALIBRATION_DEPTH_CM,
      TestSensorConfig::DEPTH_BUFFER_SIZE };
   uint32_t _lastCounter = 0;

public:
   ///
   /// <summary>
   /// Initializes the sensor with its configured format patterns.
   /// </summary>
   ///
   DepthTestSensor()
      : ITestSensor(TestSensorConfig::DEPTH_FORMAT, TestSensorConfig::DEPTH_HIGH_RES_FORMAT)
   {
   }

   ///
   /// <summary>
   /// Initializes the underlying capacitor depth sensor.
   /// </summary>
   /// <returns>True when initialization succeeds.</returns>
   ///
   bool begin() override
   {
      return _sensor.begin();
   }

   ///
   /// <summary>
   /// Reads one depth sample, in centimeters.
   /// </summary>
   /// <returns>The current depth reading, in centimeters.</returns>
   ///
   float get() override
   {
      return _sensor.getDepth();
   }

   ///
   /// <summary>
   /// Checks whether a new physical measurement has been processed since the last check.
   /// </summary>
   /// <returns>True when the sensor's measurement counter has advanced.</returns>
   ///
   bool hasNewValue() override
   {
      uint32_t current = _sensor.counter();
      bool isNew = current != _lastCounter;
      _lastCounter = current;
      return isNew;
   }

   ///
   /// <summary>
   /// Gets the sensor type name.
   /// </summary>
   /// <returns>The sensor type name.</returns>
   ///
   const char* sensorType() const override
   {
      return "Depth";
   }
};

