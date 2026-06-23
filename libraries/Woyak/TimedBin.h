#pragma once

#include "Util.h"

//
// Keeps track of the number of events that occurs over a designated
// amount of time
//
class TimedBin
{
private:
   uint* _subBins;
   uint _numSubBins;
   ulong _currentSubBinStartTimeMicros;
   ulong _subBinDurationMicros;
   uint _index;
   uint _transitionSubBin;
   unsigned long (*_micros) ();

   void _advanceIfNeeded()
   {
	  while (Util::getSpan(_currentSubBinStartTimeMicros, _micros()) > _subBinDurationMicros)
	  {
		 _currentSubBinStartTimeMicros += _subBinDurationMicros;
		 _index++;
		 if (_index >= _numSubBins)
		 {
			_index = 0;
		 }
		 _transitionSubBin = _subBins[_index];
		 _subBins[_index] = 0;
	  }
   }

public:
   // public function for testing only. Allows us to substitute our own clock
   // within test cases of timing code.
   static unsigned long (*microsFunc) ();

   TimedBin(ulong durationMs, uint numSubBins=10)
   {
	  _numSubBins = numSubBins;
	  _subBins = new uint[numSubBins];
	  for (uint i = 0; i < _numSubBins; i++)
	  {
		 _subBins[i] = 0;
	  }
	  _subBinDurationMicros = (1000 * durationMs) / numSubBins;
	  _index = 0;
	  _transitionSubBin = 0;
	  if (microsFunc != nullptr)
	  {
		 _micros = microsFunc;
	  }
	  else
	  {
		 _micros = micros;
	  }
	  _currentSubBinStartTimeMicros = _micros();
   }

   ~TimedBin()
   {
	  delete[] _subBins;
   }

   void begin()
   {
	  _currentSubBinStartTimeMicros = _micros();
   }

   void add()
   {
	  _advanceIfNeeded();
	  _subBins[_index]++;
   }

   uint getCount()
   {
	  _advanceIfNeeded();

	  uint count = 0;
	  for (uint i = 0; i < _numSubBins; i++)
	  {
		 count += _subBins[i];
	  }
	  float partial = 1.0f - ((float)Util::getSpan(_currentSubBinStartTimeMicros, _micros())) / _subBinDurationMicros;
	  count += std::roundf(partial * _transitionSubBin);

	  return count;
   }

   // functions for debugging only
   uint getBinCount(int binIndex)
   {
	  if (binIndex == -1)
	  {
		 return _transitionSubBin;
	  }
	  else
	  {
		 return _subBins[binIndex];
	  }
   }

   void println() {
	  Serial.println("TimedBin: ");
	  Serial.println("  Num Sub Bins: " + String(_numSubBins));
	  Serial.println("  Time / Bin (ms): " + String(_subBinDurationMicros / 1000.0));
	  Serial.println("  Total Time (ms): " + String(_subBinDurationMicros * _numSubBins / 1000.0) + " ms");
	  Serial.println("  Bin Counts:");
	  for (uint i = 0; i < _numSubBins; i++)
	  {
		 Serial.println("    Bin[" + String(i) + "]: " + String(_subBins[i]));
	  }
	  Serial.println("    Transition Bin: " + String(_transitionSubBin));
   }
};

unsigned long (*TimedBin::microsFunc) () = nullptr;
