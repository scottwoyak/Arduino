#pragma once

#include <ValueStore.h>
#include <FramSpiEx.h>

class ValueStoreFram : public IValueStore {
private:
   FramSpiEx* _fram;
   uint32_t _address = 0;

public:
   ValueStoreFram(FramSpiEx* fram, uint32_t address = 0) {
      this->_fram = fram;
      this->_address = address;
   }

   float get() {
      return this->_fram->readFloat(this->_address);
   }

   void set(float value) {
      this->_fram->writeEnable(true);
      this->_fram->writeFloat(this->_address, value);
      this->_fram->writeEnable(false);
   }
};