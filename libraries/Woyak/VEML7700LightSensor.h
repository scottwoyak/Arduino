#pragma once

#include "ILightSensor.h"

#include <Adafruit_VEML7700.h>

///
/// <summary>
/// Light sensor implementation for the VEML7700 (fixed I2C address 0x10).
/// </summary>
///
class VEML7700LightSensor : public ILightSensor
{
private:
   Adafruit_VEML7700 _sensor;

public:
   static constexpr uint8_t I2C_ADDRESS = 0x10;

   bool begin() override { return _sensor.begin(); }
   const char* type() const override { return "VEML7700"; }
   uint8_t address() override { return I2C_ADDRESS; }
   bool exists() override { return true; }
   float readLux() override { return _sensor.readLux(VEML_LUX_NORMAL_NOWAIT); }
};
