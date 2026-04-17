#pragma once

#include <DallasTemperature.h>
#include <ITempSensor.h>

class DS18B20TempSensor : public ITempSensor
{
private:
   OneWire _oneWire;
   DallasTemperature _sensors;
   uint8_t _index;
   String _id;
   DeviceAddress _da;

public:
   DS18B20TempSensor(uint8_t pin) : _oneWire(pin), _sensors(&_oneWire)
   {
      _index = 0;
   }

   virtual bool exists() { return true; }
   virtual bool begin()
   {
      _sensors.begin();
      _sensors.setResolution(12);
      return true;
   }

   virtual const char* type() { return "DS18B20"; }
   virtual const char* id()
   {
      if (_id.length() == 0)
      {
         _sensors.getAddress(_da, _index);
         for (uint8_t i = 0; i < 8; i++)
         {
            _id += _da[i];
         }
      }

      return _id.c_str();
   }
   virtual uint8_t address()
   {
      return 0;
   }

   virtual float readTemperatureF()
   {
      _sensors.requestTemperaturesByIndex(0);
      float value = _sensors.getTempFByIndex(0);
      return (value == (float)DEVICE_DISCONNECTED_F) ? NAN : value;
   }
   virtual float readTemperatureC()
   {
      _sensors.requestTemperaturesByIndex(0);
      float value = _sensors.getTempCByIndex(0);
      return (value == (float)DEVICE_DISCONNECTED_C) ? NAN : value;
   }

   virtual float readHumidity() { return NAN; }
   virtual bool readsBoth() { return false; }
   virtual void readBoth(float& tempF, float& hum) { tempF = readTemperatureF(); hum = readHumidity(); }
};

