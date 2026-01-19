#pragma once

// define this constant in your code based on your processor
#ifndef MAX_ANALOG_VALUES
#if defined(ESP32)
#define MAX_ANALOG_VALUES 4096
#else
#define MAX_ANALOG_VALUES 1024
#endif
#endif

#include "Arduino.h"

//-------------------------------------------------------------------------------------------------
//
// Smooths out response from a potentiometer when a number of steps is provided. When this
// happens, the potentiometer only records a value when the change has met a threshold
//
//-------------------------------------------------------------------------------------------------
class Potentiometer
{
   uint16_t _lastRead = 0;
   uint16_t _numValues;
   uint8_t _pin;

public:
   Potentiometer(uint8_t pin, uint16_t numValues = MAX_ANALOG_VALUES)
   {
      _pin = pin;
      _numValues = constrain(numValues, 2, MAX_ANALOG_VALUES);

      pinMode(pin, INPUT);
   }

   uint8_t getPin()
   {
      return _pin;
   }

   float read()
   {
      uint16_t newRead = ::analogRead(_pin);
      float stepSize = (MAX_ANALOG_VALUES - 1) / (_numValues - 1.0);

      if (newRead >= stepSize / 2.0 && newRead <= MAX_ANALOG_VALUES - stepSize / 2.0)
      {
         float changeCriteria = std::fmin(stepSize, MAX_ANALOG_VALUES / 10.0);
         if (abs(newRead - _lastRead) < changeCriteria)
         {
            return _lastRead;
         }
      }

      // jump to nearest value
      float nearestValue = round((_numValues - 1) * (newRead / (MAX_ANALOG_VALUES - 1.0)));
      _lastRead = (MAX_ANALOG_VALUES - 1) * (nearestValue / (_numValues - 1));

      return _lastRead;
   }

   uint16_t analogRead()
   {
      return ::analogRead(_pin);
   }
};

class IntPotentiometer
{
   Potentiometer _p;
   int16_t _min = 0;
   int16_t _max = 100;

public:
   IntPotentiometer(uint8_t pin, int16_t min, int16_t max) : _p(pin, (max - min)+1)
   {
      _min = min;
      _max = max;
   }

   uint8_t getPin()
   {
      return _p.getPin();
   }

   int16_t read()
   {
      return round(_min + ((float)_p.read() / (MAX_ANALOG_VALUES - 1)) * (_max - _min));
   }

   uint16_t readUnscaled()
   {
      return _p.read();
   }

   uint16_t analogRead()
   {
      return _p.analogRead();
   }
};

class FloatPotentiometer
{
   Potentiometer _p;
   float _min = 0;
   float _max = 100;

public:
   FloatPotentiometer(int pin, float min, float max, int numValues) : _p(pin, numValues)
   {
      _min = min;
      _max = max;
   }

   int getPin()
   {
      return _p.getPin();
   }

   float read()
   {
      return _min + (_p.read() / (MAX_ANALOG_VALUES - 1)) * (_max - _min);
   }

   uint16_t readUnscaled()
   {
      return _p.read();
   }

   uint16_t analogRead()
   {
      return _p.analogRead();
   }
};
