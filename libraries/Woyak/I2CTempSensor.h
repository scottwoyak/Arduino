#pragma once

#include "I2C.h"
#include "ITempSensor.h"

//-------------------------------------------------------------------------------------------------
class I2CTempSensor : public ITempSensor
{
protected:
   String _type;
   String _info;
   int8_t _address;
   String _id = "----";

public:
   explicit I2CTempSensor(const char* type, uint8_t address)
   {
      _type = type;
      _address = address;
      _info = _type + " 0x" + String(_address, HEX);
   }

   ~I2CTempSensor() override = default;

   const char* id() override { return _id.c_str(); }
   const char* type() override { return _type.c_str(); }
   uint8_t address() override { return _address; }

   bool exists() override { return true; }
   virtual float readTemperatureF() = 0;
   virtual float readTemperatureC() = 0;
   virtual float readHumidity() = 0;
   virtual bool readsBoth() = 0;

   int8_t getAddress() const { return _address; }

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

