#pragma once

#include <CapacitorSensor.h>

class CapacitorDepthSensor
{
private:
   CapacitorSensor _sensor;
   float _zeroTime;
   float _fullTime;
   float _fullDepth;

public:
   CapacitorDepthSensor(
      uint8_t chargePin,
      uint8_t sensePin,
      float zeroTime,
      float fullTime,
      float fullDepth) : _sensor(chargePin, sensePin)
   {
      _zeroTime = zeroTime;
      _fullTime = fullTime;
      _fullDepth = fullDepth;
   }

   float getDepth()
   {
      float chargeTime = _sensor.chargeTimeUs();
      float depth = _fullDepth * ((chargeTime - _zeroTime) / (_fullTime - _zeroTime));
      return depth;
   }

   void begin()
   {
      _sensor.begin();
   }

   bool loop()
   {
      return _sensor.loop();
   }
};
