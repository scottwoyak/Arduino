#pragma once

#include <Arduino.h>

enum StopwatchPrecision
{
   Micros,
   Millis,
};

class Stopwatch {
private:
   StopwatchPrecision _precision;
   unsigned long _startTicks;
   unsigned long _elapsedTicks = 0;
   bool _running = false;

   unsigned long _currentTicks() const {
      if (_running) {
         return (_ticks() - _startTicks) + _elapsedTicks;
      }

      return _elapsedTicks;
   }

   unsigned long (*_ticks) ();

public:
   // public function for testing only. Allows us to substitute our own clock
   // within test cases of timing code.
   static unsigned long (*tickFunc) ();

   Stopwatch(bool start = true, StopwatchPrecision precision = StopwatchPrecision::Micros) {
      this->_precision = precision;

      if (Stopwatch::tickFunc != nullptr) {
         this->_ticks = Stopwatch::tickFunc;
      }
      else {
         if (this->_precision == StopwatchPrecision::Micros) {
            this->_ticks = micros;
         }
         else {
            this->_ticks = millis;
         }
      }

      if (start) {
         this->start();
      }
   }

   /***
    * Precision impacts the maximum length of time. If in micros, max is
    * about 4000 seconds
    */
   Stopwatch(StopwatchPrecision precision) : Stopwatch(true, precision) {
   }

   StopwatchPrecision getPrecision() const {
      return _precision;
   }

   void start() {
      if (this->_running == false) {
         _running = true;
         _startTicks = this->_ticks();
      }
   }

   void stop() {
      if (this->_running) {
         this->_elapsedTicks += (this->_ticks() - this->_startTicks);
         this->_running = false;
      }
   }

   void reset(double elapsedMillis = 0) {
      if (this->_precision == StopwatchPrecision::Micros) {
         this->_elapsedTicks = 1000 * elapsedMillis;
      }
      else {
         this->_elapsedTicks = elapsedMillis;
      }

      if (this->_running) {
         this->_startTicks = this->_ticks();
      }
   }

   bool isRunning() const {
      return _running;
   }

   unsigned long elapsedMicros() const {
      if (_precision == StopwatchPrecision::Micros) {
         return _currentTicks();
      }

      return 1000 * _currentTicks();
   }

   double elapsedMillis() const {
      if (_precision == StopwatchPrecision::Micros) {
         return _currentTicks() / 1000.0;
      }

      return _currentTicks();
   }

   double elapsedSecs() const {
      return elapsedMillis() / 1000.0;
   }

   void printlnMicros(const char* label, bool reset = false) {
      Serial.print(label);
      Serial.print(" ");
      Serial.print(this->elapsedMicros());
      Serial.println(" micros");

      if (reset) {
         this->reset();
      }
   }

   void printlnMillis(const char* label, bool reset = false) {
      Serial.print(label);
      Serial.print(" ");
      Serial.print(this->elapsedMillis());
      Serial.println("ms");

      if (reset) {
         this->reset();
      }
   }

   void printlnSecs(const char* label, bool reset = false) {
      Serial.print(label);
      Serial.print(" ");
      Serial.print(this->elapsedSecs());
      Serial.println("s");

      if (reset) {
         this->reset();
      }
   }
};

unsigned long (*Stopwatch::tickFunc) () = nullptr;
