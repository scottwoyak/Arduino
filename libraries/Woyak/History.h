class History {
   uint _index;
   float* _values;
   uint _size;

public:
   History(uint size) {
      this->_size = size;
      this->_values = new float[size];

      for (int i = 0; i < size; i++) {
         this->_values[i] = 0;
      }

      this->_index = 0;
   }

   ~History() {
      delete this->_values;
   }

   void set(float value) {
      this->_index++;
      if (this->_index >= this->_size) {
         this->_index = 0;
      }

      this->_values[this->_index] = value;
   }

   float get(uint index) {
      int i = this->_index - index;

      if (i < 0) {
         i += this->_size;
      }

      /*
      Serial.print("size: ");
      Serial.print(this->_size);
      Serial.print(" index: ");
      Serial.print(index);
      Serial.print(" i: ");
      Serial.print(i);
      Serial.print(" value: ");
      Serial.print(this->_values[i]);
      Serial.println();
      */

      return this->_values[i];
   }
};
