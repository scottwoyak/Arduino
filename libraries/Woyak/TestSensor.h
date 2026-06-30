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
};

/// <summary>
/// Capacitor sensor implementation.
/// </summary>
class CapacitorTestSensor : public TestSensor
{
private:
   CapacitorSensor _sensor;
   uint8_t _chargePins[4] = { 0, 0, 0, 0 };
   const char* _chargePinLabels[4] = { "", "", "", "" };
   size_t _chargePinCount = 0;
   size_t _chargePinIndex = 0;

public:
   CapacitorTestSensor(
      uint8_t chargePin,
      uint8_t sensePin,
      uint16_t dischargeDelayMicros,
      size_t averageSamples = 32)
      : _sensor(chargePin, sensePin, dischargeDelayMicros, averageSamples)
   {
      _chargePins[0] = chargePin;
      _chargePinLabels[0] = "charge";
      _chargePinCount = 1;
      _chargePinIndex = 0;
   }

   CapacitorTestSensor(
      const uint8_t* chargePins,
      const char* const* chargePinLabels,
      size_t chargePinCount,
      uint8_t sensePin,
      uint16_t dischargeDelayMicros,
      size_t averageSamples = 32)
      : _sensor((chargePinCount > 0) ? chargePins[0] : 0, sensePin, dischargeDelayMicros, averageSamples)
   {
      _chargePinCount = min(chargePinCount, static_cast<size_t>(4));
      for (size_t i = 0; i < _chargePinCount; i++)
      {
         _chargePins[i] = chargePins[i];
         _chargePinLabels[i] = (chargePinLabels != nullptr) ? chargePinLabels[i] : "charge";
      }

      if (_chargePinCount == 0)
      {
         _chargePins[0] = 0;
         _chargePinLabels[0] = "charge";
         _chargePinCount = 1;
      }

      _chargePinIndex = 0;
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

   bool supportsChargePinRotation() const
   {
      return _chargePinCount > 1;
   }

   void rotateChargePin()
   {
      if (_chargePinCount <= 1)
      {
         return;
      }

      _chargePinIndex = (_chargePinIndex + 1) % _chargePinCount;
      _sensor.setChargePin(_chargePins[_chargePinIndex]);
   }

   const char* chargePinLabel() const
   {
      if (_chargePinCount == 0)
      {
         return "n/a";
      }

      return _chargePinLabels[_chargePinIndex];
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
};
