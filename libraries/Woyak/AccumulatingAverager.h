#pragma once

#include <ValueStore.h>
#include <limits>

class AccumulatingAverager : public IValueStore {
private:
   float _total;
   unsigned long _count;
   float _min;
   float _max;

public:
   AccumulatingAverager(float min = -__FLT_MAX__, float max = __FLT_MAX__) {
      this->_min = min;
      this->_max = max;
      this->_total = 0;
      this->_count = 0;
   }

   void set(float value) {
      // ignore out of range values
      if (value > this->_max || value < this->_min) {
         return;
      }

      this->_total += value;
      this->_count++;
   }

   float get() {
      if (this->_count == 0) {
         return NAN;
      }
      else {
         return this->_total / this->_count;
      }
   }

   void reset() {
      this->_count = 0;
      this->_total = 0;
   }
};