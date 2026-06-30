#pragma once

#include "ITempSensor.h"
#include "Units.h"
#include <driver/temperature_sensor.h>

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
private:
   /// <summary>Handle to the ESP32 temperature sensor driver.</summary>
   temperature_sensor_handle_t _tempHandle = NULL;

public:
   /// <summary>
   /// Constructs an ESP32TempSensor instance.
   /// </summary>
   ESP32TempSensor() {}

   /// <summary>Gets the sensor type string.</summary>
   virtual const char* type() { return "ESP32 CPU"; }

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
      // Initialize the sensor and evaluate your expected temp range (e.g., 20°C to 100°C)
      temperature_sensor_config_t temp_sensor_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(20, 100);
      temperature_sensor_install(&temp_sensor_config, &_tempHandle);

      // Enable the temperature sensor
      temperature_sensor_enable(_tempHandle);

      return true; 
   }

   /// <summary>Reads temperature in Fahrenheit.</summary>
   virtual float readTemperatureF() 
   { 
      return Units::C2F(readTemperatureC()); 
   }

   /// <summary>Reads temperature in Celsius.</summary>
   virtual float readTemperatureC() 
   { 
      float tempC = 0;
      temperature_sensor_get_celsius(_tempHandle, &tempC);
      return tempC; 
   }

   /// <summary>Not supported; returns NaN.</summary>
   virtual float readHumidity() { return NAN; }

   /// <summary>Returns false (no humidity support).</summary>
   virtual bool readsBoth() { return false; }

   /// <summary>Reads temperature; humidity always NaN.</summary>
   virtual void readBoth(float& tempF, float& hum) { tempF = readTemperatureF(); hum = readHumidity(); }
};
