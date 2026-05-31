#pragma once

//
// Class for waiting for some amount of time to elapse
//
class Timer
{
private:
   unsigned long _startMillis;
   unsigned long _durationMs;

public:
   Timer(unsigned long durationMs)
   {
	  _startMillis = millis();
	  _durationMs = durationMs;
   }

   bool ready()
   {
	  unsigned long now = millis();
	  unsigned long span = Util::getSpan(_startMillis, now);
	  if (span >= _durationMs)
	  {
		 _startMillis += (span / _durationMs) * _durationMs;
		 return true;
	  }
	  else
	  {
		 return false;
	  }
   }
};
