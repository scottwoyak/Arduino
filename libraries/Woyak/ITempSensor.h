#pragma once

//-------------------------------------------------------------------------------------------------
class ITempSensor
{
public:
   virtual bool begin() = 0;
   virtual const char* id() = 0;
   virtual const char* type() = 0;
   virtual uint8_t address() = 0;
   virtual bool exists() = 0;
   virtual float readTemperatureF() = 0;
   virtual float readTemperatureC() = 0;
   virtual float readHumidity() = 0;
   virtual bool readsBoth() = 0;
   virtual void readBoth(float& tempF, float& hum) = 0;
};
