#pragma once

#include <ITempSensor.h>
#include <driver/temperature_sensor.h>

//-------------------------------------------------------------------------------------------------
class ESP32TempSensor : public ITempSensor
{
private:
   temperature_sensor_handle_t _tempHandle = NULL;

public:
   ESP32TempSensor() {}
   virtual const char* type() { return "ESP32 CPU"; }
   virtual const char* id() { return ""; }
   virtual uint8_t address() { return 0; }
   virtual bool exists() { return true; }   
   virtual bool begin()
   { 
      // Initialize the sensor and evaluate your expected temp range (e.g., 20°C to 100°C)
      temperature_sensor_config_t temp_sensor_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(20, 100);
      temperature_sensor_install(&temp_sensor_config, &_tempHandle);

      // Enable the temperature sensor
      temperature_sensor_enable(_tempHandle);
    
      return true; 
   }
   virtual float readTemperatureF() 
   { 
      return Util::C2F(readTemperatureC()); 
   }
   virtual float readTemperatureC() 
   { 
      float tempC = 0;
      temperature_sensor_get_celsius(_tempHandle, &tempC);
      return tempC; 
   }
   virtual float readHumidity() { return NAN; }
   virtual bool readsBoth() { return false; }
   virtual void readBoth(float& tempF, float& hum) { tempF = readTemperatureF(); hum = readHumidity(); }
};

