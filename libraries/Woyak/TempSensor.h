#pragma once

#include <I2C.h>
#include <Adafruit_BME280.h>
#include <Adafruit_MCP9808.h>
#include <Adafruit_SHT31.h>
#include <Adafruit_SHT4x.h>
#include <Adafruit_HDC302x.h>
#include "Util.h"
#include <string>

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

//-------------------------------------------------------------------------------------------------
class TempSensor : public ITempSensor
{
private:
   // the actual sensor this class manages
   ITempSensor* _sensor = NULL;
   ITempSensor* _create(bool print);

public:
   float tempCorrectionF = 0;
   float humCorrection = 0;

   bool begin() { return begin(true); }
   bool begin(bool print)
   {
      Wire.begin();
      _sensor = _create(print);
      return _sensor->begin();
   }
   bool exists()
   {
      if (_sensor == nullptr)
      {
         Serial.println("TempSensor::exists() - No sensor created. Call begin()");
         return false;
      }

      return _sensor->exists();
   }
   const char* type()
   {
      if (_sensor == nullptr)
      {
         Serial.println("TempSensor::type() - No sensor created. Call begin()");
         return "No sensor created. Call begin()";
      }

      return _sensor->type();
   }
   uint8_t address()
   {
      if (_sensor == nullptr)
      {
         Serial.println("TempSensor::address() - No sensor created. Call begin()");
         return 0;
      }

      return _sensor->address();
   }
   const char* id()
   {
      if (_sensor == nullptr)
      {
         Serial.println("TempSensor::id() - No sensor created. Call begin()");
         return "No sensor created. Call begin()";
      }

      return _sensor->id();
   }
   float readTemperatureF()
   {
      if (_sensor == nullptr)
      {
         Serial.println("TempSensor::readTemperatureF() - No sensor created. Call begin()");
         return NAN;
      }

      return _sensor->readTemperatureF() + tempCorrectionF;
   }
   float readTemperatureC()
   {
      if (_sensor == nullptr)
      {
         Serial.println("TempSensor::readTemperatureC() - No sensor created. Call begin()");
         return NAN;
      }

      return _sensor->readTemperatureC() + Util::F2C(tempCorrectionF);
   }
   float readHumidity()
   {
      if (_sensor == nullptr)
      {
         Serial.println("TempSensor::readHumidity() - No sensor created. Call begin()");
         return NAN;
      }

      return _sensor->readHumidity() + humCorrection;
   }
   bool readsBoth()
   {
      if (_sensor == nullptr)
      {
         Serial.println("TempSensor::readsBoth() - No sensor created. Call begin()");
         return false;
      }

      return _sensor->readsBoth();
   }
   void readBoth(float& tempF, float& hum)
   {
      if (_sensor == nullptr)
      {
         Serial.println("TempSensor::readBoth() - No sensor created. Call begin()");
         return;
      }

      _sensor->readBoth(tempF, hum);
      tempF += tempCorrectionF;
      hum += humCorrection;
   }
};

//-------------------------------------------------------------------------------------------------
class NullSensor : public ITempSensor
{
   virtual bool exists() { return false; }
   virtual const char* type() { return "None"; }
   virtual const char* id() { return "??"; }
   virtual uint8_t address() { return 0; };
   virtual bool begin() { return true; }
   virtual float readTemperatureF() { return NAN; }
   virtual float readTemperatureC() { return NAN; }
   virtual float readHumidity() { return NAN; }
   virtual bool readsBoth() { return false; }
   virtual void readBoth(float& tempF, float& hum)
   {
      tempF = NAN;
      hum = NAN;
   }
};

//-------------------------------------------------------------------------------------------------
class I2CTempSensor : public ITempSensor
{
protected:
   String _type;
   String _info;
   int8_t _address;
   String _id = "----";

public:
   I2CTempSensor(const char* type, uint8_t address)
   {
      _type = type;
      _address = address;
      _info = _type + " 0x" + String(_address, HEX);
   }
   virtual const char* id() { return _id.c_str(); }
   virtual const char* type() { return _type.c_str(); }
   virtual uint8_t address() { return _address; }

   virtual bool exists() { return true; }
   virtual float readTemperatureF() = 0;
   virtual float readTemperatureC() = 0;
   virtual float readHumidity() = 0;
   virtual bool readsBoth() = 0;

   int8_t getAddress() { return _address; }

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
   virtual void readBoth(float& tempF, float& hum) { tempF = readTemperatureF(); hum = readHumidity(); }
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
   virtual void readBoth(float& tempF, float& hum) { tempF = readTemperatureF(); hum = readHumidity(); }
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
      if (sht.begin())
      {
         _id = sht.readSerial();
         return true;
      }
      else
      {
         return false;
      }
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
      if (hdc.begin())
      {
         _id = String(readId(hdc));
         return true;
      }
      else
      {
         return false;
      }
   }
   static uint64_t readId(Adafruit_HDC302x& hdc)
   {
      uint8_t id[8];
      hdc.readNISTID(id);

      /* not sure what these all map to, but the last 4 seem to repeat among chips */
      /*
      Serial.println(id[0]);
      Serial.println(id[1]);
      Serial.println(id[2]);
      Serial.println(id[3]);
      Serial.println(id[4]);
      Serial.println(id[5]);
      Serial.println(id[6]);
      Serial.println(id[7]);
      */

      uint32_t fullId =
         ((uint32_t)id[0] << 24) |
         ((uint32_t)id[1] << 16) |
         ((uint32_t)id[2] << 8) |
         ((uint32_t)id[3]);

      return fullId;
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
   virtual bool begin() { return mcp.begin(getAddress()); }
   virtual float readTemperatureF() { return mcp.readTempF(); }
   virtual float readTemperatureC() { return mcp.readTempF(); }
   virtual float readHumidity() { return NAN; }
   virtual bool readsBoth() { return false; }
   virtual void readBoth(float& tempF, float& hum) { tempF = readTemperatureF(); hum = readHumidity(); }
};

