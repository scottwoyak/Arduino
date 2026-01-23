#include "TempSensor.h"

//------------------------------------------------------------------------------------------------
bool TempSensor::begin(bool print)
{
   _sensor = _create(print);
   return _sensor->begin();
}

//------------------------------------------------------------------------------------------------
const char* TempSensor::info()
{
   if (_sensor == NULL)
   {
      Serial.println("No sensor created. Call begin()");
      return "No sensor created. Call begin()";
   }

   return _sensor->info();
}

//------------------------------------------------------------------------------------------------
bool TempSensor::exists()
{
   if (_sensor == NULL)
   {
      Serial.println("No sensor created. Call begin()");
      return false;
   }

   return _sensor->exists();
}

//------------------------------------------------------------------------------------------------
float TempSensor::readTemperatureF()
{
   if (_sensor == NULL)
   {
      Serial.println("No sensor created. Call begin()");
      return NAN;
   }

   return _sensor->readTemperatureF();
}

//------------------------------------------------------------------------------------------------
float TempSensor::readTemperatureC()
{
   if (_sensor == NULL)
   {
      Serial.println("No sensor created. Call begin()");
      return NAN;
   }

   return _sensor->readTemperatureC();
}

//------------------------------------------------------------------------------------------------
float TempSensor::readHumidity()
{
   if (_sensor == NULL)
   {
      Serial.println("No sensor created. Call begin()");
      return NAN;
   }

   return _sensor->readHumidity();
}

//-------------------------------------------------------------------------------------------------
ITempSensor* TempSensor::_create(bool print)
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
      // SHT sensors all use the same address, so we need extra code to determine the version
      int8_t address = I2CTempSensor::detect(SHT4x_DEFAULT_ADDR);
      if (address != 0)
      {
         Adafruit_SHT4x sht;
         sht.begin();
         uint32_t serial = sht.readSerial();

         if (print)
         {
            Serial.print("Determining SHT version from serial number: ");
            Serial.println(serial);
         }

         if (serial > 0)
         {
            sensor = new SHT4xSensor(address);
         }
         else
         { 
            sensor = new SHT31Sensor(address);
         }
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
