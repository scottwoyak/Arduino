#pragma once

#include "Arduino.h"
#include "Latch.h"
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
   volatile Latch _latch;
   volatile uint8_t _ticks = 0;

   void tick()
   {
      // debounce
      if (_latch.settled() == false)
      {
         return;
      }

      if (_latch.setState(digitalRead(_pin)))
      {
         // if a state change occurred, track the ticks
         _ticks = _ticks + 1;

         // update the led to match the pin
         if (_ticks == 1)
         {
            digitalWrite(this->_ledPin, LOW);
         }
         else if (_ticks >= 20)
         {
            _ticks = 0;
            digitalWrite(this->_ledPin, HIGH);
         }
      }
   }

   // the formula from the wind meter for turning rotation Hz into MPH
   /*
   float _computeWindSpeed(unsigned long micros, unsigned long rotations)
   {
      return (2.7 * 1000 * 1000.0 / micros) * rotations;
   }
   */

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
      noInterrupts();
      unsigned long period = _latch.getPeriod();
      unsigned long microsSinceLastTick = _latch.getMicrosSinceLastTick();
      interrupts();

      // wind speed formula is 1 rotation/s = 1.7 ms/s
      // 0.1 mph = 1901400 micros
      // 100 mph = 1901 micros
      if (microsSinceLastTick > 1901400) // less than 0.1 mph
      {
         return 0.0;
      }
      else
      {
         return 190140.0 / period;
         /*
         double rotPerS = 1000000.0 / (20 * span);
         double mPerS = 1.7 * rotPerS;
         double mph = mPerS * 2.23694;
         return mph;
         */
      }
   }
};
