#pragma once

#include <ValueStore.h>

class AccumulatingAverager : public IValueStore {
private:
   float _total;
   unsigned long _count;

public:
   AccumulatingAverager() {
      this->_total = NAN;
      this->_count = 0;
   }

   void set(float value) {
      if (this->_count == 0) {
         this->_total = value;
      }
      else {
         this->_total += value;
      }

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
      this->_total = NAN;
   }
};