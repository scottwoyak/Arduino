#pragma once

#include <Arduino.h>

/// <summary>
/// Interface for temperature and humidity sensors.
/// </summary>
/// <remarks>
/// Provides a common abstraction for various temperature (and optional humidity) sensor types
/// including I2C sensors (BME280, SHT3x, etc.), 1-Wire sensors (DS18B20), and built-in sensors.
/// Implementations may read temperature only, temperature+humidity, or derived measurements.
/// </remarks>
class ITempSensor
{
public:
   virtual ~ITempSensor() = default;

   /// <summary>
   /// Initializes the sensor device.
   /// </summary>
   /// <returns>True if initialization succeeded, false if sensor not found or communication failed</returns>
   virtual bool begin() = 0;

   /// <summary>
   /// Gets a unique identifier for this sensor instance.
   /// </summary>
   /// <returns>Sensor ID string (typically name or address)</returns>
   virtual const char* id() = 0;

   /// <summary>
   /// Gets the sensor type name.
   /// </summary>
   /// <returns>Sensor type string (e.g., "DHT22", "BME280", "DS18B20")</returns>
   virtual const char* type() = 0;

   /// <summary>
   /// Gets the I2C or 1-Wire address.
   /// </summary>
   /// <returns>Device address (0xFF if N/A)</returns>
   virtual uint8_t address() = 0;

   /// <summary>
   /// Checks if the sensor is present and responding.
   /// </summary>
   /// <returns>True if sensor detected, false otherwise</returns>
   virtual bool exists() = 0;

   /// <summary>
   /// Reads the temperature in Fahrenheit.
   /// </summary>
   /// <returns>Temperature in °F, or NaN if read failed</returns>
   virtual float readTemperatureF() = 0;

   /// <summary>
   /// Reads the temperature in Celsius.
   /// </summary>
   /// <returns>Temperature in °C, or NaN if read failed</returns>
   virtual float readTemperatureC() = 0;

   /// <summary>
   /// Reads the relative humidity percentage.
   /// </summary>
   /// <returns>Humidity 0-100%, or NaN if sensor doesn't support humidity or read failed</returns>
   virtual float readHumidity() = 0;

   /// <summary>
   /// Checks if this sensor can read both temperature and humidity.
   /// </summary>
   /// <returns>True if sensor supports humidity, false if temperature-only</returns>
   virtual bool readsBoth() = 0;

   /// <summary>
   /// Reads temperature and humidity in a single operation.
   /// </summary>
   /// <param name="tempF">Output: temperature in Fahrenheit</param>
   /// <param name="hum">Output: relative humidity 0-100%</param>
   /// <remarks>
   /// More efficient than separate calls to readTemperatureF() and readHumidity().
   /// Only valid if readsBoth() returns true.
   /// </remarks>
   virtual void readBoth(float& tempF, float& hum) = 0;
};
