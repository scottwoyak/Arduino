#pragma once

#include "I2C.h"
#include "ILightSensor.h"
#include "Util.h"

// sensor types
#include "BH1750LightSensor.h"
#include "VEML7700LightSensor.h"

///
/// <summary>
/// Null-object light sensor used when no physical sensor is detected.
/// </summary>
///
class NullLightSensor : public ILightSensor
{
public:
   bool begin() override { return false; }
   const char* type() const override { return "None"; }
   uint8_t address() override { return 0; }
   bool exists() override { return false; }
   float readLux() override { return NAN; }
};

///
/// <summary>
/// Detects and wraps a concrete ambient light sensor implementation based on which I2C
/// address responds. Supports the VEML7700 (0x10) and BH1750 (0x23 or 0x5C).
/// </summary>
///
class LightSensor : public ILightSensor
{
private:
   // the actual sensor this class manages. Defaults to a NullLightSensor so
   // callers never need to guard against a null pointer.
   ILightSensor* _sensor = new NullLightSensor();
   bool _beginCalled = false;

   ///
   /// <summary>
   /// Detects and creates the sensor matching the I2C device on the bus.
   /// </summary>
   /// <param name="print">True to print detection details to Serial.</param>
   /// <returns>A concrete sensor instance, or a NullLightSensor when none is detected.</returns>
   ///
   ILightSensor* _create(bool print)
   {
      if (print) Serial.println("Detecting Light Sensor...");

      ILightSensor* sensor = nullptr;

      if (I2C::exists(VEML7700LightSensor::I2C_ADDRESS))
      {
         sensor = new VEML7700LightSensor();
      }
      else if (I2C::exists(BH1750LightSensor::I2C_ADDRESS_LOW))
      {
         sensor = new BH1750LightSensor(BH1750LightSensor::I2C_ADDRESS_LOW);
      }
      else if (I2C::exists(BH1750LightSensor::I2C_ADDRESS_HIGH))
      {
         sensor = new BH1750LightSensor(BH1750LightSensor::I2C_ADDRESS_HIGH);
      }

      if (sensor == nullptr)
      {
         if (print) Serial.println("  No light sensor detected");
         return new NullLightSensor();
      }

      if (print)
      {
         Serial.println(String("  Found: ") + sensor->type() + " at 0x" + String(sensor->address(), HEX));
      }

      return sensor;
   }

public:
   ~LightSensor()
   {
      delete _sensor;
   }

   ///
   /// <summary>
   /// Detects and initializes a light sensor with Serial diagnostics enabled.
   /// </summary>
   /// <returns>True when a sensor was detected and initialized; otherwise false.</returns>
   ///
   bool begin() override { return begin(true); }

   ///
   /// <summary>
   /// Detects and initializes a light sensor. Must only be called once; calling it again is a bug.
   /// </summary>
   /// <param name="print">True to print detection details to Serial.</param>
   /// <returns>True when a sensor was detected and initialized; otherwise false.</returns>
   ///
   bool begin(bool print)
   {
      ASSERT(!_beginCalled);

      _beginCalled = true;
      delete _sensor;
      _sensor = _create(print);
      return _sensor->begin();
   }

   ///
   /// <summary>
   /// Indicates whether a physical sensor was detected.
   /// </summary>
   /// <returns>True when a sensor is present; otherwise false.</returns>
   ///
   bool exists() override { return _sensor->exists(); }

   ///
   /// <summary>
   /// Gets the detected sensor type name.
   /// </summary>
   /// <returns>Sensor type string, or "None" when no sensor was detected.</returns>
   ///
   const char* type() const override { return _sensor->type(); }

   ///
   /// <summary>
   /// Gets the sensor I2C address.
   /// </summary>
   /// <returns>The sensor address, or 0 when no sensor was detected.</returns>
   ///
   uint8_t address() override { return _sensor->address(); }

   ///
   /// <summary>
   /// Reads the ambient light level from the active sensor.
   /// </summary>
   /// <returns>Light level in lux, or NaN when unavailable.</returns>
   ///
   float readLux() override { return _sensor->readLux(); }
};
