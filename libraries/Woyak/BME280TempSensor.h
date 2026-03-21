#pragma once

#include <I2CTempSensor.h>
#include <Adafruit_BME280.h>
#include <Util.h>

//-------------------------------------------------------------------------------------------------
class BME280TempSensor : public I2CTempSensor
{
private:
   Adafruit_BME280 _bme;

public:
   BME280TempSensor(uint8_t address) : I2CTempSensor("BME280", address)
   {
   }
   virtual bool begin() { return _bme.begin(getAddress()); }
   virtual float readTemperatureF() { return Util::C2F(_bme.readTemperature()); }
   virtual float readTemperatureC() { return _bme.readTemperature(); }
   virtual float readHumidity() { return _bme.readHumidity(); }
   virtual bool readsBoth() { return false; }
   virtual void readBoth(float& tempF, float& hum) { tempF = readTemperatureF(); hum = readHumidity(); }
};

