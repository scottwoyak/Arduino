#include <TimedAverager.h>

class TimedHistogram {
private:
   TimedAverager** _bins;
   uint _numBins;
   float _min;
   float _max;

public:
   TimedHistogram(float min, float max, uint numBins, uint ms) {

      this->_min = min;
      this->_max = max;
      this->_numBins = numBins;
      this->_bins = new TimedAverager * [numBins];

      for (int i = 0; i < numBins; i++) {
         this->_bins[i] = new TimedAverager(ms);
      }
   }

   float min() {
      float value = __FLT_MAX__;
      for (int i = 0; i < this->_numBins; i++) {
         value = std::min(value, this->_bins[i]->getMin());
      }

      return value;
   }

   float max() {
      float value = -__FLT_MAX__;
      for (int i = 0; i < this->_numBins; i++) {
         value = std::max(value, this->_bins[i]->getMax());
      }

      return value;
   }

   void set(float value) {

      if (isnan(value)) {
         return;
      }

      // figure out which bin
      uint bin = floor((value - this->_min) / (this->_max - this->_min) * this->_numBins);

      bin = std::min(bin, this->_numBins - 1);
      bin = std::max(bin, (uint)0);

      this->_bins[bin]->set(value);
   }

   void get(float* values) {
      float maxValue;
      for (int i = 0; i < this->_numBins; i++) {
         values[i] = this->_bins[i]->getCount();
         maxValue = std::max(maxValue, values[i]);
      }

      for (int i = 0; i < this->_numBins; i++) {
         values[i] /= maxValue;
      }
   }
};
