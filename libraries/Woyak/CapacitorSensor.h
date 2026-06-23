#pragma once

#include <esp_timer.h>
#include "RollingRate.h"

/// <summary>
/// Provides interrupt-driven capacitor charge-time measurements using timer callbacks
/// for discharge and timeout control.
/// </summary>
class CapacitorSensor
{
private:
   enum State
   {
      IDLE,
      DISCHARGING,
      CHARGING
   };

   static constexpr uint64_t CHARGE_TIMEOUT_US = 200000;

   uint8_t _chargePin;
   uint8_t _sensePin;
   uint16_t _dischargeDelayMicros;

   volatile State _state = State::IDLE;
   volatile uint64_t _chargeStartTimeMicros = 0;
   volatile uint64_t _chargeTimeMicros = 0;
   volatile bool _hasChanged = false;

   RollingRate _rawSensorRate;

   esp_timer_handle_t _dischargeTimer = nullptr;
   esp_timer_handle_t _timeoutTimer = nullptr;

   static inline CapacitorSensor* _instance;

   static void _onPinRising()
   {
      CapacitorSensor::_instance->_onCharged();
   }

   static void _onDischargeElapsed(void*)
   {
      CapacitorSensor::_instance->_startCharging();
   }

   static void _onChargeTimeout(void*)
   {
      CapacitorSensor::_instance->_handleChargeTimeout();
   }

   void _startDischarging()
   {
      detachInterrupt(digitalPinToInterrupt(_sensePin));

      digitalWrite(_chargePin, LOW);
      pinMode(_sensePin, OUTPUT);
      digitalWrite(_sensePin, LOW);

      _state = DISCHARGING;
      _chargeStartTimeMicros = esp_timer_get_time();

      esp_timer_stop(_dischargeTimer);
      esp_timer_start_once(_dischargeTimer, _dischargeDelayMicros);
   }

   void _startCharging()
   {
      if (_state != DISCHARGING)
      {
         return;
      }

      digitalWrite(_chargePin, HIGH);
      _chargeStartTimeMicros = esp_timer_get_time();
      _state = CHARGING;

      pinMode(_sensePin, INPUT);
      attachInterrupt(digitalPinToInterrupt(_sensePin), _onPinRising, RISING);

      esp_timer_stop(_timeoutTimer);
      esp_timer_start_once(_timeoutTimer, CHARGE_TIMEOUT_US);
   }

   void _handleChargeTimeout()
   {
      if (_state != CHARGING)
      {
         return;
      }

      detachInterrupt(digitalPinToInterrupt(_sensePin));
      _state = IDLE;
      _startDischarging();
   }

   void _onCharged()
   {
      if (_state != CHARGING)
      {
         return;
      }

      uint64_t chargeEndTimeMicros = esp_timer_get_time();
      _chargeTimeMicros = chargeEndTimeMicros - _chargeStartTimeMicros;
      _hasChanged = true;
      _rawSensorRate.tick();

      detachInterrupt(digitalPinToInterrupt(_sensePin));
      esp_timer_stop(_timeoutTimer);

      _state = IDLE;
      _startDischarging();
   }

public:
   /// <summary>
   /// Initializes a capacitor sensor with charge/sense pins and timing options.
   /// </summary>
   /// <param name="chargePin">GPIO pin used to apply charge to the sensor.</param>
   /// <param name="sensePin">GPIO pin used to detect charge threshold crossing.</param>
   /// <param name="dischargeDelayMicros">Discharge delay in microseconds before charging starts.</param>
   /// <param name="rateSamples">Rolling window size used for rate calculations.</param>
   CapacitorSensor(uint8_t chargePin, uint8_t sensePin, uint16_t dischargeDelayMicros = 5000, uint16_t rateSamples = 500)
      : _rawSensorRate(rateSamples)
   {
      _chargePin = chargePin;
      _sensePin = sensePin;
      _dischargeDelayMicros = dischargeDelayMicros;
      _instance = this;
      _hasChanged = false;
   }

   /// <summary>
   /// Sets the discharge delay before each charge cycle.
   /// </summary>
   /// <param name="dischargeDelayMicros">Discharge delay in microseconds.</param>
   void setDischargeDelayMicros(uint16_t dischargeDelayMicros)
   {
      _dischargeDelayMicros = dischargeDelayMicros;
   }

   /// <summary>
   /// Gets the configured discharge delay.
   /// </summary>
   /// <returns>The discharge delay in microseconds.</returns>
   uint16_t dischargeDelayMicros() const
   {
      return _dischargeDelayMicros;
   }

   /// <summary>
   /// Gets the latest charge time measurement.
   /// </summary>
   /// <returns>The most recent charge time in microseconds.</returns>
   uint32_t chargeTimeMicros() const
   {
      return (uint32_t)_chargeTimeMicros;
   }

   /// <summary>
   /// Indicates whether a new measurement has been captured since the last check.
   /// </summary>
   /// <returns>True when a new measurement is available; otherwise false.</returns>
   bool hasChanged()
   {
      if (_hasChanged)
      {
         _hasChanged = false;
         return true;
      }

      return false;
   }

   /// <summary>
   /// Resets the rolling sample-rate tracker.
   /// </summary>
   void resetRate()
   {
      _rawSensorRate.reset();
   }

   /// <summary>
   /// Gets the rolling sample rate.
   /// </summary>
   /// <returns>Samples per second over the configured rolling window.</returns>
   float rate()
   {
      return _rawSensorRate.get();
   }

   /// <summary>
   /// Configures GPIO and timer resources, then starts measurement cycling.
   /// </summary>
   void begin()
   {
      pinMode(_chargePin, OUTPUT);
      digitalWrite(_chargePin, LOW);
      pinMode(_sensePin, INPUT);

      esp_timer_create_args_t dischargeArgs = {};
      dischargeArgs.callback = &_onDischargeElapsed;
      dischargeArgs.arg = nullptr;
      dischargeArgs.dispatch_method = ESP_TIMER_TASK;
      dischargeArgs.name = "cap_discharge";
      esp_timer_create(&dischargeArgs, &_dischargeTimer);

      esp_timer_create_args_t timeoutArgs = {};
      timeoutArgs.callback = &_onChargeTimeout;
      timeoutArgs.arg = nullptr;
      timeoutArgs.dispatch_method = ESP_TIMER_TASK;
      timeoutArgs.name = "cap_timeout";
      esp_timer_create(&timeoutArgs, &_timeoutTimer);

      _startDischarging();
   }
};
