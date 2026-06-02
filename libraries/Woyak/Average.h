#pragma once

class Average
{
private:
   float _avg = NAN;
   uint16_t _count = 0;

public:
   Average() 
   {
   }

   void add(float value)
   {
	  if (isnan(value)) 
	  {
		 return;
	  }

	  if (_count == 0)
	  {
		 _avg = value;
	  }
	  else
	  {
		 _avg = (_avg * _count + value) / (_count + 1);
	  }

	  _count++;
   }

   void remove(float value)
   {
	  if (isnan(value))
	  {
		 return;
	  }

	  if (_count == 0)	
	  {
		 _avg = NAN;
	  }
	  else if (_count == 1)
	  {
		 _avg = NAN;
		 _count = 0;
	  }
	  else
	  {
		 _avg = (_avg * _count - value) / (_count - 1);
		 _count--;
	  }
   }

   float get()
   {
      return _avg;
   }

   uint16_t count()
   {
	  return _count;
   }
};