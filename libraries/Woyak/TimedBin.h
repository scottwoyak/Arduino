#pragma once

#include <Util.h>

//
// Keeps track of the number of events that occurs over a designated
// amount of time
//
class TimedBin
{
private:
   uint* _bins;
   uint _numBins;
   ulong _binStartTimeMicros;
   ulong _binDurationMicros;
   uint _index;
   uint _transitionBin;
   unsigned long (*_micros) ();

   void _advanceIfNeeded()
   {
	  while (Util::getSpan(_binStartTimeMicros, _micros()) > _binDurationMicros)
	  {
		 _binStartTimeMicros += _binDurationMicros;
		 _index++;
		 if (_index >= _numBins)
		 {
			_index = 0;
		 }
		 _transitionBin = _bins[_index];
		 _bins[_index] = 0;
	  }
   }

public:
   // public function for testing only. Allows us to substitute our own clock
   // within test cases of timing code.
   static unsigned long (*microsFunc) ();

   TimedBin(ulong durationMs, uint numBins)
   {
	  _numBins = numBins;
	  _bins = new uint[numBins];
	  for (uint i = 0; i < _numBins; i++)
	  {
		 _bins[i] = 0;
	  }
	  _binDurationMicros = (1000 * durationMs) / numBins;
	  _index = 0;
	  _transitionBin = 0;
	  if (microsFunc != nullptr)
	  {
		 _micros = microsFunc;
	  }
	  else
	  {
		 _micros = micros;
	  }
	  _binStartTimeMicros = _micros();
   }

   ~TimedBin()
   {
	  delete[] _bins;
   }

   void begin()
   {
	  _binStartTimeMicros = _micros();
   }

   void add()
   {
	  _advanceIfNeeded();
	  _bins[_index]++;
   }

   uint getCount()
   {
	  _advanceIfNeeded();

	  uint count = 0;
	  for (uint i = 0; i < _numBins; i++)
	  {
		 count += _bins[i];
	  }
	  float partial = 1.0f - ((float)Util::getSpan(_binStartTimeMicros, _micros())) / _binDurationMicros;
	  count += std::roundf(partial * _transitionBin);

	  return count;
   }

   // functions for debugging only
   uint getBinCount(int binIndex)
   {
	  if (binIndex == -1)
	  {
		 return _transitionBin;
	  }
	  else
	  {
		 return _bins[binIndex];
	  }
   }

   void println() {
	  Serial.println("TimedBin: ");
	  Serial.println("  Num Bins: " + String(_numBins));
	  Serial.println("  Time / Bin (ms): " + String(_binDurationMicros / 1000.0));
	  Serial.println("  Total Time (ms): " + String(_binDurationMicros * _numBins / 1000.0) + " ms");
	  Serial.println("  Bin Counts:");
	  for (uint i = 0; i < _numBins; i++)
	  {
		 Serial.println("    Bin[" + String(i) + "]: " + String(_bins[i]));
	  }
	  Serial.println("    Transition Bin: " + String(_transitionBin));
   }
};

unsigned long (*TimedBin::microsFunc) () = nullptr;
