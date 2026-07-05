#pragma once

#include "CapacitorSensor.h"

/// <summary>
/// Converts capacitor charge timing into a depth value using linear interpolation.
/// </summary>
class CapacitorDepthSensor
{
private:
   CapacitorSensor* _sensor;
   float _zeroTime;
   float _fullTime;
   float _fullDepth;

public:
   /// <summary>
   /// Initializes a depth sensor wrapper around the capacitor timing sensor.
   /// </summary>
   /// <param name="chargePin">GPIO pin used to charge the sensor through the resistor network.</param>
   /// <param name="sensePin">GPIO pin used to detect the rising threshold.</param>
   /// <param name="zeroTime">Charge time (microseconds) measured at zero depth.</param>
   /// <param name="fullTime">Charge time (microseconds) measured at full depth.</param>
   /// <param name="fullDepth">Depth value corresponding to fullTime.</param>
   CapacitorDepthSensor(
      uint8_t chargePin,
      uint8_t sensePin,
      float zeroTime,
      float fullTime,
      float fullDepth)
   {
      _sensor = new CapacitorSensor(chargePin, sensePin);
      _zeroTime = zeroTime;
      _fullTime = fullTime;
      _fullDepth = fullDepth;
   }

   ~CapacitorDepthSensor()
   {
      delete _sensor;
   }

   /// <summary>
   /// Gets the latest computed depth based on the current capacitor charge time.
   /// </summary>
   /// <returns>The interpolated depth value.</returns>
   float getDepth()
   {
      float span = _fullTime - _zeroTime;
      if (span == 0.0f)
      {
         return 0.0f;
      }

      float chargeTime = _sensor->chargeTimeMicros();
      float depth = _fullDepth * ((chargeTime - _zeroTime) / span);
      return depth;
   }

   /// <summary>
   /// Starts sensor timing and interrupt processing.
   /// </summary>
   void begin()
   {
      _sensor->begin();
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
   /// <param name="zeroTime">Charge time measured at zero depth.</param>
   /// <param name="fullTime">Charge time measured at full depth.</param>
   void setCalibration(float zeroTime, float fullTime)
   {
      _zeroTime = zeroTime;
      _fullTime = fullTime;
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
};
