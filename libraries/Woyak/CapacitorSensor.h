#pragma once

#include <esp_timer.h>
#include "RollingRate.h"
#include "RollingStats.h"

/// <summary>
/// Provides interrupt-driven capacitor charge-time measurements using timer callbacks
/// for discharge and timeout control.
/// </summary>
class CapacitorSensor
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
   volatile uint64_t _chargeStartTimeMicros = 0;
   volatile uint64_t _latestChargeTimeMicros = 0;
   volatile bool _hasChanged = false;

   volatile uint64_t _queuedChargeTimesMicros[EVENT_QUEUE_SIZE] = { 0 };
   volatile uint64_t _queuedChargeEndTimesMicros[EVENT_QUEUE_SIZE] = { 0 };
   volatile uint8_t _queueHead = 0;
   volatile uint8_t _queueTail = 0;
   volatile uint8_t _queueCount = 0;
   volatile uint32_t _droppedQueuedEvents = 0;

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
         _instance->_startCharging();
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
      uint64_t chargeTimeMicros = chargeEndTimeMicros - _chargeStartTimeMicros;
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
   CapacitorSensor(uint8_t chargePin, uint8_t sensePin, uint16_t dischargeDelayMicros = 500)
      : _rawSensorRate(RATE_SAMPLES)
   {
      _chargePin = chargePin;
      _sensePin = sensePin;
      _dischargeDelayMicros = dischargeDelayMicros;
      _instance = this;
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
   uint32_t chargeTimeMicros() const
   {
      return static_cast<uint32_t>(_latestChargeTimeMicros);
   }

   /// <summary>
   /// Gets and consumes the next queued measurement if available.
   /// </summary>
   /// <param name="chargeTimeMicros">Output for queued charge time in microseconds.</param>
   /// <param name="chargeEndMicros">Output for queued charge completion timestamp in microseconds.</param>
   /// <returns>True when a queued measurement was consumed; otherwise false.</returns>
   bool tryDequeue(uint32_t& chargeTimeMicros, uint64_t& chargeEndMicros)
   {
      noInterrupts();
      if (_queueCount == 0)
      {
         interrupts();
         return false;
      }

      uint8_t tail = _queueTail;
      chargeTimeMicros = static_cast<uint32_t>(_queuedChargeTimesMicros[tail]);
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
   bool tryDequeue(uint32_t& chargeTimeMicros)
   {
      uint64_t ignoredEndMicros = 0;
      return tryDequeue(chargeTimeMicros, ignoredEndMicros);
   }

   /// <summary>
   /// Indicates whether a new measurement has been captured since the last check.
   /// </summary>
   /// <returns>True when a new measurement is available; otherwise false.</returns>
   bool hasChanged()
   {
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

/// <summary>
/// Wraps a CapacitorSensor with rolling statistics for charge-time measurements.
/// </summary>
/// <remarks>
/// Call loop() frequently from the sketch loop to consume newly captured samples
/// and update rolling statistics.
/// </remarks>
class RollingCapacitiveSensor
{
private:
   CapacitorSensor _sensor;
   RollingStats _stats;
   RollingStats _averageStats;

public:
   /// <summary>
   /// Initializes a rolling capacitor sensor wrapper.
   /// </summary>
   /// <param name="chargePin">GPIO pin used to apply charge to the sensor.</param>
   /// <param name="sensePin">GPIO pin used to detect charge threshold crossing.</param>
   /// <param name="rollingBufferSize">Rolling sample window size for statistics.</param>
   /// <param name="dischargeDelayMicros">Discharge delay in microseconds before charging starts.</param>
   RollingCapacitiveSensor(
      uint8_t chargePin,
      uint8_t sensePin,
      size_t rollingBufferSize,
      uint16_t dischargeDelayMicros = 500)
      : _sensor(chargePin, sensePin, dischargeDelayMicros),
      _stats(rollingBufferSize),
      _averageStats(rollingBufferSize)
   {
   }

   /// <summary>
   /// Configures GPIO and timer resources, then starts measurement cycling.
   /// </summary>
   void begin()
   {
      _sensor.begin();
   }

   /// <summary>
   /// Consumes new sensor measurements and updates the rolling statistics.
   /// Must be called frequently from the sketch loop.
   /// </summary>
   void loop()
   {
      uint32_t chargeTime = 0;
      while (_sensor.tryDequeue(chargeTime))
      {
         _stats.set(static_cast<float>(chargeTime));

         float avg = _stats.average();
         if (isfinite(avg))
         {
            _averageStats.set(avg);
         }
      }
   }

   /// <summary>
   /// Sets the discharge delay before each charge cycle.
   /// </summary>
   /// <param name="dischargeDelayMicros">Discharge delay in microseconds.</param>
   void setDischargeDelayMicros(uint16_t dischargeDelayMicros)
   {
      _sensor.setDischargeDelayMicros(dischargeDelayMicros);
   }

   /// <summary>
   /// Gets the configured discharge delay.
   /// </summary>
   /// <returns>The discharge delay in microseconds.</returns>
   uint16_t dischargeDelayMicros() const
   {
      return _sensor.dischargeDelayMicros();
   }

   /// <summary>
   /// Gets the latest raw charge time measurement.
   /// </summary>
   /// <returns>The most recent charge time in microseconds.</returns>
   uint32_t chargeTimeMicros() const
   {
      return _sensor.chargeTimeMicros();
   }

   /// <summary>
   /// Gets the rolling average charge time, or NAN if the buffer is not yet full.
   /// </summary>
   /// <returns>Average charge time in microseconds, or NAN if insufficient data.</returns>
   float average() const
   {
      if (_stats.count() < _stats.size())
      {
         return NAN;
      }
      return _stats.average();
   }

   /// <summary>
   /// Resets rolling statistics and optionally updates the discharge delay and buffer size.
   /// </summary>
   /// <param name="dischargeDelayMicros">New discharge delay in microseconds, or 0 to keep the current value.</param>
   /// <param name="rollingBufferSize">New rolling buffer size, or 0 to keep the current value.</param>
   void reset(uint16_t dischargeDelayMicros = 0, size_t rollingBufferSize = 0)
   {
      if (dischargeDelayMicros != 0)
      {
         _sensor.setDischargeDelayMicros(dischargeDelayMicros);
      }

      _sensor.resetRate();
      _stats.reset(rollingBufferSize);
      _averageStats.reset(rollingBufferSize);
   }

   /// <summary>
   /// Gets the rolling minimum charge time.
   /// </summary>
   /// <returns>Minimum charge time in microseconds.</returns>
   float min() const
   {
      return _stats.min();
   }

   /// <summary>
   /// Gets the rolling maximum charge time.
   /// </summary>
   /// <returns>Maximum charge time in microseconds.</returns>
   float max() const
   {
      return _stats.max();
   }

   /// <summary>
   /// Gets max-minus-min across the rolling raw charge-time window, or NAN if the buffer is not yet full.
   /// </summary>
   /// <returns>Range in microseconds, or NAN if insufficient data.</returns>
   float range() const
   {
      if (_stats.count() < _stats.size())
      {
         return NAN;
      }
      return _stats.range();
   }

   /// <summary>
   /// Gets max-minus-min across the rolling history of average values, or NAN if the buffer is not yet full.
   /// </summary>
   /// <returns>Range of rolling averages in microseconds, or NAN if insufficient data.</returns>
   float averageRange() const
   {
      if (_stats.count() < _stats.size())
      {
         return NAN;
      }
      return _averageStats.range();
   }

   /// <summary>
   /// Gets the number of finite values currently in the rolling window.
   /// </summary>
   /// <returns>Current rolling sample count.</returns>
   size_t count() const
   {
      return _stats.count();
   }

   /// <summary>
   /// Gets the configured rolling buffer size.
   /// </summary>
   /// <returns>Rolling buffer capacity.</returns>
   size_t bufferSize() const
   {
      return _stats.size();
   }

   /// <summary>
   /// Gets the rolling raw sensor sample rate.
   /// </summary>
   /// <returns>Samples per second over the configured sensor rate window.</returns>
   float rate()
   {
      return _sensor.rate();
   }
};
