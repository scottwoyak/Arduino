#pragma once

#include <ValueStore.h>
#include <limits>

class AccumulatingAverager : public IValueStore {
private:
   float _total;
   unsigned long _count;
   float _minRange;
   float _maxRange;
   float _min;
   float _max;
   float _avg;

public:
   AccumulatingAverager(float minRange = -__FLT_MAX__, float maxRange = __FLT_MAX__) {
      this->_minRange = minRange;
      this->_maxRange = maxRange;
      this->reset();
      this->_avg = NAN;
   }

   void set(float value) {
      // ignore out of range values
      if (value > this->_maxRange || value < this->_minRange) {
         return;
      }

      if (isnan(value)) {
         return;
      }

      this->_min = isnan(this->_min) ? value : min(this->_min, value);
      this->_max = isnan(this->_max) ? value : max(this->_max, value);

      this->_total += value;
      this->_count++;
      this->_avg = NAN;
   }

   float get() {
      if (isnan(this->_avg)) {
         if (this->_count > 0) {
            this->_avg = this->_total / this->_count;
         }
      }
      return this->_avg;
   }

   float getMin() {
      return this->_min;
   }

   float getMax() {
      return this->_max;
   }

   void reset() {
      this->_count = 0;
      this->_total = 0;
      this->_min = NAN;
      this->_max = NAN;
      this->_avg = NAN;
   }
};