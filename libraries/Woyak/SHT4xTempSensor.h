#pragma once

#include <I2CTempSensor.h>
#include <Adafruit_SHT4x.h>
#include <Units.h>

//-------------------------------------------------------------------------------------------------
class SHT4xTempSensor : public I2CTempSensor
{
private:
   Adafruit_SHT4x sht;

public:
   SHT4xTempSensor(uint8_t address = SHT4x_DEFAULT_ADDR) : I2CTempSensor("SHT4x", address)
   {}

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
      if (sht.getEvent(&humidity, &temp))
      {
         return Units::C2F(temp.temperature);
      }
      else
      {
         return NAN;
      }
   }
   virtual float readTemperatureC()
   {
      sensors_event_t humidity, temp;
      if (sht.getEvent(&humidity, &temp))
      {
         return temp.temperature;
      }
      else
      {
         return NAN;
      }
   }
   virtual float readHumidity()
   {
      sensors_event_t humidity, temp;
      if (sht.getEvent(&humidity, &temp))
      {
         return humidity.relative_humidity;
      }
      else
      {
         return NAN;
      }
   }
   virtual bool readsBoth() { return true; }
   virtual void readBoth(float& tempF, float& hum)
   {
      sensors_event_t h, t;
      if (sht.getEvent(&h, &t))
      {
         tempF = Units::C2F(t.temperature);
         hum = h.relative_humidity;
      }
      else
      {
         tempF = NAN;
         hum = NAN;
      }
   }

   static SHT4xTempSensor* tryCreate()
   {
      Adafruit_SHT4x sht;
      sht.begin();
      uint32_t serial = sht.readSerial();

      if (serial > 0)
      {
         return new SHT4xTempSensor();
      }
      else
      {
         return nullptr;
      }
   }
};
