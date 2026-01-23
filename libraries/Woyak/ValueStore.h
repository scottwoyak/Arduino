#pragma once

#include <Arduino.h>

class IValueStore {
public:
   virtual float get() = 0;
   virtual boolean set(float value) = 0;
   virtual ~IValueStore() {};
};

class ValueStoreSimple : public IValueStore {
private:
   float _value = NAN;

public:
   virtual float get() { return this->_value; }
   virtual boolean set(float value) { this->_value = value; return true; }
};