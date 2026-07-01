#pragma once

#include "I2CTempSensor.h"
#include "ITempSensor.h"
#include "TempSensorCallibration.h"

// sensor types
#include "BME280TempSensor.h"
#include "DS18B20TempSensor.h"
#include "HDC302xTempSensor.h"
#include "MCP9808TempSensor.h"
#include "MS5837TempSensor.h"
#include "SHT3xTempSensor.h"
#include "SHT4xTempSensor.h"

#include <string>

///
/// <summary>
/// Null-object temperature sensor used when no physical sensor is detected.
/// </summary>
///
class NullSensor : public ITempSensor
{
public:
   bool exists() override { return false; }
   const char* type() override { return "None"; }
   const char* id() override { return "??"; }
   uint8_t address() override { return 0; }
   bool begin() override { return true; }
   float readTemperatureF() override { return NAN; }
   float readTemperatureC() override { return NAN; }
   float readHumidity() override { return NAN; }
   bool readsBoth() override { return false; }
   void readBoth(float& tempF, float& hum) override
   {
      tempF = NAN;
      hum = NAN;
   }
};

///
/// <summary>
/// Detects and wraps a concrete temperature sensor implementation.
/// Applies optional calibration offsets to returned readings.
/// </summary>
///
class TempSensor : public ITempSensor
{
private:
   // the actual sensor this class manages
   ITempSensor* _sensor = nullptr;

   /// <summary>
   /// Detects and creates the most appropriate sensor for the current hardware.
   /// </summary>
   /// <param name="print">True to print detection details to Serial.</param>
   /// <returns>A concrete sensor instance, or a NullSensor when none is detected.</returns>
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

      if (sensor == nullptr)
      {
         int8_t address = I2CTempSensor::detect(MS5837TempSensor::DEFAULT_I2C_ADDRESS);
         if (address != 0)
         {
            sensor = new MS5837TempSensor();
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
            Serial.println("Scanning I2C addresses...");
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
            if (!found)
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

   float _tempCorrectionF = 0;
   float _humCorrection = 0;

public:

   /// <summary>
   /// Detects and initializes a temperature sensor with Serial diagnostics enabled.
   /// </summary>
   /// <returns>True when initialization succeeds; otherwise false.</returns>
   bool begin() override { return begin(true); }

   /// <summary>
   /// Detects and initializes a temperature sensor.
   /// </summary>
   /// <param name="print">True to print detection details to Serial.</param>
   /// <returns>True when initialization succeeds; otherwise false.</returns>
   bool begin(bool print)
   {
      _sensor = _create(print);

      bool status = false;
      if (_sensor != nullptr)
      {
         status = _sensor->begin();

         if (status)
         {
            std::string id = _sensor->id();
            for (int i = 0; i < NUM_CORRECTIONS; i++)
            {
               if (id == CORRECTIONS[i].id)
               {
                  _tempCorrectionF = CORRECTIONS[i].tempF;
                  _humCorrection = CORRECTIONS[i].hum;
                  break;
               }
            }
         }
      }

      return status;
   }

   /// <summary>
   /// Initializes a DS18B20 temperature sensor on the specified one-wire pin.
   /// </summary>
   /// <param name="oneWirePin">GPIO pin connected to the DS18B20 data line.</param>
   /// <param name="print">True to print setup details to Serial.</param>
   /// <returns>True when initialization succeeds; otherwise false.</returns>
   bool begin(uint8_t oneWirePin, bool print)
   {
      if (print) Serial.println("Creating DS18B20 sensor");
      _sensor = new DS18B20TempSensor(oneWirePin);
      return _sensor->begin();
   }

   /// <summary>
   /// Uses and initializes a caller-provided sensor implementation.
   /// </summary>
   /// <param name="sensor">Pre-created sensor instance to use.</param>
   /// <param name="print">True to print setup details to Serial.</param>
   /// <returns>True when initialization succeeds; otherwise false.</returns>
   bool begin(ITempSensor* sensor, bool print)
   {
      if (print) Serial.println("Using provided sensor");
      _sensor = sensor;
      return _sensor->begin();
   }

   /// <summary>
   /// Indicates whether the active sensor is available.
   /// </summary>
   /// <returns>True when a backing sensor exists and reports available; otherwise false.</returns>
   bool exists() override
   {
      if (_sensor == nullptr)
      {
         Serial.println("TempSensor::exists() - No sensor created. Call begin()");
         return false;
      }

      return _sensor->exists();
   }
   /// <summary>
   /// Gets the detected sensor type name.
   /// </summary>
   /// <returns>Sensor type string, or an error message when no sensor is initialized.</returns>
   const char* type() override
   {
      if (_sensor == nullptr)
      {
         Serial.println("TempSensor::type() - No sensor created. Call begin()");
         return "No sensor created. Call begin()";
      }

      return _sensor->type();
   }
   /// <summary>
   /// Gets the active sensor I2C address when applicable.
   /// </summary>
   /// <returns>The sensor address, or 0 when no sensor is initialized.</returns>
   uint8_t address() override
   {
      if (_sensor == nullptr)
      {
         Serial.println("TempSensor::address() - No sensor created. Call begin()");
         return 0;
      }

      return _sensor->address();
   }
   /// <summary>
   /// Gets the configured temperature correction offset in Fahrenheit.
   /// </summary>
   /// <returns>Temperature correction applied to Fahrenheit readings.</returns>
   float tempCorrectionF()
   {
      return _tempCorrectionF;
   }

   /// <summary>
   /// Gets the configured humidity correction offset.
   /// </summary>
   /// <returns>Humidity correction applied to humidity readings.</returns>
   float humidityCorrection()
   {
      return _humCorrection;
   }

   /// <summary>
   /// Sets the temperature correction offset in Fahrenheit.
   /// </summary>
   /// <param name="correction">Correction value to add to Fahrenheit readings.</param>
   void setTempCorrectionF(float correction)
   {
      _tempCorrectionF = correction;
   }

   /// <summary>
   /// Sets the humidity correction offset.
   /// </summary>
   /// <param name="correction">Correction value to add to humidity readings.</param>
   void setHumidityCorrection(float correction)
   {
      _humCorrection = correction;
   }
   /// <summary>
   /// Gets the unique identifier for the active sensor when available.
   /// </summary>
   /// <returns>Sensor ID string, or an error message when no sensor is initialized.</returns>
   const char* id() override
   {
      if (_sensor == nullptr)
      {
         Serial.println("TempSensor::id() - No sensor created. Call begin()");
         return "No sensor created. Call begin()";
      }

      return _sensor->id();
   }
   /// <summary>
   /// Reads temperature in Fahrenheit from the active sensor.
   /// </summary>
   /// <returns>Temperature in Fahrenheit with correction applied, or NaN when unavailable.</returns>
   float readTemperatureF() override
   {
      if (_sensor == nullptr)
      {
         Serial.println("TempSensor::readTemperatureF() - No sensor created. Call begin()");
         return NAN;
      }

      return _sensor->readTemperatureF() + _tempCorrectionF;
   }
   /// <summary>
   /// Reads temperature in Celsius from the active sensor.
   /// </summary>
   /// <returns>Temperature in Celsius with correction applied, or NaN when unavailable.</returns>
   float readTemperatureC() override
   {
      if (_sensor == nullptr)
      {
         Serial.println("TempSensor::readTemperatureC() - No sensor created. Call begin()");
         return NAN;
      }

      return _sensor->readTemperatureC() + Units::F2C(_tempCorrectionF);
   }

   /// <summary>
   /// Reads relative humidity from the active sensor.
   /// </summary>
   /// <returns>Humidity with correction applied, or NaN when unavailable.</returns>
   float readHumidity() override
   {
      if (_sensor == nullptr)
      {
         Serial.println("TempSensor::readHumidity() - No sensor created. Call begin()");
         return NAN;
      }

      return _sensor->readHumidity() + _humCorrection;
   }
   /// <summary>
   /// Indicates whether the active sensor can return temperature and humidity together.
   /// </summary>
   /// <returns>True when combined readings are supported; otherwise false.</returns>
   bool readsBoth() override
   {
      if (_sensor == nullptr)
      {
         Serial.println("TempSensor::readsBoth() - No sensor created. Call begin()");
         return false;
      }

      return _sensor->readsBoth();
   }

   /// <summary>
   /// Reads both temperature (F) and humidity from the active sensor.
   /// </summary>
   /// <param name="tempF">Receives the Fahrenheit temperature reading.</param>
   /// <param name="hum">Receives the humidity reading.</param>
   void readBoth(float& tempF, float& hum) override
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

