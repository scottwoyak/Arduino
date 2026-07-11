#pragma once

#include <Arduino.h>
#include "Button.h"
#include <limits>

///
/// <summary>
/// Rotary encoder with integral pushbutton.
/// </summary>
/// <remarks>
/// Detects clockwise/counter-clockwise rotation using quadrature encoding and maintains
/// a position counter. Includes a built-in button for user interaction. Supports optional
/// min/max position limits. Uses interrupts for reliable detection of fast rotations.
/// </remarks>
///
class RotaryEncoder
{
public:
   ///
   /// <summary>
   /// Integral button on the rotary encoder shaft.
   /// </summary>
   ///
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
   int32_t _min = std::numeric_limits<int32_t>::min();
   int32_t _max = std::numeric_limits<int32_t>::max();

   volatile bool _isLowA = false;
   volatile bool _isLowB = false;
   volatile Direction _initialDirection = Direction::UNKNOWN;
   volatile int32_t _position = 0;

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

   static void onChangeA(void* arg) { static_cast<RotaryEncoder*>(arg)->_onChangeA(); }
   static void onChangeB(void* arg) { static_cast<RotaryEncoder*>(arg)->_onChangeB(); }

public:
   ///
   /// <summary>
   /// Constructs a RotaryEncoder with the specified pins.
   /// </summary>
   /// <param name="pinA">GPIO pin for phase A</param>
   /// <param name="pinB">GPIO pin for phase B</param>
   /// <param name="buttonPin">GPIO pin for the pushbutton</param>
   ///
   RotaryEncoder(uint8_t pinA, uint8_t pinB, uint8_t buttonPin) : button(buttonPin)
   {
      _pinA = pinA;
      _pinB = pinB;
   }

   ///
   /// <summary>
   /// Initializes encoder pins and interrupt handlers.
   /// </summary>
   /// <remarks>
   /// Configures phase A and B pins with pull-ups and CHANGE interrupts.
   /// Also initializes the integral button. Call once during setup().
   /// </remarks>
   ///
   void begin()
   {
      pinMode(_pinA, INPUT_PULLUP);
      pinMode(_pinB, INPUT_PULLUP);
      attachInterruptArg(digitalPinToInterrupt(_pinA), RotaryEncoder::onChangeA, this, CHANGE);
      attachInterruptArg(digitalPinToInterrupt(_pinB), RotaryEncoder::onChangeB, this, CHANGE);

      // should be high because we pulled them up
      _isLowA = digitalRead(_pinA) == LOW;
      _isLowB = digitalRead(_pinB) == LOW;

      button.begin();
   }

   ///
   /// <summary>
   /// Gets the phase A pin number.
   /// </summary>
   /// <returns>GPIO pin for phase A</returns>
   ///
   uint8_t getPinA() const
   {
      return _pinA;
   }

   ///
   /// <summary>
   /// Gets the phase B pin number.
   /// </summary>
   /// <returns>GPIO pin for phase B</returns>
   ///
   uint8_t getPinB() const
   {
      return _pinB;
   }

   ///
   /// <summary>
   /// Gets the button pin number.
   /// </summary>
   /// <returns>GPIO pin for the pushbutton</returns>
   ///
   uint8_t getButtonPin() const
   {
      return button.getPin();
   }

   ///
   /// <summary>
   /// Gets the current state of phase A pin.
   /// </summary>
   /// <returns>True if phase A pin is low</returns>
   ///
   bool isLowA() const
   {
      return _isLowA;
   }

   ///
   /// <summary>
   /// Gets the current state of phase B pin.
   /// </summary>
   /// <returns>True if phase B pin is low</returns>
   ///
   bool isLowB() const
   {
      return _isLowB;
   }

   ///
   /// <summary>
   /// Gets the current rotation position.
   /// </summary>
   /// <returns>Position counter (incremented on clockwise, decremented on counter-clockwise)</returns>
   ///
   int32_t getPosition() const
   {
      return _position;
   }

   ///
   /// <summary>
   /// Sets the rotation position.
   /// </summary>
   /// <param name="position">New position value</param>
   ///
   void setPosition(int32_t position)
   {
      _position = position;
   }

   ///
   /// <summary>
   /// Sets the min/max position limits.
   /// </summary>
   /// <param name="min">Minimum allowed position</param>
   /// <param name="max">Maximum allowed position</param>
   /// <remarks>
   /// Position will be clamped to this range during rotation.
   /// </remarks>
   ///
   void setLimits(int32_t min, int32_t max)
   {
      _min = min;
      _max = max;
   }

   ///
   /// <summary>
   /// Gets the minimum position limit.
   /// </summary>
   /// <returns>Minimum allowed position</returns>
   ///
   int32_t getMin() const
   {
      return _min;
   }

   ///
   /// <summary>
   /// Gets the maximum position limit.
   /// </summary>
   /// <returns>Maximum allowed position</returns>
   ///
   int32_t getMax() const
   {
      return _max;
   }
};
