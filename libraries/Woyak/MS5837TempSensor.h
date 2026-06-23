#pragma once

#include "I2CTempSensor.h"
#include <MS5837.h>
#include "Units.h"

//-------------------------------------------------------------------------------------------------
class MS5837TempSensor : public I2CTempSensor
{
public:
   static constexpr uint8_t DEFAULT_I2C_ADDRESS = 0x76;

private:
   MS5837 _sensor;
   uint8_t _model;

public:
   MS5837TempSensor(uint8_t model = MS5837::MS5837_02BA)
      : I2CTempSensor("MS5837", DEFAULT_I2C_ADDRESS), _sensor(), _model(model)
   {
   }

   virtual bool begin()
   {
      _sensor.setModel(_model);
      return _sensor.init();
   }

   virtual float readTemperatureF()
   {
      return Units::C2F(readTemperatureC());
   }

   virtual float readTemperatureC()
   {
      _sensor.read();
      return _sensor.temperature();
   }

   virtual float readHumidity()
   {
      return NAN;
   }

   virtual bool readsBoth()
   {
      return false;
   }

   virtual void readBoth(float& tempF, float& hum)
   {
      tempF = readTemperatureF();
      hum = NAN;
   }

   static MS5837TempSensor* tryCreate(uint8_t model = MS5837::MS5837_02BA)
   {
      MS5837TempSensor* sensor = new MS5837TempSensor(model);
      if (sensor->begin())
      {
         return sensor;
      }

      delete sensor;
      return nullptr;
   }
};
