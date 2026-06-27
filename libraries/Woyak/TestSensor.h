#pragma once

#include <Arduino.h>
#include "CapacitorSensor.h"
#include "TempSensor.h"

/// <summary>
/// Abstract sensor contract used by general sensor sketches.
/// </summary>
class TestSensor
{
public:
   virtual ~TestSensor() = default;
   virtual void begin() = 0;
   virtual float readValue() = 0;
   virtual float rawRate() = 0;
   virtual const char* unit() const = 0;
   virtual const char* title() const = 0;
   virtual const char* sourceDescription() const = 0;
};

/// <summary>
/// Capacitor sensor implementation.
/// </summary>
class CapacitorTestSensor : public TestSensor
{
private:
   CapacitorSensor _sensor;

public:
   CapacitorTestSensor(
      uint8_t chargePin,
      uint8_t sensePin,
      uint16_t dischargeDelayMicros,
      size_t averageSamples = 32)
      : _sensor(chargePin, sensePin, dischargeDelayMicros, averageSamples)
   {
   }

   void begin() override
   {
      _sensor.begin();
   }

   float readValue() override
   {
      return _sensor.chargeTimeMicros();
   }

   float rawRate() override
   {
      return _sensor.rate();
   }

   const char* unit() const override
   {
      return "us";
   }

   const char* title() const override
   {
      return "Capacitor Noise";
   }

   const char* sourceDescription() const override
   {
      return "CapacitorSensor rolling-average charge time (microseconds)";
   }
};

/// <summary>
/// Temperature sensor implementation.
/// </summary>
class TempTestSensor : public TestSensor
{
private:
   TempSensor _sensor;

public:
   void begin() override
   {
      _sensor.begin();
   }

   float readValue() override
   {
      return _sensor.readTemperatureF();
   }

   float rawRate() override
   {
      return NAN;
   }

   const char* unit() const override
   {
      return "F";
   }

   const char* title() const override
   {
      return "Sensor Noise";
   }

   const char* sourceDescription() const override
   {
      return "TempSensor temperature read (Fahrenheit)";
   }
};
