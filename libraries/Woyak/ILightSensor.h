#pragma once

#include <Arduino.h>

///
/// <summary>
/// Interface for ambient light sensors.
/// </summary>
///
class ILightSensor
{
public:
   virtual ~ILightSensor() = default;

   ///
   /// <summary>
   /// Initializes the sensor device.
   /// </summary>
   /// <returns>True if initialization succeeded, false if the sensor was not found or communication failed.</returns>
   ///
   virtual bool begin() = 0;

   ///
   /// <summary>
   /// Gets the sensor type name.
   /// </summary>
   /// <returns>Sensor type string (e.g., "VEML7700", "BH1750").</returns>
   ///
   virtual const char* type() const = 0;

   ///
   /// <summary>
   /// Gets the I2C address.
   /// </summary>
   /// <returns>Device address, or 0 if not applicable.</returns>
   ///
   virtual uint8_t address() = 0;

   ///
   /// <summary>
   /// Checks if a physical sensor is present.
   /// </summary>
   /// <returns>True if a sensor was detected, false otherwise.</returns>
   ///
   virtual bool exists() = 0;

   ///
   /// <summary>
   /// Reads the ambient light level.
   /// </summary>
   /// <returns>Light level in lux, or NaN if the read failed.</returns>
   ///
   virtual float readLux() = 0;
};
