#pragma once

#include <ValueStore.h>
#include <limits>

class AccumulatingAverager : public IValueStore {
private:
   float _total;
   unsigned long _count;
   unsigned long _badCount;
   float _minRange;
   float _maxRange;
   float _min;
   float _max;
   float _avg = NAN;
   float _last = NAN;

public:
   AccumulatingAverager() {
      this->_minRange = -__FLT_MAX__;
      this->_maxRange = __FLT_MAX__;
      this->reset();
   }

   AccumulatingAverager(float minRange, float maxRange) {
      this->_minRange = minRange;
      this->_maxRange = maxRange;
      this->reset();
   }

   boolean set(float value) {
      // ignore out of range values
      if (value > this->_maxRange || value < this->_minRange) {
         this->_badCount++;
         this->_last = NAN;
         return false;
      }

      if (isnan(value)) {
         this->_badCount++;
         this->_last = NAN;
         return false;
      }

      this->_min = isnan(this->_min) ? value : min(this->_min, value);
      this->_max = isnan(this->_max) ? value : max(this->_max, value);

      this->_last = value;
      this->_total += value;
      this->_count++;
      this->_avg = NAN;

      return true;
   }

   float get() {
      if (isnan(this->_avg)) {
         if (this->_count > 0) {
            this->_avg = this->_total / this->_count;
         }
      }
      return this->_avg;
   }

   float get(bool lastValue) {
      if (lastValue) {
         return this->_last;
      }
      else {
         return get();
      }
   }

   float last() {
      return this->_last;
   }

   float getMin() {
      return this->_min;
   }

   float getMax() {
      return this->_max;
   }

   void reset() {
      this->_count = 0;
      this->_badCount = 0;
      this->_total = 0;
      this->_min = NAN;
      this->_max = NAN;
      this->_avg = NAN;
   }

   unsigned long getCount() {
      return this->_count;
   }

   unsigned long getBadCount() {
      return this->_badCount;
   }

   String getValues() {
      String msg = "";
      msg += "  Good: " + String(this->getCount()) + "\n";
      msg += "  Bad : " + String(this->getBadCount()) + "\n";
      msg += "  Min : " + String(this->getMin()) + "\n";
      msg += "  Max : " + String(this->getMax()) + "\n";
      msg += "  Avg : " + String(this->get()) + "\n";
      return msg;
   }
};