#pragma once

#include <esp_timer.h>
#include "RollingAverage.h"
#include "RollingRate.h"
#include "Tick.h"

///
/// <summary>
/// Provides simplified capacitor measurements as a rolling average charge time.
/// </summary>
/// <remarks>
/// This implementation is self-contained and intentionally does not expose queued
/// per-measurement access.
/// </remarks>
/// 
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
   static constexpr uint16_t RATE_SAMPLES = 500;
   static constexpr size_t DEFAULT_AVERAGE_SAMPLES = 32;

   uint8_t _chargePin;
   uint8_t _sensePin;
   uint16_t _dischargeDelayMicros;

   volatile State _state = State::IDLE;
   volatile uint32_t _chargeStartTicks = 0;
   volatile bool _chargeStartPending = false;
   bool _started = false;

   RollingAverage _average;
   volatile float _latestAverageMicros = NAN;
   RollingRate _rawSensorRate;

   esp_timer_handle_t _dischargeTimer = nullptr;
   esp_timer_handle_t _timeoutTimer = nullptr;

   static inline CapacitorSensor* _instance;

   static void _onPinRising()
   {
      if (_instance != nullptr)
      {
         _instance->_onCharged();
      }
   }

   static void _onDischargeElapsed(void*)
   {
      if (_instance != nullptr)
      {
         _instance->_chargeStartPending = true;
      }
   }

   static void _onChargeTimeout(void*)
   {
      if (_instance != nullptr)
      {
         _instance->_handleChargeTimeout();
      }
   }

   void _startDischarging()
   {
      detachInterrupt(digitalPinToInterrupt(_sensePin));

      digitalWrite(_chargePin, LOW);
      pinMode(_sensePin, OUTPUT);
      digitalWrite(_sensePin, LOW);

      _state = DISCHARGING;
      _chargeStartPending = false;

      esp_timer_stop(_dischargeTimer);
      esp_timer_start_once(_dischargeTimer, _dischargeDelayMicros);
   }

   void _startCharging()
   {
      if (_state != DISCHARGING)
      {
         return;
      }

      _chargeStartPending = false;
      digitalWrite(_chargePin, HIGH);
      _chargeStartTicks = Tick::now();
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

   void _servicePendingChargeStart()
   {
      if (!_chargeStartPending)
      {
         return;
      }

      if (_state != DISCHARGING)
      {
         return;
      }

      _startCharging();
   }

   void _onCharged()
   {
      if (_state != CHARGING)
      {
         return;
      }

      uint32_t chargeEndTicks = Tick::now();
      float chargeTimeMicros = static_cast<float>(Tick::elapsedMicros(_chargeStartTicks, chargeEndTicks));
      _average.set(chargeTimeMicros);
      _latestAverageMicros = _average.get();

      _rawSensorRate.tick();

      detachInterrupt(digitalPinToInterrupt(_sensePin));
      esp_timer_stop(_timeoutTimer);

      _state = IDLE;
      _startDischarging();
   }

public:
   /// <summary>
   /// Initializes a simplified capacitor sensor that reports rolling-average charge time.
   /// </summary>
   /// <param name="chargePin">GPIO pin used to apply charge to the sensor.</param>
   /// <param name="sensePin">GPIO pin used to detect charge threshold crossing.</param>
   /// <param name="dischargeDelayMicros">Discharge delay in microseconds before charging starts.</param>
   /// <param name="averageSamples">Rolling average sample window size.</param>
   CapacitorSensor(
      uint8_t chargePin,
      uint8_t sensePin,
      uint16_t dischargeDelayMicros = 500,
      size_t averageSamples = DEFAULT_AVERAGE_SAMPLES)
      : _average(averageSamples),
      _rawSensorRate(RATE_SAMPLES)
   {
      _chargePin = chargePin;
      _sensePin = sensePin;
      _dischargeDelayMicros = dischargeDelayMicros;
      _instance = this;
      _chargeStartPending = false;
      _started = false;
      _latestAverageMicros = NAN;
   }

   /// <summary>
   /// Configures GPIO/timer resources and starts measurement cycling.
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

      start();
   }

   /// <summary>
   /// Starts measurement cycling. Must be called after begin().
   /// </summary>
   void start()
   {
      if (_started)
      {
         return;
      }

      _started = true;
      _startDischarging();
   }

   /// <summary>
   /// Updates the GPIO pin used to charge the sensor.
   /// </summary>
   /// <param name="chargePin">GPIO pin used to apply charge to the sensor.</param>
   void setChargePin(uint8_t chargePin)
   {
      if (_chargePin == chargePin)
      {
         return;
      }

      bool wasStarted = _started;
      if (wasStarted)
      {
         digitalWrite(_chargePin, LOW);
         pinMode(_chargePin, INPUT);
      }

      _chargePin = chargePin;
      _rawSensorRate.reset();
      _latestAverageMicros = NAN;

      if (wasStarted)
      {
         pinMode(_chargePin, OUTPUT);
         digitalWrite(_chargePin, LOW);
         _state = IDLE;
         _startDischarging();
      }
   }

   /// <summary>
   /// Gets the configured charge GPIO pin.
   /// </summary>
   /// <returns>The GPIO pin used to apply charge to the sensor.</returns>
   uint8_t chargePin() const
   {
      return _chargePin;
   }

   /// <summary>
   /// Gets the configured sense GPIO pin.
   /// </summary>
   /// <returns>The GPIO pin used to detect charge threshold crossing.</returns>
   uint8_t sensePin() const
   {
      return _sensePin;
   }

   /// <summary>
   /// Sets the discharge delay before each charge cycle. Resets rate tracking.
   /// </summary>
   /// <param name="dischargeDelayMicros">Discharge delay in microseconds.</param>
   void setDischargeDelayMicros(uint16_t dischargeDelayMicros)
   {
      _dischargeDelayMicros = dischargeDelayMicros;
      _rawSensorRate.reset();
      _latestAverageMicros = NAN;
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
   /// Gets the latest rolling-average charge time measurement.
   /// </summary>
   /// <returns>The rolling-average charge time in microseconds.</returns>
   float chargeTimeMicros() const
   {
      const_cast<CapacitorSensor*>(this)->_servicePendingChargeStart();
      return _latestAverageMicros;
   }

   /// <summary>
   /// Gets the latest rolling-average charge time measurement.
   /// </summary>
   /// <returns>The rolling-average charge time in microseconds.</returns>
   float averageMicros() const
   {
      const_cast<CapacitorSensor*>(this)->_servicePendingChargeStart();
      return _latestAverageMicros;
   }

   /// <summary>
   /// Gets the rolling sample rate.
   /// </summary>
   /// <returns>Samples per second over the configured rolling window.</returns>
   float rate()
   {
      _servicePendingChargeStart();
      return _rawSensorRate.get();
   }
};

