#pragma once

#include <Arduino.h>
#include "Latch.h"
#include "Led.h"

///
/// <summary>
/// Measures wind speed from a reed-switch/hall-effect anemometer using interrupt-driven
/// pulse timing, with an optional LED that blinks once per full rotation.
/// </summary>
/// <remarks>
/// Only one WindMeter instance may be active at a time: the interrupt handler is routed
/// through a single static instance pointer, so constructing a second WindMeter will
/// silently redirect that pointer and break the first instance's interrupt handling.
/// </remarks>
///
class WindMeter
{
private:
   // Wind speed threshold below which the meter is considered stopped/idle rather than
   // rotating too slowly to measure reliably.
   static constexpr float MIN_DETECTABLE_MPH = 0.1f;

   // microsSinceLastTick threshold (in microseconds) corresponding to MIN_DETECTABLE_MPH,
   // derived from the anemometer's speed formula: 1 rotation/s = 1.7 ms/s, so
   // 0.1 mph = 1901400 micros between ticks and 100 mph = 1901 micros between ticks.
   static constexpr unsigned long IDLE_THRESHOLD_MICROS = 1901400;

   // Numerator for converting a tick period (in microseconds) to mph: speed = MPH_NUMERATOR / period.
   static constexpr float MPH_NUMERATOR = 190140.0f;

   // Number of ticks (half-rotations) per LED blink toggle.
   static constexpr uint8_t TICKS_PER_ROTATION = 20;

   static inline WindMeter* _instance;
   static void interruptTick()
   {
      // call the function on the class
      WindMeter::_instance->tick();
   }

   // Applies any pending rotation-indicator LED state change. Runs on a FreeRTOS timer
   // (not interrupt context) since analogWrite()/LEDC on ESP32 uses locks that are not
   // safe to call from an ISR.
   static void _timerCallback(TimerHandle_t xTimer)
   {
      WindMeter* meter = static_cast<WindMeter*>(pvTimerGetTimerID(xTimer));
      meter->_applyLedState();
   }

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

   void _applyLedState()
   {
      if (_ledStateChanged && _ledPin > 0)
      {
         _ledStateChanged = false;
         analogWrite(_ledPin, _ledState ? (uint8_t)(255 * _ledCalibrationFactor) : 0);
      }
   }

   uint8_t _pin;
   uint8_t _ledPin;
   float _ledCalibrationFactor;
   volatile Latch _latch;
   volatile uint8_t _ticks = 0;
   volatile bool _ledState = false;
   volatile bool _ledStateChanged = false;

   void tick()
   {
      // debounce
      if (_latch.settled() == false)
      {
         return;
      }

      if (_latch.setState(digitalRead(_pin)))
      {
         // if a state change occurred, track the ticks
         _ticks = _ticks + 1;

         // Record the desired led state here, but don't call analogWrite() from the ISR:
         // on ESP32, analogWrite()/LEDC uses locks that are not safe to call from
         // interrupt context and can crash. _applyLedState(), run from the timer
         // callback, applies the change instead.
         if (_ticks == 1)
         {
            _ledState = false;
            _ledStateChanged = true;
         }
         else if (_ticks >= TICKS_PER_ROTATION)
         {
            _ticks = 0;
            _ledState = true;
            _ledStateChanged = true;
         }
      }
   }

public:
   ///
   /// <summary>
   /// Constructs a WindMeter monitoring the specified pin, with an optional LED that
   /// blinks once per rotation.
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
      _instance = this;
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
      attachInterrupt(digitalPinToInterrupt(_pin), WindMeter::interruptTick, CHANGE);

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
      unsigned long period = _latch.getPeriod();
      unsigned long microsSinceLastTick = _latch.getMicrosSinceLastTick();
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
