#pragma once

class Averager {
private:
   double* _values;
   int _index;
   int _size;

public:
   Averager(unsigned int size) {
      this->_values = new double[size];
      this->_size = size;
      this->_index = 0;

      for (int i = 0; i < this->_size; i++) {
         this->_values[i] = NAN;
      }
   }

   ~Averager() {
      delete[] this->_values;
   }

   void set(double value) {
      this->_values[this->_index] = value;

      this->_index++;
      if (this->_index >= this->_size) {
         this->_index = 0;
      }
   }

   double get() {
      int count = 0;
      double sum = 0;
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