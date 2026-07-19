#pragma once

#include <Arduino.h>
#include "IEncoder.h"
#include "Button.h"
#include <limits>

///
/// <summary>
/// GPIO quadrature encoder implementation with an integral pushbutton.
/// </summary>
/// <remarks>
/// Detects clockwise/counter-clockwise rotation using quadrature encoding and maintains
/// a position counter. Includes a built-in button for user interaction. Supports optional
/// min/max position limits. Uses interrupts for reliable detection of fast rotations.
/// </remarks>
///
class QuadratureEncoder : public IEncoder
{
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

   Button _button;

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

   static void onChangeA(void* arg) { static_cast<QuadratureEncoder*>(arg)->_onChangeA(); }
   static void onChangeB(void* arg) { static_cast<QuadratureEncoder*>(arg)->_onChangeB(); }

   ///
   /// <summary>
   /// Reads the current phase A/B pin states into _isLowA/_isLowB. Shared by begin()
   /// and reset() so newly-read pin states stay in sync after (re)initialization.
   /// </summary>
   ///
   void _refreshPinStates()
   {
      _isLowA = digitalRead(_pinA) == LOW;
      _isLowB = digitalRead(_pinB) == LOW;
   }

public:
   ///
   /// <summary>
   /// Constructs a QuadratureEncoder with the specified pins.
   /// </summary>
   /// <param name="pinA">GPIO pin for phase A</param>
   /// <param name="pinB">GPIO pin for phase B</param>
   /// <param name="buttonPin">GPIO pin for the pushbutton</param>
   ///
   QuadratureEncoder(uint8_t pinA, uint8_t pinB, uint8_t buttonPin) : _button(buttonPin)
   {
      _pinA = pinA;
      _pinB = pinB;
   }

   ///
   /// <summary>
   /// Gets the phase A pin number.
   /// </summary>
   ///
   uint8_t getPinA() const
   {
      return _pinA;
   }

   ///
   /// <summary>
   /// Gets the phase B pin number.
   /// </summary>
   ///
   uint8_t getPinB() const
   {
      return _pinB;
   }

   ///
   /// <summary>
   /// Gets the button pin number.
   /// </summary>
   ///
   uint8_t getButtonPin() const
   {
      return _button.getPin();
   }

   ///
   /// <summary>
   /// Gets the current state of phase A pin.
   /// </summary>
   ///
   bool isLowA() const
   {
      return _isLowA;
   }

   ///
   /// <summary>
   /// Gets the current state of phase B pin.
   /// </summary>
   ///
   bool isLowB() const
   {
      return _isLowB;
   }

   void begin() override
   {
      pinMode(_pinA, INPUT_PULLUP);
      pinMode(_pinB, INPUT_PULLUP);
      attachInterruptArg(digitalPinToInterrupt(_pinA), QuadratureEncoder::onChangeA, this, CHANGE);
      attachInterruptArg(digitalPinToInterrupt(_pinB), QuadratureEncoder::onChangeB, this, CHANGE);

      // should be high because we pulled them up
      _refreshPinStates();

      _button.begin();
   }

   int32_t getPosition() const override
   {
      return _position;
   }

   void setPosition(int32_t position) override
   {
      _position = position;
   }

   ///
   /// <summary>
   /// Resets the current position to zero and clears any in-progress quadrature
   /// transition state. Without clearing _initialDirection, a stray transition
   /// detected while pins were settling (e.g. during begin()) could leave the
   /// encoder mid-detent, causing the next real turn to silently complete/cancel
   /// that stale transition instead of registering as a fresh position change.
   /// </summary>
   ///
   void reset() override
   {
      _position = 0;
      _refreshPinStates();
      _initialDirection = Direction::UNKNOWN;
      IEncoder::reset();
   }

   void setLimits(int32_t min, int32_t max) override
   {
      _min = min;
      _max = max;
   }

   int32_t getMin() const override
   {
      return _min;
   }

   int32_t getMax() const override
   {
      return _max;
   }

   bool isButtonPressed() const override
   {
      return _button.isPressed();
   }

   bool buttonWasPressed() override
   {
      return _button.wasPressed();
   }

   void resetButton() override
   {
      _button.reset();
   }
};
