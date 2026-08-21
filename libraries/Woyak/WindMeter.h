#pragma once

#include <Arduino.h>
#include "Led.h"
#include "Util.h"

///
/// <summary>
/// Measures wind speed from a reed-switch/hall-effect anemometer using interrupt-driven
/// pulse timing, with an optional LED that flashes on for LED_FLASH_MS once per full rotation.
/// </summary>
/// <remarks>
/// Uses attachInterruptArg() so any number of WindMeter instances can be created, each
/// routed through a single shared ISR.
/// </remarks>
///
class WindMeter
{
private:
   // microsSinceLastTick threshold (in microseconds) below which the meter is considered
   // rotating rather than stopped/idle, derived from the anemometer's speed formula:
   // 1 rotation/s = 1.7 ms/s, so 0.1 mph = 1901400 micros between ticks and 100 mph =
   // 1901 micros between ticks.
   static constexpr unsigned long IDLE_THRESHOLD_MICROS = 1901400;

   // Numerator for converting a tick period (in microseconds) to mph: speed = MPH_NUMERATOR / period.
   static constexpr float MPH_NUMERATOR = 190140.0f;

   // Number of ticks per full rotation. The LED flashes on once per rotation, when the
   // tick count reaches TICKS_PER_ROTATION.
   static constexpr uint8_t TICKS_PER_ROTATION = 20;

   // Duration the rotation-indicator LED stays on for each flash.
   static constexpr uint16_t LED_FLASH_MS = 50;

   // Debounce time constant for latched pin state changes.
   static constexpr unsigned long DEBOUNCE_TIME_MICROS = 100;

   uint8_t _pin;
   uint8_t _ledPin;
   float _ledCalibrationFactor;
   volatile unsigned long _lastHighMicros = 0;
   volatile unsigned long _period = 0;
   volatile unsigned long _microsAtStateChange = 0;
   volatile int _latchState = LOW;
   volatile uint8_t _ticks = 0;
   volatile bool _ledState = false;
   volatile bool _ledStateChanged = false;
   volatile unsigned long _ledOffTime = 0;

   ///
   /// <summary>
   /// ISR trampoline that forwards the pin-change interrupt to the owning instance's tick().
   /// </summary>
   /// <param name="arg">The WindMeter instance that owns this interrupt.</param>
   ///
   static void ARDUINO_ISR_ATTR _interruptTickHandler(void* arg)
   {
      static_cast<WindMeter*>(arg)->_tick();
   }

   ///
   /// <summary>
   /// FreeRTOS timer callback that applies any pending rotation-indicator LED state
   /// change. Runs on a FreeRTOS timer (not interrupt context) since analogWrite()/LEDC
   /// on ESP32 uses locks that are not safe to call from an ISR.
   /// </summary>
   /// <param name="xTimer">The FreeRTOS timer handle whose user data is the owning WindMeter instance.</param>
   ///
   static void _timerCallback(TimerHandle_t xTimer)
   {
      WindMeter* meter = static_cast<WindMeter*>(pvTimerGetTimerID(xTimer));
      meter->_applyLedState();
   }

   ///
   /// <summary>
   /// Creates and starts the FreeRTOS timer used to apply rotation-indicator LED
   /// changes outside of interrupt context.
   /// </summary>
   ///
   void _startTimer()
   {
      TimerHandle_t timerHandle = xTimerCreate(
         "WindLedTimer",   // only used for debugging
         pdMS_TO_TICKS(1), // tick interval in ms
         pdTRUE,           // auto-reload
         this,             // user data
         _timerCallback);  // callback function

      if (timerHandle != nullptr)
      {
         xTimerStart(timerHandle, 0); // Start the timer
      }
   }

   ///
   /// <summary>
   /// Applies any pending rotation-indicator LED on/off change and turns the LED back
   /// off once its flash duration has elapsed.
   /// </summary>
   ///
   void _applyLedState()
   {
      if (_ledStateChanged && _ledPin > 0)
      {
         _ledStateChanged = false;
         analogWrite(_ledPin, _ledState ? (uint8_t)(255 * _ledCalibrationFactor) : 0);
      }

      if (_ledState && _ledPin > 0 && millis() >= _ledOffTime)
      {
         _ledState = false;
         analogWrite(_ledPin, 0);
      }
   }

   ///
   /// <summary>
   /// Attempts to change the latched pin state with debouncing.
   /// </summary>
   /// <param name="state">The new state value (HIGH or LOW)</param>
   /// <returns>true if state changed (after debounce); false if ignored due to debounce or no change</returns>
   /// <remarks>
   /// If transitioning to HIGH, automatically records the period since the previous HIGH state.
   /// </remarks>
   ///
   bool ARDUINO_ISR_ATTR _setLatchState(int state)
   {
      unsigned long newMicros = micros();

      // debounce
      if (Util::getSpan(_microsAtStateChange, newMicros) < DEBOUNCE_TIME_MICROS)
      {
         return false;
      }

      if (state == _latchState)
      {
         return false;
      }

      _latchState = state;
      _microsAtStateChange = newMicros;

      // keep track of the period
      if (state == HIGH)
      {
         _period = Util::getSpan(_lastHighMicros, newMicros);
         _lastHighMicros = newMicros;
      }

      return true;
   }

   ///
   /// <summary>
   /// Interrupt service routine for anemometer pin state changes. Debounces the pin,
   /// tracks the tick count, and schedules the rotation-indicator LED flash once per
   /// full rotation.
   /// </summary>
   ///
   void ARDUINO_ISR_ATTR _tick()
   {
      if (_setLatchState(digitalRead(_pin)))
      {
         // if a state change occurred, track the ticks
         _ticks = _ticks + 1;

         // Record the desired led state here, but don't call analogWrite() from the ISR:
         // on ESP32, analogWrite()/LEDC uses locks that are not safe to call from
         // interrupt context and can crash. _applyLedState(), run from the timer
         // callback, applies the change instead.
         if (_ticks >= TICKS_PER_ROTATION)
         {
            _ticks = 0;
            _ledState = true;
            _ledStateChanged = true;
            _ledOffTime = millis() + LED_FLASH_MS;
         }
      }
   }

public:
   ///
   /// <summary>
   /// Constructs a WindMeter monitoring the specified pin, with an optional LED that
   /// flashes once per rotation.
   /// </summary>
   /// <param name="sensorPin">GPIO pin connected to the anemometer's switch.</param>
   /// <param name="ledPin">Optional GPIO pin for a rotation-indicator LED; defaults to LED_BUILTIN.</param>
   /// <param name="ledColor">LED color/lens tint used to seed the rotation-indicator LED's calibration factor (see ledColorCalibrationFactor()); defaults to LEDColor::UNKNOWN (no scaling).</param>
   ///
   WindMeter(uint8_t sensorPin, uint8_t ledPin = LED_BUILTIN, LEDColor ledColor = LEDColor::UNKNOWN)
   {
      _pin = sensorPin;
      _ledPin = ledPin;
      _ledCalibrationFactor = ledColorCalibrationFactor(ledColor);
   }

   ///
   /// <summary>
   /// Initializes the anemometer pin and attaches the change interrupt.
   /// </summary>
   ///
   void begin()
   {
      // set up the led pin
      if (_ledPin > 0)
      {
         pinMode(_ledPin, OUTPUT);
         digitalWrite(_ledPin, LOW);
      }

      // create the interrupt for monitoring the pin change
      pinMode(_pin, INPUT_PULLUP);
      attachInterruptArg(digitalPinToInterrupt(_pin), WindMeter::_interruptTickHandler, this, CHANGE);

      // start the timer that applies rotation-indicator LED changes outside of
      // interrupt context
      _startTimer();
   }

   ///
   /// <summary>
   /// Gets the current wind speed in miles per hour, computed from the time between
   /// the two most recent pulses.
   /// </summary>
   /// <returns>Wind speed in mph, or 0.0 if the anemometer has been idle too long.</returns>
   ///
   float getSpeed()
   {
      noInterrupts();
      unsigned long period = _period;
      unsigned long microsSinceLastTick = Util::getSpan(_lastHighMicros, micros());
      interrupts();

      if (period == 0 || microsSinceLastTick > IDLE_THRESHOLD_MICROS)
      {
         // no ticks yet, or idle long enough to be considered stopped
         return 0.0f;
      }
      else
      {
         return MPH_NUMERATOR / period;
      }
   }

   ///
   /// <summary>
   /// Gets the current state of the rotation-indicator LED.
   /// </summary>
   /// <returns>True if the LED is currently on; false otherwise.</returns>
   ///
   bool isLedOn()
   {
      return _ledState;
   }
};
