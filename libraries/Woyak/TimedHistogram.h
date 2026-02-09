#include <TimedAverager.h>

class TimedHistogram 
{
private:
   TimedAverager** _bins;
   uint _numBins;
   RangeF _range;

public:
   TimedHistogram(RangeF range, uint numBins, uint ms) 
   {
      _range = range;
      _numBins = numBins;
      _bins = new TimedAverager * [numBins];

      for (int i = 0; i < numBins; i++) 
      {
         _bins[i] = new TimedAverager(ms);
      }
   }

   uint getNumBinsX() 
   {
      return _numBins;
   }

   float min() 
   {
      float value = __FLT_MAX__;
      for (int i = 0; i < _numBins; i++) 
      {
         value = std::min(value, this->_bins[i]->getMin());
      }

      return value;
   }

   float max() 
   {
      float value = -__FLT_MAX__;
      for (int i = 0; i < _numBins; i++)
      {
         value = std::max(value, this->_bins[i]->getMax());
      }

      return value;
   }

   void set(float value) 
   {
      if (isnan(value)) 
      {
         return;
      }

      // figure out which bin
      uint bin = floor((value - _range.min) / (_range.max - _range.min) * _numBins);

      bin = std::min(bin, _numBins - 1);
      bin = std::max(bin, 0u);

      this->_bins[bin]->set(value);
   }

   void get(float* values) 
   {
      float maxValue;
      for (int i = 0; i < _numBins; i++)
      {
         values[i] = this->_bins[i]->getCount();
         maxValue = std::max(maxValue, values[i]);
      }

      for (int i = 0; i < _numBins; i++)
      {
         values[i] /= maxValue;
      }
   }

   RangeU16 getCurrentBinsRange()
   {
      int firstBin = -1;
      int lastBin = -1;

      for (uint i = 0; i < _numBins; i++)
      {
         if (firstBin == -1 && _bins[i]->getCount() > 0)
         {
            firstBin = i;
         }

         if (_bins[i]->getCount() > 0)
         {
            lastBin = i;
         }
      }

      // if not bins had values, use the full range
      if (firstBin == -1)
      {
         firstBin = 0;
         lastBin = _numBins - 1;
      }

      return RangeU16(firstBin, lastBin);
   }

   RangeF getCurrentValuesRange()
   {
      RangeU16 bins = getCurrentBinsRange();
      float span = (_range.max - _range.min) / _numBins;
      float min = _range.min + ((float)bins.min / _numBins) * (_range.max - _range.min);
      float max = _range.min + ((float)bins.max / _numBins) * (_range.max - _range.min) + span;
      return RangeF(min, max);
   }

   RangeF getBinRange(uint16_t bin)
   {
      float span = (_range.max - _range.min) / _numBins;
      float min = _range.min + ((float) bin / _numBins) * (_range.max - _range.min);
      float max = min + span;
      return RangeF(min, max);
   }
};
