#pragma once

#include <I2C.h>
#include <Adafruit_BME280.h>
#include <Adafruit_MCP9808.h>
#include <Adafruit_SHT31.h>
#include <Adafruit_SHT4x.h>
#include <Adafruit_HDC302x.h>
#include "Util.h"

class ITempSensor;

//-------------------------------------------------------------------------------------------------
class TempSensor
{
private:
   ITempSensor* _sensor = NULL;
   ITempSensor* _create(bool print);

public:
   bool begin(bool print = true);
   const char* info();
   bool exists();
   float readTemperatureF();
   float readTemperatureC();
   float readHumidity();

   bool readsBoth();
   void readBoth(float& tempF, float& hum);
};

//-------------------------------------------------------------------------------------------------
class ITempSensor
{
public:
   virtual bool begin() = 0;
   virtual const char* info() = 0;
   virtual bool exists() = 0;
   virtual float readTemperatureF() = 0;
   virtual float readTemperatureC() = 0;
   virtual float readHumidity() = 0;

   virtual bool readsBoth() = 0;
   virtual void readBoth(float& tempF, float& hum)
   {
      tempF = readTemperatureF();
      hum = readHumidity();
   }
};

//-------------------------------------------------------------------------------------------------
class NullSensor : public ITempSensor
{
   virtual bool exists() { return false; }
   virtual const char* info() { return "No Sensor Detected"; }
   virtual bool begin() { return true; }
   virtual float readTemperatureF() { return NAN; }
   virtual float readTemperatureC() { return NAN; }
   virtual float readHumidity() { return NAN; }
   virtual bool readsBoth() { return false; }
   virtual void readBoth(float tempF, float hum) { tempF = hum = NAN; }
};

//-------------------------------------------------------------------------------------------------
class I2CTempSensor : public ITempSensor
{
private:
   String _info;
   int8_t _address;

public:
   I2CTempSensor(const char* type, int8_t address)
   {
      _address = address;
      _info = String(type) + " 0x" + String(address, HEX);
   }
   virtual const char* info() { return _info.c_str(); }
   virtual bool exists() { return true; }
   virtual float readTemperatureF() = 0;
   virtual float readTemperatureC() = 0;
   virtual float readHumidity() = 0;
   virtual bool readsBoth() = 0;

   int8_t getAddress()
   {
      return _address;
   }


   static int8_t detect(int8_t baseAddress, int8_t numAddresses = 1)
   {
      for (int8_t i = 0; i < numAddresses; i++)
      {
         if (I2C::exists(baseAddress + i))
         {
            return baseAddress + i;
         }
      }

      return 0;
   }
};

//-------------------------------------------------------------------------------------------------
class BME280Sensor : public I2CTempSensor
{
private:
   Adafruit_BME280 _bme;

public:
   BME280Sensor(uint8_t address) : I2CTempSensor("BME280", address)
   {
   }
   virtual bool begin() { return _bme.begin(getAddress()); }
   virtual float readTemperatureF() { return Util::C2F(_bme.readTemperature()); }
   virtual float readTemperatureC() { return _bme.readTemperature(); }
   virtual float readHumidity() { return _bme.readHumidity(); }
   virtual bool readsBoth() { return false; }
};

//-------------------------------------------------------------------------------------------------
class SHT31Sensor : public I2CTempSensor
{
private:
   Adafruit_SHT31 sht;

public:
   SHT31Sensor(uint8_t address = SHT31_DEFAULT_ADDR) : I2CTempSensor("SHT3x", address)
   {
   }
   virtual bool begin() { return sht.begin(); }
   virtual float readTemperatureF() { return Util::C2F(sht.readTemperature()); }
   virtual float readTemperatureC() { return sht.readTemperature(); }
   virtual float readHumidity() { return sht.readHumidity(); }
   virtual bool readsBoth() { return false; }
};

//-------------------------------------------------------------------------------------------------
class SHT4xSensor : public I2CTempSensor
{
private:
   Adafruit_SHT4x sht;

public:
   SHT4xSensor(uint8_t address = SHT4x_DEFAULT_ADDR) : I2CTempSensor("SHT4x", address)
   {
   }
   virtual bool begin()
   {
      return sht.begin();
   }
   virtual float readTemperatureF()
   {
      sensors_event_t humidity, temp;
      sht.getEvent(&humidity, &temp);

      return Util::C2F(temp.temperature);
   }
   virtual float readTemperatureC()
   {
      sensors_event_t humidity, temp;
      sht.getEvent(&humidity, &temp);

      return temp.temperature;
   }
   virtual float readHumidity()
   {
      sensors_event_t humidity, temp;
      sht.getEvent(&humidity, &temp);

      return humidity.relative_humidity;
   }
   virtual bool readsBoth() { return true; }
   virtual void readBoth(float& tempF, float& hum)
   {
      sensors_event_t h, t;
      sht.getEvent(&h, &t);

      tempF = Util::C2F(t.temperature);
      hum = h.relative_humidity;
   }
};

//-------------------------------------------------------------------------------------------------
class HDC302xSensor : public I2CTempSensor
{
private:
   Adafruit_HDC302x hdc;

public:
   HDC302xSensor(uint8_t address = 0x44) : I2CTempSensor("HDC302x", address)
   {
   }
   virtual bool begin()
   {
      return hdc.begin();
   }
   virtual float readTemperatureF()
   {
      double temp;
      double humidity;
      hdc.readTemperatureHumidityOnDemand(temp, humidity, TRIGGERMODE_LP0);
      return Util::C2F((float)temp);
   }
   virtual float readTemperatureC()
   {
      double temp;
      double humidity;
      hdc.readTemperatureHumidityOnDemand(temp, humidity, TRIGGERMODE_LP0);
      return (float)temp;
   }
   virtual float readHumidity()
   {
      double temp;
      double humidity;
      hdc.readTemperatureHumidityOnDemand(temp, humidity, TRIGGERMODE_LP0);
      return (float)humidity;
   }
   virtual bool readsBoth() { return true; }
   virtual void readBoth(float& tempF, float& hum)
   {
      double t, h;
      hdc.readTemperatureHumidityOnDemand(t, h, TRIGGERMODE_LP0);
      tempF = Util::C2F((float)t);
      hum = (float)h;
   }
};

//-------------------------------------------------------------------------------------------------
class MCP9808Sensor : public I2CTempSensor
{
private:
   Adafruit_MCP9808 mcp;

public:
   MCP9808Sensor(uint8_t resolution, uint8_t address) : I2CTempSensor("MCP9808", address)
   {
      // Mode Resolution SampleTime
      //  0    0.5°C       30 ms
      //  1    0.25°C      65 ms
      //  2    0.125°C     130 ms
      //  3    0.0625°C    250 ms
      mcp.setResolution(resolution);
   }
   virtual bool begin()
   {
      return mcp.begin(getAddress());
   }
   virtual float readTemperatureF()
   {
      return mcp.readTempF();
   }
   virtual float readTemperatureC()
   {
      return mcp.readTempF();
   }
   virtual float readHumidity()
   {
      return NAN;
   }
   virtual bool readsBoth() { return false; }
};

