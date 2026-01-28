#pragma once

#include "Arduino.h"
#include "Button.h"

//-------------------------------------------------------------------------------------------------
class RotaryEncoder
{
public:
   Button button;

private:
   enum class Direction
   {
      UNKNOWN,
      UP,
      DOWN,
   };

   enum class Source
   {
      A,
      B,
   };

   uint8_t _pinA;
   uint8_t _pinB;
   long _min = std::numeric_limits<long>::min();
   long _max = std::numeric_limits<long>::max();

   static RotaryEncoder* _instance;

   volatile bool _isLowA = false;
   volatile bool _isLowB = false;
   volatile Direction _initialDirection = Direction::UNKNOWN;
   volatile long _position = 0;

   void _onChange(Source source)
   {
      if (_initialDirection == Direction::UNKNOWN)
      {
         // as soon as a pin goes low, rotation of the encoder has started. Record
         // the direction so that we can check to see if the final state completes
         // the change or goes back to the initial state.
         if (!_isLowA && _isLowB)
         {
            // turning right
            _initialDirection = Direction::UP;
         }
         else if (_isLowA && !_isLowB)
         {
            // turing left
            _initialDirection = Direction::DOWN;
         }
      }

      // when both are high, we're back to a nuetral position. We need to check if 
      // a change ocurred, or if we just returned to the intial position
      if (!_isLowA && !_isLowB)
      {
         // the last to go high tells us the direction.
         Direction direction = source == Source::A ? Direction::UP : Direction::DOWN;

         // the direction must match the initial direction to complete the change
         if (_initialDirection == direction)
         {
            // keep the value within the limits
            if (direction == Direction::UP && _position < _max)
            {
               _position = _position + 1;
            }
            else if (direction == Direction::DOWN && _position > _min)
            {
               _position = _position - 1;
            }
         }

         // reset states
         _initialDirection = Direction::UNKNOWN;
      }
   }

   void _onChangeA()
   {
      _isLowA = digitalRead(_pinA) == LOW;
      _onChange(Source::A);
   }

   void _onChangeB()
   {
      _isLowB = digitalRead(_pinB) == LOW;
      _onChange(Source::B);
   }

   static void onChangeA() { _instance->_onChangeA(); }
   static void onChangeB() { _instance->_onChangeB(); }

public:
   RotaryEncoder(uint8_t pinA, uint8_t pinB, uint8_t buttonPin) : button(buttonPin)
   {
      _pinA = pinA;
      _pinB = pinB;
      _instance = this;
   }

   void begin()
   {
      pinMode(_pinA, INPUT_PULLUP);
      pinMode(_pinB, INPUT_PULLUP);
      attachInterrupt(digitalPinToInterrupt(_pinA), RotaryEncoder::onChangeA, CHANGE);
      attachInterrupt(digitalPinToInterrupt(_pinB), RotaryEncoder::onChangeB, CHANGE);

      // should be high because we pulled them up
      _isLowA = digitalRead(_pinA) == LOW;
      _isLowB = digitalRead(_pinB) == LOW;

      button.begin();
   }

   uint8_t getPinA() const
   {
      return _pinA;
   }

   uint8_t getPinB() const
   {
      return _pinB;
   }

   uint8_t getButtonPin() const
   {
      return button.getPin();
   }

   bool isLowA() const
   {
      return _isLowA;
   }

   bool isLowB() const
   {
      return _isLowB;
   }

   long getPosition() const
   {
      return _position;
   }

   void setPosition(long position)
   {
      _position = position;
   }

   void setLimits(long min, long max)
   {
      _min = min;
      _max = max;
   }

   long getMin() const
   {
      return _min;
   }

   long getMax() const
   {
      return _max;
   }
};
