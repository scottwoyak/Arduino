#ifndef WIND_METER
#define WIND_METER

#include "Value.h"
#include <Arduino.h>

class WindMeter {
private:
  static WindMeter *_instance;
  static void interruptTick() {
    // call the function on the class
    WindMeter::_instance->tick();
  }

  uint8_t _pin;
  uint8_t _ledPin;
  unsigned long _lastMicros;
  unsigned long _totalMicros;
  Value _windSpeed;
  unsigned long _rotations = 0;

  void tick() {
    // update the led to match the pin
    int val = digitalRead(this->_pin);
    digitalWrite(this->_ledPin, val);

    // each time the pin goes high, record the wind speed associated
    // with that rotation
    if (val == HIGH) {
      long newTime = micros();

      if (WindMeter::_lastMicros == -1) {
        WindMeter::_lastMicros = newTime;
        return;
      }

      // debounce
      if (newTime - WindMeter::_lastMicros < 1000) {
        return;
      }

      // compute the speed
      unsigned long elapsedMicros = newTime - this->_lastMicros;
      if (elapsedMicros > 1000) {
        double speed = this->_computeWindSpeed(elapsedMicros, 1);
        this->_windSpeed.setValue(speed);

        // update timing values used for the average
        this->_rotations++;
        this->_totalMicros += elapsedMicros;
        WindMeter::_lastMicros = newTime;
      }
      WindMeter::_lastMicros = newTime;
    }
  }

  // the formula from the wind meter for turning rotation Hz into MPH
  double _computeWindSpeed(unsigned long micros, unsigned long rotations) {
    return (2.7 * 1000000.0 / micros) * rotations;
  }

public:
  WindMeter(uint8_t pin, uint8_t ledPin = 13) {
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
  }

  double getMin() { return this->_windSpeed.getMin(); }
  double getMax() { return this->_windSpeed.getMax(); }
  double getAvg() {
    return this->_computeWindSpeed(this->_totalMicros, this->_rotations);
  }
  double getCurrent() {
    if ((micros() - this->_lastMicros) > 5 * 1000000) {
      return 0;
    } else {
      return this->_windSpeed.getValue();
    }
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

#endif