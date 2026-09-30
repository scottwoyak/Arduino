#pragma once

#include "ILightSensor.h"

#include <Wire.h>

///
/// <summary>
/// Light sensor implementation for the BH1750 (GY-302 breakout), talking directly over I2C
/// so no external library is needed. Runs in continuous high resolution mode.
/// </summary>
///
class BH1750LightSensor : public ILightSensor
{
private:
   static constexpr uint8_t CMD_POWER_ON = 0x01;
   static constexpr uint8_t CMD_CONTINUOUS_HIGH_RES = 0x10;
   static constexpr float COUNTS_PER_LUX = 1.2f;

   uint8_t _address;

   bool _write(uint8_t command)
   {
      Wire.beginTransmission(_address);
      Wire.write(command);
      return Wire.endTransmission() == 0;
   }

public:
   // 0x23 when the ADDR pin is low (default), 0x5C when it is high
   static constexpr uint8_t I2C_ADDRESS_LOW = 0x23;
   static constexpr uint8_t I2C_ADDRESS_HIGH = 0x5C;

   ///
   /// <summary>
   /// Creates a BH1750 sensor.
   /// </summary>
   /// <param name="address">I2C address of the sensor.</param>
   ///
   explicit BH1750LightSensor(uint8_t address = I2C_ADDRESS_LOW)
   {
      _address = address;
   }

   bool begin() override
   {
      return _write(CMD_POWER_ON) && _write(CMD_CONTINUOUS_HIGH_RES);
   }

   const char* type() const override { return "BH1750"; }
   uint8_t address() override { return _address; }
   bool exists() override { return true; }

   float readLux() override
   {
      if (Wire.requestFrom(_address, (uint8_t)2) != 2)
      {
         return NAN;
      }

      uint16_t counts = (uint16_t)Wire.read() << 8;
      counts |= Wire.read();
      return counts / COUNTS_PER_LUX;
   }
};
