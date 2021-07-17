#pragma once

#include <ValueStore.h>

class Averager : public IValueStore {
private:
   float* _values;
   int _index;
   int _size;

public:
   Averager(unsigned int size) {
      this->_values = new float[size];
      this->_size = size;
      this->_index = 0;

      for (int i = 0; i < this->_size; i++) {
         this->_values[i] = NAN;
      }
   }

   ~Averager() {
      delete[] this->_values;
   }

   void set(float value) {
      this->_values[this->_index] = value;

      this->_index++;
      if (this->_index >= this->_size) {
         this->_index = 0;
      }
   }

   float get() {
      int count = 0;
      float sum = 0;
      for (int i = 0; i < this->_size; i++) {
         if (isnan(this->_values[i]) == false) {
            count++;
            sum += this->_values[i];
         }
      }

      if (count == 0) {
         return NAN;
      }
      else {
         return sum / count;
      }
   }
};