#include <I2C.h>
#include <Adafruit_BME280.h>
#include <Adafruit_MCP9808.h>
#include <Adafruit_SHT31.h>
#include "Util.h"

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
};

//-------------------------------------------------------------------------------------------------
class NullSensor : public ITempSensor
{
   virtual bool exists()
   {
      return false;
   }
   virtual const char* info()
   {
      return "No Sensor Detected";
   }
   virtual bool begin()
   {
      return true;
   }
   virtual float readTemperatureF()
   {
      return NAN;
   }
   virtual float readTemperatureC()
   {
      return NAN;
   }
   virtual float readHumidity()
   {
      return NAN;
   }
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
      _info = String(type) + "[0x" + String(address, HEX) + "]";
   }
   virtual const char* info() { return _info.c_str(); }
   virtual bool exists() { return true; }
   virtual float readTemperatureF() = 0;
   virtual float readTemperatureC() = 0;
   virtual float readHumidity() = 0;

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
   virtual bool begin()
   {
      return _bme.begin(getAddress());
   }
   virtual float readTemperatureF()
   {
      return Util::C2F(_bme.readTemperature());
   }
   virtual float readTemperatureC()
   {
      return _bme.readTemperature();
   }
   virtual float readHumidity()
   {
      return _bme.readHumidity();
   }
};

//-------------------------------------------------------------------------------------------------
class SHT31Sensor : public I2CTempSensor
{
private:
   Adafruit_SHT31 sht;

public:
   SHT31Sensor(uint8_t address) : I2CTempSensor("SHT31", address)
   {
   }
   virtual bool begin()
   {
      return sht.begin();
   }
   virtual float readTemperatureF()
   {
      return Util::C2F(sht.readTemperature());
   }
   virtual float readTemperatureC()
   {
      return sht.readTemperature();
   }
   virtual float readHumidity()
   {
      return sht.readHumidity();
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
};

//-------------------------------------------------------------------------------------------------
class TempSensorFactory
{
public:
   static ITempSensor* create(bool print = true)
   {
      if (print)
      {
         Serial.println("Detecting Temperature Sensor...");
      }
      ITempSensor* sensor = NULL;

      if (sensor == NULL)
      {
         int8_t address = I2CTempSensor::detect(BME280_ADDRESS, 2);
         if (address != 0)
         {
            sensor = new BME280Sensor(address);
         }
      }

      if (sensor == NULL)
      {
         int8_t address = I2CTempSensor::detect(SHT31_DEFAULT_ADDR);
         if (address != 0)
         {
            sensor = new SHT31Sensor(address);
         }
      }

      if (sensor == NULL)
      {
         int8_t address = I2CTempSensor::detect(MCP9808_I2CADDR_DEFAULT, 8);
         if (address != 0)
         {
            sensor = new MCP9808Sensor(3, MCP9808_I2CADDR_DEFAULT);
         }
      }

      if (print)
      {
         if (sensor != NULL)
         {
            Serial.println(String("Found: ") + sensor->info());
         }
         else
         {
            Serial.println("No sensor detected. Scanning I2C addressess...");
            bool found = false;
            for (int8_t address = 1; address < 127; address++)
            {
               if (I2C::exists(address))
               {
                  found = true;
                  Serial.print("  0x");
                  Serial.print(address, HEX);
                  Serial.println(": Found Device");
               }
            }
            if (found == false)
            {
               Serial.println("No devices detected");
            }
         }
      }

      if (sensor == NULL)
      {
         sensor = new NullSensor();
      }

      return sensor;
   }
};

