#pragma once

#include <Arduino.h>

/// <summary>
/// Defines a simple floating-point value store contract.
/// </summary>
class IValueStore
{
public:
   virtual ~IValueStore() = default;

   virtual float get() = 0;
   virtual bool set(float value) = 0;
};

/// <summary>
/// In-memory implementation of <see cref="IValueStore"/>.
/// </summary>
class ValueStoreSimple : public IValueStore
{
private:
   float _value = NAN;

public:
   float get() override
   {
      return _value;
   }

   bool set(float value) override
   {
      _value = value;
      return true;
   }
};
