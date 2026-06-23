#pragma once

#include "CapacitorSensor.h"

/// <summary>
/// Converts capacitor charge timing into a depth value using linear interpolation.
/// </summary>
class CapacitorDepthSensor
{
private:
   CapacitorSensor _sensor;
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
      float fullDepth) : _sensor(chargePin, sensePin)
   {
      _zeroTime = zeroTime;
      _fullTime = fullTime;
      _fullDepth = fullDepth;
   }

   /// <summary>
   /// Gets the latest computed depth based on the current capacitor charge time.
   /// </summary>
   /// <returns>The interpolated depth value.</returns>
   float getDepth()
   {
      float chargeTime = _sensor.chargeTimeMicros();
      float depth = _fullDepth * ((chargeTime - _zeroTime) / (_fullTime - _zeroTime));
      return depth;
   }

   /// <summary>
   /// Starts sensor timing and interrupt processing.
   /// </summary>
   void begin()
   {
      _sensor.begin();
   }

   /// <summary>
   /// Returns the raw measured charge time in microseconds.
   /// </summary>
   /// <returns>The latest charge time measurement.</returns>
   uint32_t chargeTimeMicros() const
   {
      return _sensor.chargeTimeMicros();
   }

   /// <summary>
   /// Indicates whether a new raw measurement has been captured since the last check.
   /// </summary>
   /// <returns>True when a new measurement is available; otherwise false.</returns>
   bool hasChanged()
   {
      return _sensor.hasChanged();
   }

   /// <summary>
   /// Resets the sensor rate tracking window.
   /// </summary>
   void resetRate()
   {
      _sensor.resetRate();
   }

   /// <summary>
   /// Gets the sensor sample rate in samples per second.
   /// </summary>
   /// <returns>The current rolling sample rate.</returns>
   float rate()
   {
      return _sensor.rate();
   }
};
