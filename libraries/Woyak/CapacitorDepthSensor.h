#pragma once

#include "CapacitorSensor.h"
#include "IDepthSensor.h"

/// <summary>
/// Converts capacitor charge timing into a depth value using linear interpolation.
/// </summary>
class CapacitorDepthSensor : public IDepthSensor
{
private:
   CapacitorSensor* _sensor;
   float _zeroTime;
   float _calibrationTime;
   float _calibrationDepth;
   float _fullDepth;
   float _baselineCm;

public:
   /// <summary>
   /// Initializes a depth sensor wrapper around the capacitor timing sensor.
   /// </summary>
   /// <param name="chargePin">GPIO pin used to charge the sensor through the resistor network.</param>
   /// <param name="sensePin">GPIO pin used to detect the rising threshold.</param>
   /// <param name="zeroTime">Charge time (microseconds) measured at zero depth (in air).</param>
   /// <param name="calibrationTime">Charge time (microseconds) measured at calibrationDepth.</param>
   /// <param name="calibrationDepth">Depth value corresponding to calibrationTime (e.g. half of fullDepth).</param>
   /// <param name="fullDepth">Full-scale depth value used to size the depth bar/UI; the charge-time-to-depth mapping is linear and is not clamped to this value.</param>
   /// <param name="bufferSize">Rolling average window size used by the underlying capacitor sensor. A
   /// value of 0 is treated as 1, i.e. no averaging is performed and the latest sample is used directly.</param>
   CapacitorDepthSensor(
      uint8_t chargePin,
      uint8_t sensePin,
      float zeroTime,
      float calibrationTime,
      float calibrationDepth,
      float fullDepth,
      size_t bufferSize = CapacitorSensor::DEFAULT_BUFFER_SIZE)
   {
      _sensor = new CapacitorSensor(chargePin, sensePin, CapacitorSensor::DEFAULT_DISCHARGE_DELAY_MICROS, bufferSize);
      _zeroTime = zeroTime;
      _calibrationTime = calibrationTime;
      _calibrationDepth = calibrationDepth;
      _fullDepth = fullDepth;
      _baselineCm = 0.0f;
   }

   ~CapacitorDepthSensor()
   {
      delete _sensor;
   }

   /// <summary>
   /// Gets the latest computed depth, in centimeters, relative to the current baseline.
   /// </summary>
   /// <returns>The interpolated depth minus the configured baseline.</returns>
   float getDepth() override
   {
      return rawDepthCm() - _baselineCm;
   }

   /// <summary>
   /// Gets the latest computed raw depth based on the current capacitor charge time, unaffected
   /// by baseline. The charge-time-to-depth mapping is linear, derived from the zero-depth and
   /// calibration-depth points, and is not clamped to fullDepth.
   /// </summary>
   /// <returns>The interpolated depth value.</returns>
   float rawDepthCm()
   {
      float span = _calibrationTime - _zeroTime;
      if (span == 0.0f)
      {
         return 0.0f;
      }

      float chargeTime = _sensor->chargeTimeMicros();
      float depth = _calibrationDepth * ((chargeTime - _zeroTime) / span);
      return depth;
   }

   /// <summary>
   /// Sets the baseline depth that is subtracted from raw readings by getDepth().
   /// </summary>
   /// <param name="baselineCm">The new baseline depth.</param>
   void setBaseline(float baselineCm)
   {
      _baselineCm = baselineCm;
   }

   /// <summary>
   /// Gets the currently configured baseline depth.
   /// </summary>
   /// <returns>The current baseline depth.</returns>
   float getBaseline() const
   {
      return _baselineCm;
   }

   /// <summary>
   /// Starts sensor timing and interrupt processing.
   /// </summary>
   /// <returns>Always true; the capacitor sensor has no failure mode to report.</returns>
   bool begin() override
   {
      _sensor->begin();
      return true;
   }

   /// <summary>
   /// Returns the raw measured charge time in microseconds.
   /// </summary>
   /// <returns>The latest charge time measurement.</returns>
   float chargeTimeMicros()
   {
      return _sensor->chargeTimeMicros();
   }

   /// <summary>
   /// Updates depth calibration points.
   /// </summary>
   /// <param name="zeroTime">Charge time measured at zero depth (in air).</param>
   /// <param name="calibrationTime">Charge time measured at calibrationDepth.</param>
   void setCalibration(float zeroTime, float calibrationTime)
   {
      _zeroTime = zeroTime;
      _calibrationTime = calibrationTime;
   }

   /// <summary>
   /// Sets the capacitor sensor discharge delay in microseconds.
   /// </summary>
   /// <param name="dischargeDelayMicros">Discharge delay in microseconds.</param>
   void setDischargeDelayMicros(uint16_t dischargeDelayMicros)
   {
      _sensor->setDischargeDelayMicros(dischargeDelayMicros);
   }

   /// <summary>
   /// Sets the deferred processing period in microseconds.
   /// </summary>
   /// <param name="deferredProcessingPeriodMicros">Deferred processing period in microseconds.</param>
   void setDeferredProcessingPeriodMicros(uint32_t deferredProcessingPeriodMicros)
   {
      _sensor->setDeferredProcessingPeriodMicros(deferredProcessingPeriodMicros);
   }

   /// <summary>
   /// Sets the rolling average buffer size.
   /// </summary>
   /// <param name="bufferSize">Buffer size for averaging.</param>
   void setBufferSize(size_t bufferSize)
   {
      _sensor->setBufferSize(bufferSize);
   }

   /// <summary>
   /// Gets the rolling average buffer size.
   /// </summary>
   /// <returns>Current buffer size.</returns>
   size_t bufferSize()
   {
      return _sensor->bufferSize();
   }

   /// <summary>
   /// Gets the sensor sample rate in samples per second.
   /// </summary>
   /// <returns>The current rolling sample rate.</returns>
   float rate()
   {
      return _sensor->rate();
   }

   /// <summary>
   /// Gets the effective sample rate after rolling-average smoothing, i.e. the raw sample rate
   /// divided by the buffer size.
   /// </summary>
   /// <returns>The current effective sample rate.</returns>
   float effectiveRate()
   {
      return _sensor->effectiveRate();
   }

   /// <summary>
   /// Gets the processed measurement counter, useful for detecting whether a new
   /// measurement has arrived since the last check.
   /// </summary>
   /// <returns>Monotonic count of processed measurements.</returns>
   uint32_t counter() const
   {
      return _sensor->counter();
   }
};
