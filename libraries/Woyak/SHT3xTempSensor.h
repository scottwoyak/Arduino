#pragma once

#include <I2CTempSensor.h>
#include <Adafruit_SHT31.h>

//-------------------------------------------------------------------------------------------------
class SHT3xTempSensor : public I2CTempSensor
{
private:
   Adafruit_SHT31 sht;

public:
   SHT3xTempSensor(uint8_t address = SHT31_DEFAULT_ADDR) : I2CTempSensor("SHT3x", address)
   {
   }
   virtual bool begin() { return sht.begin(); }
   virtual float readTemperatureF() { return Util::C2F(sht.readTemperature()); }
   virtual float readTemperatureC() { return sht.readTemperature(); }
   virtual float readHumidity() { return sht.readHumidity(); }
   virtual bool readsBoth() { return false; }
   virtual void readBoth(float& tempF, float& hum) { tempF = readTemperatureF(); hum = readHumidity(); }
};

