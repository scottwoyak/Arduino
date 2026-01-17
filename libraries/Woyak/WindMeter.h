#pragma once

#include <RunningAverager.h>
#include <MinMaxValue.h>
#include <Arduino.h>

class WindMeter {
private:
   static WindMeter* _instance;
   static void interruptTick() {
      // call the function on the class
      WindMeter::_instance->tick();
   }

   uint8_t _pin;
   uint8_t _ledPin;
   unsigned long _lastMicros;
   unsigned long _totalMicros;
   MinMaxValue _windSpeed;
   unsigned long _rotations = 0;

   void tick() {
      // update the led to match the pin
      int val = digitalRead(this->_pin);
      digitalWrite(this->_ledPin, val);

      // each time the pin goes high, record the wind speed associated
      // with that rotation
      if (val == HIGH) {
         long newTime = micros();

         if (this->_lastMicros == -1) {
            this->_lastMicros = newTime;
         }
         else {
            // get the elapsed micros, accounting for possible overflow
            unsigned long elapsedMicros;
            if (newTime < this->_lastMicros) {
               elapsedMicros = newTime + (ULONG_MAX - this->_lastMicros);
            }
            else {
               elapsedMicros = newTime - this->_lastMicros;
            }

            // debounce
            if (elapsedMicros < 15000) {
               // false signal
               return;
            }
            else {
               // compute the speed
               float speed = this->_computeWindSpeed(elapsedMicros, 1);
               this->_windSpeed.setValue(speed);

               // update timing values used for the average
               this->_rotations++;
               this->_totalMicros += elapsedMicros;
               this->_lastMicros = newTime;
            }
         }
      }
   }

   // the formula from the wind meter for turning rotation Hz into MPH
   float _computeWindSpeed(unsigned long micros, unsigned long rotations) {
      return (2.7 * 1000 * 1000.0 / micros) * rotations;
   }

public:
   WindMeter(uint8_t pin, uint8_t ledPin = LED_BUILTIN)
      : _windSpeed(new RunningAverager(3)) {
      this->_pin = pin;
      this->_ledPin = ledPin;
      this->_instance = this;
   }

   void begin() {
      // set up the led pin
      pinMode(this->_ledPin, OUTPUT);
      digitalWrite(this->_ledPin, LOW);

      // create the interrupt for monitoring the pin change
      pinMode(this->_pin, INPUT_PULLDOWN);
      unsigned short interrupt = digitalPinToInterrupt(this->_pin);
      attachInterrupt(interrupt, WindMeter::interruptTick, CHANGE);

      // provide an initial value of zero
      this->_windSpeed.setValue(0);
   }

   float getMin() {
      return this->_windSpeed.getMin();
   }
   float getMax() {
      return this->_windSpeed.getMax();
   }
   float getAvg() {
      return this->_computeWindSpeed(this->_totalMicros, this->_rotations);
   }
   float getCurrent() {
      if ((micros() - this->_lastMicros) > 10 * 1000 * 1000) {
         this->_windSpeed.setValue(0);
      }

      return this->_windSpeed.getValue();
   }
   void reset() {
      // turn off interrupts so values are not modified
      noInterrupts();

      this->_windSpeed.resetMinMax();
      this->_rotations = 0;
      this->_totalMicros = 0;

      interrupts();
   }
};
