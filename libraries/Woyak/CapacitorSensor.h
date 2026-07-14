#pragma once

#include <Arduino.h>

#if !defined(ARDUINO_ARCH_ESP32)
#error "CapacitorSensor is only supported on ESP32 targets."
#endif

#include <esp_timer.h>
#include <driver/gpio.h>
#include "soc/soc.h"
#include "soc/gpio_reg.h"

#include "RollingAverage.h"
#include "RollingRate.h"


///
/// <summary>
/// Measures capacitor charge time as a rolling average, operating entirely in
/// the background via ESP timers. No service() call is required.
/// </summary>
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
   static constexpr uint16_t RATE_SAMPLES = 100;

   uint8_t _chargePin;
   uint8_t _sensePin;
   uint16_t _dischargeDelayMicros;
   uint32_t _deferredProcessingPeriodMicros;

   // ----------- Core State
   portMUX_TYPE _mux = SPINLOCK_INITIALIZER;
   volatile State _state = State::IDLE;
   volatile int64_t _chargeStartMicros = 0;
   volatile int64_t _chargeEndMicros = 0;
   volatile bool _measurementComplete = false;
   uint32_t _counter = 0;
   bool _started = false;

   // ----------- Analysis
   RollingAverage _average;
   float _latestAverageMicros = NAN;
   RollingRate _rawSensorRate;

   // ----------- Background Timers
   esp_timer_handle_t _dischargeTimer = nullptr;
   esp_timer_handle_t _timeoutTimer = nullptr;
   esp_timer_handle_t _deferredProcessingTimer = nullptr;

   static inline bool _instanceExists = false;

   // ----------- ISR Callback
   static inline CapacitorSensor* _isrContext = nullptr;

   /// <summary>ISR: capture charge end time when the sense pin rises.</summary>
   static void ARDUINO_ISR_ATTR _onPinRising()
   {
      if (_isrContext != nullptr)
      {
         portENTER_CRITICAL_ISR(&_isrContext->_mux);
         if (_isrContext->_state == CHARGING)
         {
            _isrContext->_chargeEndMicros = esp_timer_get_time();
            _isrContext->_measurementComplete = true;
            _isrContext->_state = IDLE;
         }
         portEXIT_CRITICAL_ISR(&_isrContext->_mux);
      }
   }

   // ----------- Timer Callbacks
   /// <summary>Timer callback: discharge period elapsed; begin charging.</summary>
   static void _onDischargeElapsed(void* arg)
   {
      CapacitorSensor* self = static_cast<CapacitorSensor*>(arg);
      if (self != nullptr)
      {
         self->_startCharging();
      }
   }

   /// <summary>Timer callback: charge exceeded timeout; restart cycle.</summary>
   static void _onChargeTimeout(void* arg)
   {
      CapacitorSensor* self = static_cast<CapacitorSensor*>(arg);
      if (self != nullptr)
      {
         self->_handleChargeTimeout();
      }
   }

   /// <summary>Periodic timer callback: process a completed measurement if one is ready.</summary>
   static void _onDeferredProcessing(void* arg)
   {
      CapacitorSensor* self = static_cast<CapacitorSensor*>(arg);
      if (self == nullptr) return;

      bool processNow = false;
      portENTER_CRITICAL(&self->_mux);
      processNow = self->_measurementComplete;
      portEXIT_CRITICAL(&self->_mux);

      if (processNow)
      {
         self->_processMeasurement();
      }
   }

   /// <summary>Drive the sense pin low and start the discharge hold timer.</summary>
   void _startDischarging()
   {
      gpio_intr_disable(static_cast<gpio_num_t>(_sensePin));
      digitalWrite(_chargePin, LOW);

      // Drive sense pin low to actively discharge the capacitor
      pinMode(_sensePin, OUTPUT);
      digitalWrite(_sensePin, LOW);

      portENTER_CRITICAL(&_mux);
      _state = DISCHARGING;
      _measurementComplete = false;
      esp_timer_stop(_dischargeTimer);
      esp_timer_start_once(_dischargeTimer, _dischargeDelayMicros);
      portEXIT_CRITICAL(&_mux);
   }

   /// <summary>Arm the charge timeout, release the sense pin, clear pending interrupts, and apply charge voltage.</summary>
   void _startCharging()
   {
      bool beginCharge = false;
      portENTER_CRITICAL(&_mux);
      if (_state == DISCHARGING)
      {
         _state = CHARGING;
         beginCharge = true;
      }
      portEXIT_CRITICAL(&_mux);

      if (!beginCharge)
      {
         return;
      }

      // 1. Arm timeout timer
      portENTER_CRITICAL(&_mux);
      esp_timer_stop(_timeoutTimer);
      esp_timer_start_once(_timeoutTimer, CHARGE_TIMEOUT_US);
      portEXIT_CRITICAL(&_mux);

      // 2. Release sense pin back to high-impedance input
      pinMode(_sensePin, INPUT);

      // pinMode(INPUT) destroys the interrupt attachment matrix routing on ESP32
      // We must explicitly re-attach it to ensure the edge is caught.
      attachInterrupt(digitalPinToInterrupt(_sensePin), _onPinRising, RISING);

      // 3. Clear pending interrupts to prevent false triggering
      // GPIO 0-31 are in STATUS_W1TC_REG, GPIO 32+ are in STATUS1_W1TC_REG
      gpio_intr_disable(static_cast<gpio_num_t>(_sensePin));
      if (_sensePin < 32)
      {
         REG_WRITE(GPIO_STATUS_W1TC_REG, BIT(_sensePin));
      }
      else
      {
         REG_WRITE(GPIO_STATUS1_W1TC_REG, BIT(_sensePin - 32));
      }

      // 4. Capture start time
      portENTER_CRITICAL(&_mux);
      _chargeStartMicros = esp_timer_get_time();
      portEXIT_CRITICAL(&_mux);

      // 5. Enable interrupt
      gpio_intr_enable(static_cast<gpio_num_t>(_sensePin));

      // 6. Apply charge voltage
      digitalWrite(_chargePin, HIGH);
   }

   /// <summary>Handle a charge timeout by restarting the discharge cycle.</summary>
   void _handleChargeTimeout()
   {
      bool timeoutValid = false;
      portENTER_CRITICAL(&_mux);
      if (_state == CHARGING)
      {
         _state = IDLE;
         timeoutValid = true;
      }
      portEXIT_CRITICAL(&_mux);

      if (!timeoutValid)
      {
         return;
      }

      // Ensure interrupt is disabled on timeout
      gpio_intr_disable(static_cast<gpio_num_t>(_sensePin));

      _startDischarging();
   }

   // ----------- Measurement Processing
   /// <summary>Compute charge time from captured timestamps, update rolling average and rate, then restart discharge.</summary>
   void _processMeasurement()
   {
      int64_t start, end;

      // Disable interrupt before re-entering discharge
      gpio_intr_disable(static_cast<gpio_num_t>(_sensePin));

      portENTER_CRITICAL(&_mux);
      start = _chargeStartMicros;
      end = _chargeEndMicros;
      _measurementComplete = false;
      _counter++;
      esp_timer_stop(_timeoutTimer);
      portEXIT_CRITICAL(&_mux);

      float chargeTimeMicros = static_cast<float>(end - start);

      if (isfinite(chargeTimeMicros) && (chargeTimeMicros > 0.0f) && (chargeTimeMicros <= static_cast<float>(CHARGE_TIMEOUT_US)))
      {
         _average.set(chargeTimeMicros);

         portENTER_CRITICAL(&_mux);
         _latestAverageMicros = _average.get();
         _rawSensorRate.tick();
         portEXIT_CRITICAL(&_mux);
      }

      _startDischarging();
   }

   /// <summary>Start the deferred processing timer and begin the first discharge cycle.</summary>
   void start()
   {
      if (_started)
      {
         return;
      }

      _started = true;
      esp_timer_start_periodic(_deferredProcessingTimer, _deferredProcessingPeriodMicros);
      _startDischarging();
   }

   /// <summary>Stop all background timers, disable the sense interrupt, and drive pins to a safe idle state.</summary>
   void stop()
   {
      portENTER_CRITICAL(&_mux);
      _started = false;
      _state = IDLE;
      _measurementComplete = false;
      if (_dischargeTimer != nullptr) esp_timer_stop(_dischargeTimer);
      if (_timeoutTimer != nullptr) esp_timer_stop(_timeoutTimer);
      if (_deferredProcessingTimer != nullptr) esp_timer_stop(_deferredProcessingTimer);
      portEXIT_CRITICAL(&_mux);

      // Disable interrupt but keep the attachment intact
      gpio_intr_disable(static_cast<gpio_num_t>(_sensePin));

      pinMode(_chargePin, OUTPUT);
      digitalWrite(_chargePin, LOW);
      pinMode(_sensePin, INPUT);
   }

public:
   /// <summary>Default discharge hold time in microseconds.</summary>
   static constexpr uint16_t DEFAULT_DISCHARGE_DELAY_MICROS = 200;

   /// <summary>Default deferred processing period in microseconds.</summary>
   static constexpr uint32_t DEFAULT_DEFERRED_PROCESSING_PERIOD_MICROS = 500;

   /// <summary>Default rolling average window size.</summary>
   static constexpr size_t DEFAULT_BUFFER_SIZE = 30;

   /// <summary>Construct and configure a capacitor sensor. Only one instance may exist at a time.</summary>
   /// <param name="chargePin">Output pin that drives the charge resistor</param>
   /// <param name="sensePin">Input pin that detects when the capacitor is fully charged</param>
   /// <param name="dischargeDelayMicros">Discharge hold time in microseconds</param>
   /// <param name="averageSamples">Rolling average window size</param>
   CapacitorSensor(
      uint8_t chargePin,
      uint8_t sensePin,
      uint16_t dischargeDelayMicros = DEFAULT_DISCHARGE_DELAY_MICROS,
      size_t averageSamples = DEFAULT_BUFFER_SIZE)
      : _average(averageSamples),
      _rawSensorRate(RATE_SAMPLES)
   {
     if (_instanceExists)
     {
        abort();
     }

     _instanceExists = true;

      _chargePin = chargePin;
      _sensePin = sensePin;
      _dischargeDelayMicros = dischargeDelayMicros;
      _deferredProcessingPeriodMicros = DEFAULT_DEFERRED_PROCESSING_PERIOD_MICROS;
      _isrContext = this;
   }

   /// <summary>Stop background timers, release ESP timer handles, and detach the GPIO interrupt.</summary>
   ~CapacitorSensor()
   {
      stop();
      if (_dischargeTimer != nullptr) esp_timer_delete(_dischargeTimer);
      if (_timeoutTimer != nullptr) esp_timer_delete(_timeoutTimer);
      if (_deferredProcessingTimer != nullptr) esp_timer_delete(_deferredProcessingTimer);

      detachInterrupt(digitalPinToInterrupt(_sensePin));

      _isrContext = nullptr;
      _instanceExists = false;
   }

   /// <summary>Initialize GPIO pins and ESP timers, then start background measurement.</summary>
   void begin()
   {
      pinMode(_chargePin, OUTPUT);
      digitalWrite(_chargePin, LOW);
      pinMode(_sensePin, INPUT);

      // Attach interrupt once; keep it disabled until charging begins
      attachInterrupt(digitalPinToInterrupt(_sensePin), _onPinRising, RISING);
      gpio_intr_disable(static_cast<gpio_num_t>(_sensePin));

      esp_timer_create_args_t dischargeArgs = {};
      dischargeArgs.callback = &_onDischargeElapsed;
      dischargeArgs.arg = this;
      dischargeArgs.dispatch_method = ESP_TIMER_TASK;
      dischargeArgs.name = "cap_discharge";
      esp_timer_create(&dischargeArgs, &_dischargeTimer);

      esp_timer_create_args_t timeoutArgs = {};
      timeoutArgs.callback = &_onChargeTimeout;
      timeoutArgs.arg = this;
      timeoutArgs.dispatch_method = ESP_TIMER_TASK;
      timeoutArgs.name = "cap_timeout";
      esp_timer_create(&timeoutArgs, &_timeoutTimer);

      esp_timer_create_args_t processArgs = {};
      processArgs.callback = &_onDeferredProcessing;
      processArgs.arg = this;
      processArgs.dispatch_method = ESP_TIMER_TASK;
      processArgs.name = "cap_process";
      esp_timer_create(&processArgs, &_deferredProcessingTimer);

      start();
   }

   /// <summary>Switch to a different charge pin, resetting all rolling statistics.</summary>
   /// <param name="chargePin">New output pin to drive the charge resistor</param>
   void setChargePin(uint8_t chargePin)
   {
      if (_chargePin == chargePin)
      {
         return;
      }

      bool wasStarted = _started;
      if (wasStarted) stop();

      // stop() leaves the old charge pin driven LOW as an output; release it to
      // high-impedance input so it no longer forms a voltage divider with the new
      // charge resistor through the shared capacitor/sense node.
      pinMode(_chargePin, INPUT);

      _chargePin = chargePin;

      portENTER_CRITICAL(&_mux);
      _rawSensorRate.reset();
      _latestAverageMicros = NAN;
      _chargeEndMicros = 0;
      portEXIT_CRITICAL(&_mux);

      _average.reset();

      if (wasStarted)
      {
         pinMode(_chargePin, OUTPUT);
         digitalWrite(_chargePin, LOW);
         start();
      }
   }

   /// <summary>Get the latest rolling-average charge time.</summary>
   /// <returns>Charge time in microseconds, or NaN when no measurement is available</returns>
   float chargeTimeMicros()
   {
      portENTER_CRITICAL(&_mux);
      float val = _latestAverageMicros;
      portEXIT_CRITICAL(&_mux);
      return val;
   }

   /// <summary>Get the latest rolling-average charge time.</summary>
   /// <returns>Charge time in microseconds, or NaN when no measurement is available</returns>
   float chargeTimeMicros() const
   {
      return const_cast<CapacitorSensor*>(this)->chargeTimeMicros();
   }

   /// <summary>Get the raw sensor sample rate.</summary>
   /// <returns>Samples per second</returns>
   float rate()
   {
      portENTER_CRITICAL(&_mux);
      float val = _rawSensorRate.get();
      portEXIT_CRITICAL(&_mux);
      return val;
   }

   /// <summary>Get the processed measurement counter.</summary>
   /// <returns>Monotonic count of processed measurements</returns>
   uint32_t counter()
   {
      portENTER_CRITICAL(&_mux);
      uint32_t val = _counter;
      portEXIT_CRITICAL(&_mux);
      return val;
   }

   /// <summary>Set the discharge hold time.</summary>
   /// <param name="dischargeDelayMicros">Discharge hold time in microseconds</param>
   void setDischargeDelayMicros(uint16_t dischargeDelayMicros)
   {
      _dischargeDelayMicros = dischargeDelayMicros;
   }

   /// <summary>Get the current discharge hold time in microseconds.</summary>
   /// <returns>Discharge delay in microseconds</returns>
   uint16_t dischargeDelayMicros() const
   {
      return _dischargeDelayMicros;
   }

   /// <summary>Set the deferred processing period.</summary>
   /// <param name="deferredProcessingPeriodMicros">Processing period in microseconds (minimum 1)</param>
   void setDeferredProcessingPeriodMicros(uint32_t deferredProcessingPeriodMicros)
   {
      if (deferredProcessingPeriodMicros == 0)
      {
         deferredProcessingPeriodMicros = 1;
      }

      bool isStarted = false;
      portENTER_CRITICAL(&_mux);
      _deferredProcessingPeriodMicros = deferredProcessingPeriodMicros;
      isStarted = _started;
      portEXIT_CRITICAL(&_mux);

      if (isStarted && _deferredProcessingTimer != nullptr)
      {
         esp_timer_stop(_deferredProcessingTimer);
         esp_timer_start_periodic(_deferredProcessingTimer, _deferredProcessingPeriodMicros);
      }
   }

   /// <summary>Get the current deferred processing period in microseconds.</summary>
   /// <returns>Deferred processing period in microseconds</returns>
   uint32_t deferredProcessingPeriodMicros() const
   {
      return _deferredProcessingPeriodMicros;
   }

   /// <summary>Resize the rolling average buffer, clearing all existing samples.</summary>
   /// <param name="size">New buffer size</param>
   void setBufferSize(size_t size)
   {
      _average.reset(size);
   }

   /// <summary>Get the current rolling average buffer size.</summary>
   /// <returns>Number of samples in the rolling window</returns>
   size_t bufferSize() const
   {
      return _average.size();
   }

   /// <summary>Get the active charge pin.</summary>
   /// <returns>Charge pin number</returns>
   uint8_t chargePin() const
   {
      return _chargePin;
   }
};
