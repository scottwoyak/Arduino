#pragma once

class IValueStore {
public:
   virtual float get() = 0;
   virtual void set(float value) = 0;
};

class ValueStoreSimple : public IValueStore {
private:
   float _value = NAN;

public:
   virtual float get() { return this->_value; }
   virtual void set(float value) { this->_value = value; }
};