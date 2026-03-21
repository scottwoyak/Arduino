#pragma once

#include <I2CTempSensor.h>
#include <Adafruit_MCP9808.h>

//-------------------------------------------------------------------------------------------------
class MCP9808TempSensor : public I2CTempSensor
{
private:
   Adafruit_MCP9808 mcp;

public:
   MCP9808TempSensor(uint8_t resolution, uint8_t address) : I2CTempSensor("MCP9808", address)
   {
      // Mode Resolution SampleTime
      //  0    0.5°C       30 ms
      //  1    0.25°C      65 ms
      //  2    0.125°C     130 ms
      //  3    0.0625°C    250 ms
      mcp.setResolution(resolution);
   }
   virtual bool begin() { return mcp.begin(getAddress()); }
   virtual float readTemperatureF() { return mcp.readTempF(); }
   virtual float readTemperatureC() { return mcp.readTempF(); }
   virtual float readHumidity() { return NAN; }
   virtual bool readsBoth() { return false; }
   virtual void readBoth(float& tempF, float& hum) { tempF = readTemperatureF(); hum = readHumidity(); }
};
