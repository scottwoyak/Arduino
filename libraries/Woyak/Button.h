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
public:
   bool autoReset = true;

private:
   uint8_t _pin;

   static uint8_t _index;
   static const uint8_t MAX_BUTTONS = 20;
   static Button* _buttons[MAX_BUTTONS];

   volatile uint16_t _pressedCount = 0;
   volatile unsigned long _millis = 0;

   void _onChange()
   {
      unsigned long now = millis();
      if (digitalRead(_pin) == LOW)
      {
         if (now - _millis > 50)
         {
            // don't use ++ for volatile vars
            _pressedCount = _pressedCount + 1;
         }
      }
      _millis = now;
   }

   static void _onChange0() { _buttons[0]->_onChange(); }
   static void _onChange1() { _buttons[1]->_onChange(); }
   static void _onChange2() { _buttons[2]->_onChange(); }
   static void _onChange3() { _buttons[3]->_onChange(); }
   static void _onChange4() { _buttons[4]->_onChange(); }
   static void _onChange5() { _buttons[5]->_onChange(); }
   static void _onChange6() { _buttons[6]->_onChange(); }
   static void _onChange7() { _buttons[7]->_onChange(); }
   static void _onChange8() { _buttons[8]->_onChange(); }
   static void _onChange9() { _buttons[9]->_onChange(); }
   static void _onChange10() { _buttons[10]->_onChange(); }
   static void _onChange11() { _buttons[11]->_onChange(); }
   static void _onChange12() { _buttons[12]->_onChange(); }
   static void _onChange13() { _buttons[13]->_onChange(); }
   static void _onChange14() { _buttons[14]->_onChange(); }
   static void _onChange15() { _buttons[15]->_onChange(); }
   static void _onChange16() { _buttons[16]->_onChange(); }
   static void _onChange17() { _buttons[17]->_onChange(); }
   static void _onChange18() { _buttons[18]->_onChange(); }
   static void _onChange19() { _buttons[19]->_onChange(); }

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
            attachInterrupt(digitalPinToInterrupt(_pin), Button::_onChange0, CHANGE);
            break;

         case 1:
            attachInterrupt(digitalPinToInterrupt(_pin), Button::_onChange1, CHANGE);
            break;

         case 2:
            attachInterrupt(digitalPinToInterrupt(_pin), Button::_onChange2, CHANGE);
            break;

         case 3:
            attachInterrupt(digitalPinToInterrupt(_pin), Button::_onChange3, CHANGE);
            break;

         case 4:
            attachInterrupt(digitalPinToInterrupt(_pin), Button::_onChange4, CHANGE);
            break;

         case 5:
            attachInterrupt(digitalPinToInterrupt(_pin), Button::_onChange5, CHANGE);
            break;

         case 6:
            attachInterrupt(digitalPinToInterrupt(_pin), Button::_onChange6, CHANGE);
            break;

         case 7:
            attachInterrupt(digitalPinToInterrupt(_pin), Button::_onChange7, CHANGE);
            break;

         case 8:
            attachInterrupt(digitalPinToInterrupt(_pin), Button::_onChange8, CHANGE);
            break;

         case 9:
            attachInterrupt(digitalPinToInterrupt(_pin), Button::_onChange9, CHANGE);
            break;
         case 10:
            attachInterrupt(digitalPinToInterrupt(_pin), Button::_onChange10, CHANGE);
            break;
         case 11:
            attachInterrupt(digitalPinToInterrupt(_pin), Button::_onChange11, CHANGE);
            break;
         case 12:
            attachInterrupt(digitalPinToInterrupt(_pin), Button::_onChange12, CHANGE);
            break;
         case 13:
            attachInterrupt(digitalPinToInterrupt(_pin), Button::_onChange13, CHANGE);
            break;
         case 14:
            attachInterrupt(digitalPinToInterrupt(_pin), Button::_onChange14, CHANGE);
            break;
         case 15:
            attachInterrupt(digitalPinToInterrupt(_pin), Button::_onChange15, CHANGE);
            break;
         case 16:
            attachInterrupt(digitalPinToInterrupt(_pin), Button::_onChange16, CHANGE);
            break;
         case 17:
            attachInterrupt(digitalPinToInterrupt(_pin), Button::_onChange17, CHANGE);
            break;
         case 18:
            attachInterrupt(digitalPinToInterrupt(_pin), Button::_onChange18, CHANGE);
            break;
         case 19:
            attachInterrupt(digitalPinToInterrupt(_pin), Button::_onChange19, CHANGE);
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

   bool isPressed() const
   {
      return digitalRead(_pin) == LOW;
   }

   bool wasPressed()
   {
      noInterrupts();
      bool pressed = _pressedCount > 0;
      if (autoReset)
      {
         _pressedCount = 0;
      }
      interrupts();

      return pressed;
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
