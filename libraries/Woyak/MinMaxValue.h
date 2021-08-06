#pragma once

#include <Arduino.h>
#include <ValueStore.h>

class MinMaxValue {
private:
   IValueStore* _min;
   IValueStore* _max;
   IValueStore* _value;

public:
   MinMaxValue(IValueStore* value = nullptr, IValueStore* min = nullptr, IValueStore* max = nullptr)
   {
      if (value == nullptr) {
         value = new ValueStoreSimple();
      }
      if (min == nullptr) {
         min = new ValueStoreSimple();
      }
      if (max == nullptr) {
         max = new ValueStoreSimple();
      }

      this->_value = value;
      this->_min = min;
      this->_max = max;
   }

   virtual ~MinMaxValue() {
      delete this->_value;
      delete this->_min;
      delete this->_max;
   }

   void resetMinMax() {
      // reset by using the current value as the new min/max
      this->_min->set(this->_value->get());
      this->_max->set(this->_value->get());
   }

   void setValue(float value) {
      // store the value
      this->_value->set(value);

      // get the value - store mechanism may manipulate it, e.g. average of last 5 values
      value = this->_value->get();

      // update min/max values
      if (isnan(this->_min->get())) {
         this->_min->set(value);
      }
      else {
         this->_min->set(min(value, this->_min->get()));
      }
      if (isnan(this->_max->get())) {
         this->_max->set(value);
      }
      else {
         this->_max->set(max(value, this->_max->get()));
      }
   }

   float getMin() {
      return this->_min->get();
   }
   float getMax() {
      return this->_max->get();
   }
   float getValue() {
      return this->_value->get();
   }
};