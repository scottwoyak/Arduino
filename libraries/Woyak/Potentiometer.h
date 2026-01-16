//-------------------------------------------------------------------------------------------------
//
// Smooths out response from a potentiometer when a number of steps is provided. When this
// happens, the potentiometer only records a value when the change has met a threshold
//
//-------------------------------------------------------------------------------------------------
class Potentiometer
{
   int _pin;
   int _lastRead = -1;
   int _numValues;

public:
   Potentiometer(int pin, int numValues = 1024)
   {
      _pin = pin;
      _numValues = constrain(numValues, 2, 1024);

      pinMode(_pin, INPUT);
   }

   float read()
   {
      int newRead = analogRead(_pin);

      if (_lastRead != -1)
      {
         if (newRead != 0 && newRead != 1023)
         {
            float stepSize = 1023.0 / (_numValues - 1);
            if (abs(newRead - _lastRead) < stepSize)
            {
               return _lastRead;
            }
         }
      }

      // jump to nearest value
      float nearestValue = round((_numValues - 1) * (newRead / 1023.0));
      _lastRead = 1023 * (nearestValue / (_numValues - 1));

      return _lastRead;
   }
};

class ScaledPotentiometer
{
   Potentiometer _p;
   int _min = 0;
   int _max = 100;

public:
   ScaledPotentiometer(int pin, float min, float max) : _p(pin, (max - min))
   {
      _min = min;
      _max = max;
   }

   int read()
   {
      return _min + (_p.read() / 1023.0) * (_max - _min);
   }
};
