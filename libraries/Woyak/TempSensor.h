#pragma once

#include <I2CTempSensor.h>
#include <ITempSensor.h>
#include <TempSensorCallibration.h>

// sensor types
#include <BME280TempSensor.h>
#include <DS18B20TempSensor.h>
#include <HDC302xTempSensor.h>
#include <MCP9808TempSensor.h>
#include <SHT3xTempSensor.h>
#include <SHT4xTempSensor.h>

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
class TempSensor : public ITempSensor
{
private:
   // the actual sensor this class manages
   ITempSensor* _sensor = NULL;
   ITempSensor* _create(bool print)
   {
      if (print) Serial.println("Detecting Temperature Sensor...");

      ITempSensor* sensor = nullptr;

      if (sensor == nullptr)
      {
         int8_t address = I2CTempSensor::detect(BME280_ADDRESS, 2);
         if (address != 0)
         {
            sensor = new BME280TempSensor(address);
         }
      }

      if (sensor == nullptr)
      {
         // Multiple sensors use 0x44 as an address
         int8_t address = I2CTempSensor::detect(0x44);
         if (address != 0)
         {
            if (print) Serial.println("  Something found at 0x44");

            if (print) Serial.print("  Checking HDC302x... ");
            sensor = HDC302xTempSensor::tryCreate();

            if (sensor != nullptr)
            {
               if (print) Serial.print("Success");
            }
            else
            {
               if (print) Serial.print("Nope");
               if (print) Serial.print("  Checking SHT4x... ");
               sensor = SHT4xTempSensor::tryCreate();

               if (sensor != nullptr)
               {
                  if (print) Serial.print("Success");
               }
               else
               {
                  if (print) Serial.print("Nope");
               }
            }

            if (sensor == nullptr)
            {
               if (print) Serial.println("  Defaulting to SHT3x");
               sensor = new SHT3xTempSensor(address);
            }
         }
      }

      if (sensor == nullptr)
      {
         int8_t address = I2CTempSensor::detect(MCP9808_I2CADDR_DEFAULT, 8);
         if (address != 0)
         {
            sensor = new MCP9808TempSensor(3, MCP9808_I2CADDR_DEFAULT);
         }
      }

      if (print)
      {
         if (sensor != nullptr)
         {
            Serial.println(String("  Found: ") + sensor->type() + " at 0x" + String(sensor->address(), HEX));
         }
         else
         {
            Serial.println("No sensor detected.");
            Serial.println("Scanning I2C addressess...");
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

      if (sensor != nullptr)
      {
         sensor->begin();
         std::string id = sensor->id();
         for (int i = 0; i < NUM_CORRECTIONS; i++)
         {
            if (id == CORRECTIONS[i].id)
            {
               _tempCorrectionF = CORRECTIONS[i].tempF;
               _humCorrection = CORRECTIONS[i].hum;
            }
         }
      }

      if (sensor == nullptr)
      {
         sensor = new NullSensor();
      }

      return sensor;
   }

   float _tempCorrectionF = 0;
   float _humCorrection = 0;

public:

   bool begin() { return begin(true); } // needed to implement ITempSensor
   bool begin(bool print)
   {
      Wire.begin();
      _sensor = _create(print);
      return _sensor->begin();
   }

   bool begin(uint8_t oneWirePin, bool print)
   {
      if (print) Serial.println("Creating DS18B20 sensor");
      _sensor = new DS18B20TempSensor(oneWirePin);
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
   float tempCorrectionF()
   {
      return _tempCorrectionF;
   }
   float humidityCorrection()
   {
      return _humCorrection;
   }
   void setTempCorrectionF(float correction)
   {
      _tempCorrectionF = correction;
   }
   void setHumidityCorrection(float correction)
   {
      _humCorrection = correction;
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

      return _sensor->readTemperatureF() + _tempCorrectionF;
   }
   float readTemperatureC()
   {
      if (_sensor == nullptr)
      {
         Serial.println("TempSensor::readTemperatureC() - No sensor created. Call begin()");
         return NAN;
      }

      return _sensor->readTemperatureC() + Util::F2C(_tempCorrectionF);
   }
   float readHumidity()
   {
      if (_sensor == nullptr)
      {
         Serial.println("TempSensor::readHumidity() - No sensor created. Call begin()");
         return NAN;
      }

      return _sensor->readHumidity() + _humCorrection;
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
      tempF += _tempCorrectionF;
      hum += _humCorrection;
   }
};





