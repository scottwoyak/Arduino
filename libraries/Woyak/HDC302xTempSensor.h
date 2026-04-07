#pragma once

#include <Adafruit_HDC302x.h>
#include <I2CTempSensor.h>
#include <Util.h>
#include <string>


//-------------------------------------------------------------------------------------------------
class HDC302xTempSensor : public I2CTempSensor
{
private:
   Adafruit_HDC302x _hdc;

   uint64_t _readId()
   {
      uint8_t id[8];
      _hdc.readNISTID(id);

      // not sure what these all map to, but the last 4 seem to repeat among chips
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


public:
   HDC302xTempSensor(uint8_t address = 0x44) : I2CTempSensor("HDC302x", address)
   {
   }

   virtual bool begin()
   {
      if (_hdc.begin())
      {
         uint64_t id = _readId();
         std::string str = std::to_string(id);
         _id = str.c_str();
         return true;
      }
      else
      {
         return false;
      }
   }

   virtual float readTemperatureF()
   {
      double temp;
      double humidity;
      _hdc.readTemperatureHumidityOnDemand(temp, humidity, TRIGGERMODE_LP0);
      return Util::C2F((float)temp);
   }
   virtual float readTemperatureC()
   {
      double temp;
      double humidity;
      _hdc.readTemperatureHumidityOnDemand(temp, humidity, TRIGGERMODE_LP0);
      return (float)temp;
   }
   virtual float readHumidity()
   {
      double temp;
      double humidity;
      _hdc.readTemperatureHumidityOnDemand(temp, humidity, TRIGGERMODE_LP0);
      return (float)humidity;
   }
   virtual bool readsBoth() { return true; }
   virtual void readBoth(float& tempF, float& hum)
   {
      double t, h;
      _hdc.readTemperatureHumidityOnDemand(t, h, TRIGGERMODE_LP0);
      tempF = Util::C2F((float)t);
      hum = (float)h;
   }

   static HDC302xTempSensor* tryCreate()
   {
      Adafruit_HDC302x hdc;
      hdc.begin();
      uint16_t id = hdc.readManufacturerID();

      if (id == 0x3000)
      {
         return new HDC302xTempSensor();
      }
      else
      {
         return nullptr;
      }
   }
};

