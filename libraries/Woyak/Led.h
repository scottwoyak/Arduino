#pragma once

#include "Arduino.h"
#include "Latch.h"
#include "Util.h"

class Led
{
private:
   uint8_t _pin;
   uint8_t _blinkIntervalMs = 0;
   uint8_t _level = 255;
   bool _isOn = false;

public:
   Led(uint8_t pin)
   {
      this->_pin = pin;
   }

   void begin()
   {
      pinMode(this->_pin, OUTPUT);
      digitalWrite(this->_pin, LOW);
   }

   bool isOn()
   {
      return _isOn;
   }

   void turnOn()
   {
      analogWrite(this->_pin, _level);
      _isOn = _level > 0;
   }

   void turnOff()
   {
      analogWrite(this->_pin, 0);
      _isOn = false;
   }

   void setBlinkInterval(uint16_t blinkIntervalMs)
   {
      this->_blinkIntervalMs = blinkIntervalMs;
   }

   void setLevel(uint8_t level)
   {
      _level = constrain(level, 0, 255);
      analogWrite(this->_pin, _level);
   }


   void loop()
   {
      if (_blinkIntervalMs > 0)
      {
         if (millis() % (2 * _blinkIntervalMs) < _blinkIntervalMs)
         {
            turnOn();
         }
         else
         {
            turnOff();
         }
      }
   }
};
