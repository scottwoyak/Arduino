#pragma once

#include <ValueStore.h>

class RunningAverager : public IValueStore {
private:
   float* _values;
   int _index;
   int _size;
   float _avg;

public:
   RunningAverager(unsigned int size) {
      this->_values = new float[size];
      this->_size = size;
      this->_index = 0;
      this->_avg = NAN;

      for (int i = 0; i < this->_size; i++) {
         this->_values[i] = NAN;
      }
   }

   ~RunningAverager() {
      delete[] this->_values;
   }

   boolean set(float value) {
      this->_values[this->_index] = value;

      this->_index++;
      if (this->_index >= this->_size) {
         this->_index = 0;
      }

      this->_avg = NAN;

      return true;
   }
   float getLast()
   {
      int index = this->_index - 1;
      if (index < 0)
      {
         index = this->_size - 1;
      }
      return this->_values[index];
   }

   float get() {
      if (isnan(this->_avg)) {
         int count = 0;
         float sum = 0;
         for (int i = 0; i < this->_size; i++) {
            if (isnan(this->_values[i]) == false) {
               count++;
               sum += this->_values[i];
            }
         }

         if (count > 0) {
            this->_avg = sum / count;
         }
      }

      return this->_avg;
   }
};