#include "TempSensor.h"


//-------------------------------------------------------------------------------------------------
ITempSensor* _tryCreateHTC(bool print)
{
   if (print)
   {
      Serial.print("  Checking HTC302... ");
   }

   Adafruit_HDC302x hdc;
   hdc.begin();
   uint16_t id = hdc.readManufacturerID();

   if (print)
   {
      if (id > 0)
      {
         Serial.print("Success. ID: ");
         Serial.println(id);
      }
      else
      {
         Serial.println("Nope");
      }
   }

   if (id > 0)
   {
      return new HDC302xSensor();
   }
   else
   {
      return nullptr;
   }
}

//-------------------------------------------------------------------------------------------------
ITempSensor* _tryCreateSHT4x(bool print)
{
   if (print)
   {
      Serial.print("  Checking SHT4x... ");
   }

   Adafruit_SHT4x sht;
   sht.begin();
   uint32_t serial = sht.readSerial();

   if (print)
   {
      if (serial > 0)
      {
         Serial.print("Success. Serial: ");
         Serial.println(serial);
      }
      else
      {
         Serial.println("Nope");
      }
   }

   if (serial > 0)
   {
      return new SHT4xSensor();
   }
   else
   {
      return nullptr;
   }
}

//-------------------------------------------------------------------------------------------------
ITempSensor* TempSensor::_create(bool print)
{
   if (print)
   {
      Serial.println("Detecting Temperature Sensor...");
   }

   ITempSensor* sensor = nullptr;

   if (sensor == nullptr)
   {
      int8_t address = I2CTempSensor::detect(BME280_ADDRESS, 2);
      if (address != 0)
      {
         sensor = new BME280Sensor(address);
      }
   }

   if (sensor == nullptr)
   {
      // Multiple sensors use 0x44 as an address
      int8_t address = I2CTempSensor::detect(0x44);
      if (address != 0)
      {
         if (print)
         {
            Serial.println("  Something found at 0x44");
         }

         sensor = _tryCreateHTC(print);

         if (sensor == nullptr)
         {
            sensor = _tryCreateSHT4x(print);
         }

         if (sensor == nullptr)
         {
            if (print)
            {
               Serial.println("  Defaulting to SHT3x");
            }
            sensor = new SHT31Sensor(address);
         }
      }
   }

   if (sensor == nullptr)
   {
      int8_t address = I2CTempSensor::detect(MCP9808_I2CADDR_DEFAULT, 8);
      if (address != 0)
      {
         sensor = new MCP9808Sensor(3, MCP9808_I2CADDR_DEFAULT);
      }
   }

   if (print)
   {
      if (sensor != nullptr)
      {
         Serial.println(String("Found: ") + sensor->type() + " at 0x" + String(sensor->address(), HEX));
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

   if (sensor == nullptr)
   {
      sensor = new NullSensor();
   }

   return sensor;
}


