#pragma once

#include "ValueStore.h"
#include "FramSpiEx.h"

/// <summary>
/// FRAM-backed implementation of <see cref="IValueStore"/>.
/// </summary>
class ValueStoreFram : public IValueStore
{
private:
   FramSpiEx* _fram;
   uint32_t _address = 0;

public:
   ValueStoreFram(FramSpiEx* fram, uint32_t address = 0)
   {
      _fram = fram;
      _address = address;
   }

   float get() override
   {
      return _fram->readFloat(_address);
   }

   bool set(float value) override
   {
      _fram->writeEnable(true);
      _fram->writeFloat(_address, value);
      _fram->writeEnable(false);
      return true;
   }
};
