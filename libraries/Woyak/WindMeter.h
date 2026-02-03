#pragma once

#include "Arduino.h"
#include "Util.h"

class WindMeter
{
private:
   static WindMeter* _instance;
   static void interruptTick()
   {
      // call the function on the class
      WindMeter::_instance->tick();
   }

   uint8_t _pin;
   uint8_t _ledPin;
   volatile unsigned long _micros = 0;
   volatile unsigned long _lastMicros = 0;

   void tick()
   {
      if (micros() - this->_micros < 1000)
      {
         return;
      }

      // update the led to match the pin
      int val = digitalRead(this->_pin);
      digitalWrite(this->_ledPin, val);

      if (val == HIGH)
      {
         _lastMicros = _micros;
         _micros = micros();
      }
   }

   // the formula from the wind meter for turning rotation Hz into MPH
   float _computeWindSpeed(unsigned long micros, unsigned long rotations)
   {
      return (2.7 * 1000 * 1000.0 / micros) * rotations;
   }

public:
   WindMeter(uint8_t pin, uint8_t ledPin = LED_BUILTIN)
   {
      this->_pin = pin;
      this->_ledPin = ledPin;
      this->_instance = this;
   }

   void begin()
   {
      // set up the led pin
      pinMode(this->_ledPin, OUTPUT);
      digitalWrite(this->_ledPin, LOW);

      // create the interrupt for monitoring the pin change
      pinMode(this->_pin, INPUT_PULLUP);
      attachInterrupt(digitalPinToInterrupt(this->_pin), WindMeter::interruptTick, CHANGE);
   }

   float getSpeed()
   {
      unsigned long span;
      unsigned long lastMicros;

      noInterrupts();
      span = Util::getSpan(this->_lastMicros, this->_micros);
      lastMicros = this->_lastMicros;
      interrupts();

      // wind speed formula is 1 rotation/s = 1.7 ms/s
      if (micros() - lastMicros > 1901400) // less than 0.1 mph
      {
         return 0.0;
      }
      else
      {
         return 190140.0 / span;
         /*
         double rotPerS = 1000000.0 / (20 * span);
         double mPerS = 1.7 * rotPerS;
         double mph = mPerS * 2.23694;
         return mph;
         */
      }
   }

   unsigned long getLastMicros()
   {
      unsigned long lastMicros;
      noInterrupts();
      lastMicros = this->_lastMicros;
      interrupts();
      return lastMicros;
   }
};
