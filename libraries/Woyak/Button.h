#pragma once

#include "Arduino.h"

//-------------------------------------------------------------------------------------------------
//
// Easy to use class for button presses with interrupts and debouncing. 
// Set the pin to LOW to trigger.
//
//-------------------------------------------------------------------------------------------------
class Button
{
   uint8_t _pin;

   static uint8_t _index;
   static const uint8_t MAX_BUTTONS = 10;
   static Button* _buttons[MAX_BUTTONS];

   volatile uint16_t _pressedCount = 0;
   volatile unsigned long _millis = 0;

   void _onRising()
   {
      _millis = millis();
   }

   void _onFalling()
   {
      unsigned long now = millis();
      if (now - _millis > 50)
      {
         // don't use ++ for volatile vars
         _pressedCount = _pressedCount + 1;
      }
      _millis = now;
   }

   static void _onRising0() { _buttons[0]->_onRising(); }
   static void _onFalling0() { _buttons[0]->_onFalling(); }
   static void _onRising1() { _buttons[1]->_onRising(); }
   static void _onFalling1() { _buttons[1]->_onFalling(); }
   static void _onRising2() { _buttons[2]->_onRising(); }
   static void _onFalling2() { _buttons[2]->_onFalling(); }
   static void _onRising3() { _buttons[3]->_onRising(); }
   static void _onFalling3() { _buttons[3]->_onFalling(); }
   static void _onRising4() { _buttons[4]->_onRising(); }
   static void _onFalling4() { _buttons[4]->_onFalling(); }
   static void _onRising5() { _buttons[5]->_onRising(); }
   static void _onFalling5() { _buttons[5]->_onFalling(); }
   static void _onRising6() { _buttons[6]->_onRising(); }
   static void _onFalling6() { _buttons[6]->_onFalling(); }
   static void _onRising7() { _buttons[7]->_onRising(); }
   static void _onFalling7() { _buttons[7]->_onFalling(); }
   static void _onRising8() { _buttons[8]->_onRising(); }
   static void _onFalling8() { _buttons[8]->_onFalling(); }
   static void _onRising9() { _buttons[9]->_onRising(); }
   static void _onFalling9() { _buttons[9]->_onFalling(); }

public:
   Button(uint8_t pin)
   {
      _pin = pin;
   }

   bool begin()
   {
      if (_index < MAX_BUTTONS)
      {
         pinMode(_pin, INPUT_PULLUP);

         switch (_index)
         {
         case 0:
            attachInterrupt(digitalPinToInterrupt(_pin), Button::_onRising0, RISING);
            attachInterrupt(digitalPinToInterrupt(_pin), Button::_onFalling0, FALLING);
            break;

         case 1:
            attachInterrupt(digitalPinToInterrupt(_pin), Button::_onRising1, RISING);
            attachInterrupt(digitalPinToInterrupt(_pin), Button::_onFalling1, FALLING);
            break;

         case 2:
            attachInterrupt(digitalPinToInterrupt(_pin), Button::_onRising2, RISING);
            attachInterrupt(digitalPinToInterrupt(_pin), Button::_onFalling2, FALLING);
            break;

         case 3:
            attachInterrupt(digitalPinToInterrupt(_pin), Button::_onRising3, RISING);
            attachInterrupt(digitalPinToInterrupt(_pin), Button::_onFalling3, FALLING);
            break;

         case 4:
            attachInterrupt(digitalPinToInterrupt(_pin), Button::_onRising4, RISING);
            attachInterrupt(digitalPinToInterrupt(_pin), Button::_onFalling4, FALLING);
            break;

         case 5:
            attachInterrupt(digitalPinToInterrupt(_pin), Button::_onRising5, RISING);
            attachInterrupt(digitalPinToInterrupt(_pin), Button::_onFalling5, FALLING);
            break;

         case 6:
            attachInterrupt(digitalPinToInterrupt(_pin), Button::_onRising6, RISING);
            attachInterrupt(digitalPinToInterrupt(_pin), Button::_onFalling6, FALLING);
            break;

         case 7:
            attachInterrupt(digitalPinToInterrupt(_pin), Button::_onRising7, RISING);
            attachInterrupt(digitalPinToInterrupt(_pin), Button::_onFalling7, FALLING);
            break;

         case 8:
            attachInterrupt(digitalPinToInterrupt(_pin), Button::_onRising8, RISING);
            attachInterrupt(digitalPinToInterrupt(_pin), Button::_onFalling8, FALLING);
            break;

         case 9:
            attachInterrupt(digitalPinToInterrupt(_pin), Button::_onRising9, RISING);
            attachInterrupt(digitalPinToInterrupt(_pin), Button::_onFalling9, FALLING);
            break;
         }
         _buttons[_index++] = this;
         return true;
      }
      else
      {
         return false;
      }

   }

   uint8_t getPin() const
   {
      return _pin;
   }

   bool isCurrentlyPressed() const
   {
      return digitalRead(_pin) == LOW;
   }

   bool wasPressed() const
   {
      return _pressedCount > 0;
   }

   void reset()
   {
      _pressedCount = 0;
   }

   uint16_t getPressedCount() const
   {
      return _pressedCount;
   }
};
