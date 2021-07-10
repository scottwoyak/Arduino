#pragma once

#include <Arduino.h>

class Value {
private:
  double _min;
  double _max;
  double _value;

public:
  Value() {
    this->_min = NAN;
    this->_max = NAN;
    this->_value = NAN;
  }

  void resetMinMax() {
    // reset by using the current value as the new min/max
    this->_min = this->_value;
    this->_max = this->_value;
  }

  void setValue(double value) {
    // store the value
    this->_value = value;

    // update min/max values
    if (isnan(this->_min)) {
      this->_min = value;
    } else {
      this->_min = min(value, this->_min);
    }
    if (isnan(this->_max)) {
      this->_max = value;
    } else {
      this->_max = max(value, this->_max);
    }
  }

  double getMin() { return this->_min; }
  double getMax() { return this->_max; }
  double getValue() { return this->_value; }
};