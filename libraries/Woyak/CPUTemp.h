#pragma once

#include <driver/temperature_sensor.h>

#include "Units.h"

///
/// <summary>
/// Static access to the ESP32 built-in CPU temperature sensor. The underlying driver can only
/// be installed once per sketch, so everything that needs the CPU temperature (health reports,
/// InfluxDB, ESP32TempSensor, ...) goes through this class rather than the driver directly.
/// </summary>
///
class CPUTemp
{
   /// <summary>Handle to the ESP32 temperature sensor driver; NULL until begin() succeeds.</summary>
   static inline temperature_sensor_handle_t _handle = NULL;

public:
   ///
   /// <summary>
   /// Installs and enables the temperature sensor driver (range 20 C to 100 C). Safe to call
   /// any number of times from any location; only the first successful call does any work.
   /// </summary>
   /// <returns>True if the sensor is ready; false if the driver could not be started.</returns>
   ///
   static bool begin()
   {
      if (_handle != NULL)
      {
         return true;
      }

      temperature_sensor_handle_t handle = NULL;
      temperature_sensor_config_t config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(20, 100);
      if (temperature_sensor_install(&config, &handle) != ESP_OK)
      {
         return false;
      }

      if (temperature_sensor_enable(handle) != ESP_OK)
      {
         temperature_sensor_uninstall(handle);
         return false;
      }

      _handle = handle;
      return true;
   }

   ///
   /// <summary>
   /// Reads the CPU temperature, starting the sensor first if nobody has yet.
   /// </summary>
   /// <returns>Temperature in Celsius, or NAN if the sensor is unavailable.</returns>
   ///
   static float readC()
   {
      if (!begin())
      {
         return NAN;
      }

      float tempC = NAN;
      if (temperature_sensor_get_celsius(_handle, &tempC) != ESP_OK)
      {
         return NAN;
      }
      return tempC;
   }

   ///
   /// <summary>
   /// Reads the CPU temperature, starting the sensor first if nobody has yet.
   /// </summary>
   /// <returns>Temperature in Fahrenheit, or NAN if the sensor is unavailable.</returns>
   ///
   static float readF()
   {
      return Units::C2F(readC());
   }
};
