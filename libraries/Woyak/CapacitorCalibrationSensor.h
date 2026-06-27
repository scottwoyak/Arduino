#pragma once

#include <esp_timer.h>
#include "RollingRate.h"
#include "Tick.h"

/// <summary>
/// Provides interrupt-driven capacitor charge-time measurements using timer callbacks
/// for discharge and timeout control, and queues every captured event.
/// </summary>
class CapacitorCalibrationSensor
{
private:
   /// <summary>
   /// Internal measurement state machine.
   /// </summary>
   enum State
   {
      IDLE,
      DISCHARGING,
      CHARGING
   };

   static constexpr uint64_t CHARGE_TIMEOUT_US = 200000;
   static constexpr uint16_t RATE_SAMPLES = 500;
   static constexpr uint8_t EVENT_QUEUE_SIZE = 32;

   uint8_t _chargePin;
   uint8_t _sensePin;
   uint16_t _dischargeDelayMicros;

   volatile State _state = State::IDLE;
   volatile uint32_t _chargeStartTicks = 0;
   volatile bool _chargeStartPending = false;
   volatile float _latestChargeTimeMicros = 0;
   volatile bool _hasChanged = false;

   volatile float _queuedChargeTimesMicros[EVENT_QUEUE_SIZE] = { 0 };
   volatile uint64_t _queuedChargeEndTimesMicros[EVENT_QUEUE_SIZE] = { 0 };
   volatile uint8_t _queueHead = 0;
   volatile uint8_t _queueTail = 0;
   volatile uint8_t _queueCount = 0;
   volatile uint32_t _droppedQueuedEvents = 0;

   RollingRate _rawSensorRate;

   esp_timer_handle_t _dischargeTimer = nullptr;
   esp_timer_handle_t _timeoutTimer = nullptr;

   static inline CapacitorCalibrationSensor* _instance;

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
      uint64_t chargeEndTimeMicros = static_cast<uint64_t>(micros());
      _latestChargeTimeMicros = chargeTimeMicros;
      _hasChanged = true;

      _queuedChargeTimesMicros[_queueHead] = chargeTimeMicros;
      _queuedChargeEndTimesMicros[_queueHead] = chargeEndTimeMicros;
      _queueHead = static_cast<uint8_t>((_queueHead + 1) % EVENT_QUEUE_SIZE);

      if (_queueCount < EVENT_QUEUE_SIZE)
      {
         _queueCount = static_cast<uint8_t>(_queueCount + 1);
      }
      else
      {
         _queueTail = static_cast<uint8_t>((_queueTail + 1) % EVENT_QUEUE_SIZE);
         _droppedQueuedEvents = _droppedQueuedEvents + 1;
      }

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
   CapacitorCalibrationSensor(uint8_t chargePin, uint8_t sensePin, uint16_t dischargeDelayMicros = 500)
      : _rawSensorRate(RATE_SAMPLES)
   {
      _chargePin = chargePin;
      _sensePin = sensePin;
      _dischargeDelayMicros = dischargeDelayMicros;
      _instance = this;
      _chargeStartPending = false;
      _latestChargeTimeMicros = 0;
      _hasChanged = false;
      _queueHead = 0;
      _queueTail = 0;
      _queueCount = 0;
      _droppedQueuedEvents = 0;
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
   float chargeTimeMicros() const
   {
      return _latestChargeTimeMicros;
   }

   /// <summary>
   /// Gets and consumes the next queued measurement if available.
   /// </summary>
   /// <param name="chargeTimeMicros">Output for queued charge time in microseconds.</param>
   /// <param name="chargeEndMicros">Output for queued charge completion timestamp in microseconds.</param>
   /// <returns>True when a queued measurement was consumed; otherwise false.</returns>
   bool tryDequeue(float& chargeTimeMicros, uint64_t& chargeEndMicros)
   {
      _servicePendingChargeStart();

      noInterrupts();
      if (_queueCount == 0)
      {
         interrupts();
         return false;
      }

      uint8_t tail = _queueTail;
      chargeTimeMicros = _queuedChargeTimesMicros[tail];
      chargeEndMicros = _queuedChargeEndTimesMicros[tail];
      _queueTail = static_cast<uint8_t>((_queueTail + 1) % EVENT_QUEUE_SIZE);
      _queueCount = static_cast<uint8_t>(_queueCount - 1);
      interrupts();

      return true;
   }

   /// <summary>
   /// Gets and consumes the next queued measurement if available.
   /// </summary>
   /// <param name="chargeTimeMicros">Output for queued charge time in microseconds.</param>
   /// <returns>True when a queued measurement was consumed; otherwise false.</returns>
   bool tryDequeue(float& chargeTimeMicros)
   {
      uint64_t ignoredEndMicros = 0;
      return tryDequeue(chargeTimeMicros, ignoredEndMicros);
   }

   /// <summary>
   /// Services deferred start requests on the caller's execution context.
   /// </summary>
   void loop()
   {
      _servicePendingChargeStart();
   }

   /// <summary>
   /// Indicates whether a new measurement has been captured since the last check.
   /// </summary>
   /// <returns>True when a new measurement is available; otherwise false.</returns>
   bool hasChanged()
   {
      _servicePendingChargeStart();

      bool hasChanged = _hasChanged;
      _hasChanged = false;
      return hasChanged;
   }

   /// <summary>
   /// Gets the number of queued measurements waiting to be consumed.
   /// </summary>
   uint8_t queuedCount() const
   {
      return _queueCount;
   }

   /// <summary>
   /// Gets the number of events dropped because the queue was full.
   /// </summary>
   uint32_t droppedQueuedEvents() const
   {
      return _droppedQueuedEvents;
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
   /// Resets rolling rate state and clears pending change flags.
   /// </summary>
   void resetRate()
   {
      _rawSensorRate.reset();
      _hasChanged = false;
      _queueHead = 0;
      _queueTail = 0;
      _queueCount = 0;
      _droppedQueuedEvents = 0;
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
