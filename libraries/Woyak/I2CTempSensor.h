#pragma once

#include <I2C.h>
#include <ITempSensor.h>

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

