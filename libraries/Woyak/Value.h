#pragma once

#include <Arduino.h>
#include <Averager.h>

class Value {
 private:
   double _min;
   double _max;
   Averager _value;

 public:
   Value(int numSamples = 1)
       : _value(numSamples) {
      this->_min = NAN;
      this->_max = NAN;
   }

   void resetMinMax() {
      // reset by using the current value as the new min/max
      this->_min = this->_value.get();
      this->_max = this->_value.get();
   }

   void setValue(double value) {
      // store the value
      this->_value.set(value);

      // update min/max values
      double avgValue = this->_value.get();
      if (isnan(this->_min)) {
         this->_min = avgValue;
      } else {
         this->_min = min(avgValue, this->_min);
      }
      if (isnan(this->_max)) {
         this->_max = avgValue;
      } else {
         this->_max = max(avgValue, this->_max);
      }
   }

   double getMin() {
      return this->_min;
   }
   double getMax() {
      return this->_max;
   }
   double getValue() {
      return this->_value.get();
   }
};