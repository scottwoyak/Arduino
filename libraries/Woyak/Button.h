#pragma once

#include <Arduino.h>

///
/// <summary>
/// Interrupt-driven button press detector with debouncing.
/// </summary>
/// <remarks>
/// Monitors button presses using hardware interrupts and debounces with a minimum
/// interval between accepted presses. Press events are tracked in a counter that can be
/// polled or auto-reset. Uses attachInterruptArg() so any number of Button instances can
/// be created, each routed through a single shared ISR. Buttons should be wired to pull
/// LOW on press (active-low), with internal pull-ups enabled.
/// </remarks>
///
class Button
{
private:
   uint8_t _pin;

   volatile uint16_t _pressedCount = 0;
   volatile unsigned long _lastPressMillis = 0;
   volatile bool _pressedState = false;

   ///
   /// <summary>
   /// Interrupt service routine for button state changes.
   /// </summary>
   ///
   void ARDUINO_ISR_ATTR _onChange()
   {
      unsigned long now = millis();
      bool isPressedNow = (digitalRead(_pin) == LOW);
      if (isPressedNow && !_pressedState)
      {
         if (now - _lastPressMillis >= minPressIntervalMs)
         {
            // don't use ++ for volatile vars
            _pressedCount = _pressedCount + 1;
            _lastPressMillis = now;
         }
      }

      _pressedState = isPressedNow;
   }

   static void ARDUINO_ISR_ATTR _onChangeHandler(void* arg) { static_cast<Button*>(arg)->_onChange(); }

public:
   /// <summary>
   /// If true, pressing wasPressed() resets the counter; otherwise manual reset required.
   /// </summary>
   bool autoReset = true;

   /// <summary>
   /// Minimum time between accepted button press events.
   /// </summary>
   uint16_t minPressIntervalMs = 500;

   ///
   /// <summary>
   /// Constructs a Button on the specified GPIO pin.
   /// </summary>
   /// <param name="pin">GPIO pin number for the button</param>
   ///
   Button(uint8_t pin)
   {
      _pin = pin;
   }

   ///
   /// <summary>
   /// Initializes the button with input pull-up and registers interrupt handler.
   /// </summary>
   /// <returns>true, always; retained for backward compatibility with callers that check the result</returns>
   /// <remarks>
   /// Must be called once during setup(). Automatically configures INPUT_PULLUP mode
   /// and attaches a CHANGE interrupt to detect press/release transitions.
   /// </remarks>
   ///
   bool begin()
   {
      pinMode(_pin, INPUT_PULLUP);
      _pressedState = (digitalRead(_pin) == LOW);

      attachInterruptArg(digitalPinToInterrupt(_pin), Button::_onChangeHandler, this, CHANGE);

      return true;
   }

   ///
   /// <summary>
   /// Gets the GPIO pin number for this button.
   /// </summary>
   /// <returns>GPIO pin number</returns>
   ///
   uint8_t getPin() const
   {
      return _pin;
   }

   ///
   /// <summary>
   /// Checks if the button is currently in the pressed state.
   /// </summary>
   /// <returns>true if button is pressed (pin is LOW); false otherwise</returns>
   ///
   bool isPressed() const
   {
      return digitalRead(_pin) == LOW;
   }

   ///
   /// <summary>
   /// Checks if a button press occurred since the last check.
   /// </summary>
   /// <returns>true if one or more presses were detected; false otherwise</returns>
   /// <remarks>
   /// If autoReset is true, the press counter is cleared after this call.
   /// If autoReset is false, use reset() to manually clear the counter.
   /// </remarks>
   ///
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

   ///
   /// <summary>
   /// Manually resets the press counter to zero.
   /// </summary>
   ///
   void reset()
   {
      _pressedCount = 0;
   }

   ///
   /// <summary>
   /// Gets the current press counter value.
   /// </summary>
   /// <returns>Number of debounced press events detected</returns>
   ///
   uint16_t getPressedCount() const
   {
      return _pressedCount;
   }
};
