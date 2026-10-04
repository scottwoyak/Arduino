#pragma once

#include "ITempSensor.h"
#include "Units.h"
#include "CPUTemp.h"

/// <summary>
/// ESP32 built-in CPU temperature sensor.
/// </summary>
/// <remarks>
/// Reads the internal temperature sensor of ESP32 and ESP32-S3 processors.
/// Provides CPU die temperature; does not measure external sensors or humidity.
/// Useful for thermal monitoring during development or for ambient estimates.
/// </remarks>
class ESP32TempSensor : public ITempSensor
{
public:
   /// <summary>
   /// Constructs an ESP32TempSensor instance.
   /// </summary>
   ESP32TempSensor() {}

   /// <summary>Gets the sensor type string.</summary>
   virtual const char* type() const { return "ESP32 CPU"; }

   /// <summary>Gets the sensor ID.</summary>
   virtual const char* id() { return ""; }

   /// <summary>Gets the device address (N/A for built-in sensor).</summary>
   virtual uint8_t address() { return 0; }

   /// <summary>Checks if sensor is present (always true for built-in).</summary>
   virtual bool exists() { return true; }

   /// <summary>
   /// Initializes the ESP32 temperature sensor for reading with a range of 20°C to 100°C.
   /// </summary>
   /// <returns>True if initialization succeeded; false otherwise.</returns>
   virtual bool begin()
   {
      return CPUTemp::begin();
   }

   /// <summary>Reads temperature in Fahrenheit.</summary>
   virtual float readTemperatureF() 
   { 
      return CPUTemp::readF();
   }

   /// <summary>Reads temperature in Celsius.</summary>
   virtual float readTemperatureC() 
   { 
      return CPUTemp::readC();
   }

   /// <summary>Not supported; returns NaN.</summary>
   virtual float readHumidity() { return NAN; }

   /// <summary>Returns false (no humidity support).</summary>
   virtual bool supportsHumidity() { return false; }
};
