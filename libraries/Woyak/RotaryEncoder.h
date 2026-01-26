#pragma once

#include "Arduino.h"
#include "Button.h"

//-------------------------------------------------------------------------------------------------
class RotaryEncoder
{
public:
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

private:
   uint8_t _pinA;
   uint8_t _pinB;
   Button _button;

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

         if (_initialDirection == direction)
         {
            // the direction must match the initial direction to complete the change
            _position = _position + (direction == Direction::UP ? 1 : -1);
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
   RotaryEncoder(uint8_t pinA, uint8_t pinB, uint8_t buttonPin) : _button(buttonPin)
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

      _button.begin();
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
      return _button.getPin();
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

   bool isPressed() const
   {
      return _button.isPressed();
   }

   bool wasPressed() const
   {
      return _button.wasPressed();
   }

   void reset()
   {
      _button.reset();
   }
};
