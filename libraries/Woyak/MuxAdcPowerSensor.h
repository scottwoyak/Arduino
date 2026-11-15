#pragma once

#include <Arduino.h>
#include <array>
#include <cmath>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "PowerSensorBase.h"
#include "Util.h"

///
/// <summary>
/// Multi-channel AC current and real-power (Watts) sensor using a 74HC4067 16:1 analog
/// multiplexer and a ZMPT101B AC voltage sensor connected directly to two channels of the
/// ESP32's internal SAR ADC1 (e.g. GPIO22 for mux SIG and GPIO21 for ZMPT101B OUT on ESP32-P4).
///
/// Each CT has its own burden resistor on the PCB and returns to a shared, capacitor-decoupled
/// 1.65V mid-rail bias divider. A periodic 1,000 Hz hardware timer wakes a dedicated sampler
/// task that captures time-aligned (V1, I, V2) sample triplets with dummy S&H pre-charge reads
/// so V = 0.5 * (V1 + V2) has zero phase lag and zero mux cross-talk relative to current I.
/// Connected CT channels are integrated over exact 50.000 ms (3-cycle) windows to compute
/// DC-removed RMS current and true real power P = |mean(v_ac * i_ac)| across both L1 and L2
/// split-phase legs, while an independent 1.2 s rolling accumulator keeps live AC line voltage
/// steady even when all CTs are unplugged.
/// </summary>
///
class MuxAdcPowerSensor : public PowerSensorBase
{
public:
   static constexpr float DEFAULT_VOLTAGE_SCALE = 120.0f / 0.100f; // 120.0V line per 0.100V RMS at ZMPT101B OUT
   static constexpr float MIN_CONNECTED_BIAS_V = 0.85f;
   static constexpr float MAX_CONNECTED_BIAS_V = 2.45f;
   static constexpr float MIN_VALID_VOLTAGE_BIAS_V = 0.50f;
   static constexpr float MAX_VALID_VOLTAGE_BIAS_V = 2.80f;
   static constexpr float MIN_VALID_VOLTAGE_RMS_V = 0.015f;

private:
   static constexpr uint32_t SAMPLE_INTERVAL_MICROS = 1000; // 1,000 Hz (50 sample pairs per 50 ms = 16.7 per 60 Hz cycle)
   static constexpr double RMS_SLICE_SPAN_MICROS = 50000.0; // Exact 50.000 ms (3.0000 cycles at 60 Hz)
   static constexpr double PROBE_SPAN_MICROS = 3000.0;      // 3.0 ms settled window to check DC bias on unplugged channels
   static constexpr double MAX_VALID_DT_MICROS = 20000.0;   // Reset slice integration if paused > 20 ms
   static constexpr uint8_t MUX_SETTLE_SAMPLES = 3;         // Discard 3 ticks (3.0 ms) after switching the external mux
   static constexpr uint8_t PROBE_PASS_INTERVAL = 4;        // Probe one unconnected channel every 4th pass (~200 ms)
   static constexpr size_t VOLTAGE_TRACE_SLICES = 24;       // 24 x 50 ms = 1.2 s (72 AC cycles) rolling voltage average
   static constexpr uint32_t SAMPLER_TASK_STACK_BYTES = 4096;
   static constexpr UBaseType_t SAMPLER_TASK_PRIORITY = 2;
   static constexpr double NOMINAL_MIDRAIL_V = 1.65;
   static constexpr float RAW_FULL_SCALE_COUNTS = 4095.0f;
   static constexpr float RAW_REF_VOLTS = 3.3f;

   std::array<uint8_t, 4> _pinSelect = {};
   uint8_t _pinMuxSig = 0;
   uint8_t _pinAcVoltage = 0;
   float _voltageScale = DEFAULT_VOLTAGE_SCALE;
   float _nominalLineVoltageV = DEFAULT_LINE_VOLTAGE_V;

   adc_unit_t _adcUnit = ADC_UNIT_1;
   adc_channel_t _currentChannel = ADC_CHANNEL_0;
   adc_channel_t _voltageChannel = ADC_CHANNEL_0;
   adc_oneshot_unit_handle_t _adcHandle = nullptr;
   adc_cali_handle_t _caliHandle = nullptr;
   float _voltsPerCount = RAW_REF_VOLTS / RAW_FULL_SCALE_COUNTS;
   float _voltsOffset = 0.0f;

   esp_timer_handle_t _sampleTimer = nullptr;
   TaskHandle_t _samplerTask = nullptr;
   volatile bool _stopSampler = false;
   int64_t _lastSampleMicros = 0;
   size_t _activeChannel = 0;
   size_t _probeCursor = 0;
   uint8_t _passesSinceProbe = 0;
   bool _probingUnconnected = false;
   uint8_t _settleSamplesRemaining = 0;

   bool _chHasPrev = false;
   double _chPrevDV = 0.0;
   double _chPrevDI = 0.0;
   double _chElapsedMicros = 0.0;
   double _chSumDV = 0.0;
   double _chSumDI = 0.0;
   double _chSumDI2 = 0.0;
   double _chSumDVI = 0.0;

   bool _voltHasPrev = false;
   double _voltPrevDV = 0.0;
   double _voltWinElapsedMicros = 0.0;
   double _voltWinSumDV = 0.0;
   double _voltWinSumDV2 = 0.0;
   std::array<float, VOLTAGE_TRACE_SLICES> _voltTraceRms = {};
   size_t _voltTraceWriteIndex = 0;
   size_t _voltTraceCount = 0;
   volatile float _voltageDcVolts = 0.0f;
   volatile float _voltageRmsVolts = 0.0f;
   volatile bool _voltageConnected = false;

   uint32_t _sampleCounter = 0;
   uint32_t _lastRateMillis = 0;
   volatile uint32_t _samplesPerSec = 0;

   ///
   /// <summary>
   /// Drives the 74HC4067 address select lines for the given channel.
   /// </summary>
   /// <param name="channel">Mux channel (0 to 15).</param>
   ///
   void _applyMuxChannel(size_t channel) const
   {
      for (size_t bit = 0; bit < _pinSelect.size(); bit++)
      {
         digitalWrite(_pinSelect[bit], (channel >> bit) & 1);
      }
   }

   ///
   /// <summary>
   /// Resets the active CT channel's per-slice trapezoidal accumulators.
   /// </summary>
   ///
   void _resetChannelAccumulators()
   {
      _chHasPrev = false;
      _chPrevDV = 0.0;
      _chPrevDI = 0.0;
      _chElapsedMicros = 0.0;
      _chSumDV = 0.0;
      _chSumDI = 0.0;
      _chSumDI2 = 0.0;
      _chSumDVI = 0.0;
   }

   ///
   /// <summary>
   /// Resets the continuous AC voltage window's trapezoidal accumulators.
   /// </summary>
   ///
   void _resetVoltageAccumulators()
   {
      _voltHasPrev = false;
      _voltPrevDV = 0.0;
      _voltWinElapsedMicros = 0.0;
      _voltWinSumDV = 0.0;
      _voltWinSumDV2 = 0.0;
   }

   ///
   /// <summary>
   /// Switches the active multiplexer channel and starts the settling counter if the physical
   /// channel changed.
   /// </summary>
   /// <param name="nextChannel">Next channel index to select.</param>
   /// <param name="probeMode">True if the next channel is an unconnected DC-bias probe.</param>
   ///
   void _selectChannel(size_t nextChannel, bool probeMode)
   {
      bool changed = (nextChannel != _activeChannel);
      _activeChannel = nextChannel;
      _probingUnconnected = probeMode;
      _resetChannelAccumulators();
      if (changed)
      {
         _applyMuxChannel(nextChannel);
         _settleSamplesRemaining = MUX_SETTLE_SAMPLES;
      }
   }

   ///
   /// <summary>
   /// Advances to the next CT channel to sample. Connected channels are sampled in a round-robin
   /// loop with full 50 ms 3-cycle slices; after a complete pass of connected channels, all
   /// channels are latched together. Every PROBE_PASS_INTERVAL passes (or immediately if no
   /// channels are connected), one unconnected channel is probed to detect newly plugged-in CTs.
   /// </summary>
   ///
   void _advanceChannel()
   {
      if (_numChannels <= 1)
      {
         _latchPassSnapshot();
         _probingUnconnected = false;
         _resetChannelAccumulators();
         return;
      }

      if (!_probingUnconnected)
      {
         for (size_t next = _activeChannel + 1; next < _numChannels; next++)
         {
            if (_channels[next].connected)
            {
               _selectChannel(next, false);
               return;
            }
         }

         _latchPassSnapshot();
         _passesSinceProbe++;

         if (_passesSinceProbe < PROBE_PASS_INTERVAL)
         {
            for (size_t i = 0; i < _numChannels; i++)
            {
               if (_channels[i].connected)
               {
                  _selectChannel(i, false);
                  return;
               }
            }
         }
         _passesSinceProbe = 0;
      }
      else
      {
         _latchPassSnapshot();
         for (size_t i = 0; i < _numChannels; i++)
         {
            if (_channels[i].connected)
            {
               _selectChannel(i, false);
               return;
            }
         }
      }

      for (size_t step = 0; step < _numChannels; step++)
      {
         size_t candidate = (_probeCursor + step) % _numChannels;
         if (!_channels[candidate].connected)
         {
            _probeCursor = (candidate + 1) % _numChannels;
            _selectChannel(candidate, true);
            return;
         }
      }

      _selectChannel(0, false);
   }

   ///
   /// <summary>
   /// Captures a time-symmetric sample pair (V1, dummyI, I, dummyV, V2) on ADC1. ADC1 is left
   /// parked on _voltageChannel at the end of every tick so V1 requires no channel switch, each
   /// channel transition discards one dummy conversion to pre-charge the internal S&H capacitor
   /// (eliminating cross-talk from unplugged 0V mux channels), and V = 0.5 * (V1 + V2) aligns
   /// the effective voltage timestamp with I for zero phase lag.
   /// </summary>
   /// <param name="vVolts">Receives the time-centered AC voltage sensor reading in volts.</param>
   /// <param name="iVolts">Receives the muxed CT sensor reading in volts.</param>
   /// <returns>True if all conversions succeeded.</returns>
   ///
   bool _readSamplePair(float* vVolts, float* iVolts) const
   {
      int rawV1 = 0;
      int rawDummyI = 0;
      int rawI = 0;
      int rawDummyV = 0;
      int rawV2 = 0;

      if (adc_oneshot_read(_adcHandle, _voltageChannel, &rawV1) != ESP_OK ||
          adc_oneshot_read(_adcHandle, _currentChannel, &rawDummyI) != ESP_OK ||
          adc_oneshot_read(_adcHandle, _currentChannel, &rawI) != ESP_OK ||
          adc_oneshot_read(_adcHandle, _voltageChannel, &rawDummyV) != ESP_OK ||
          adc_oneshot_read(_adcHandle, _voltageChannel, &rawV2) != ESP_OK)
      {
         return false;
      }

      *vVolts = _voltsOffset + (0.5f * (rawV1 + rawV2) * _voltsPerCount);
      *iVolts = _voltsOffset + (rawI * _voltsPerCount);
      return true;
   }

   ///
   /// <summary>
   /// Integrates every voltage sample using time-weighted trapezoidal integration across exact
   /// 50,000 us (3.0000 cycles at 60 Hz) windows and smooths across a 1.2 s rolling buffer so
   /// WiFi/display timer jitter never causes displayed voltage bounce.
   /// </summary>
   /// <param name="dv">Latest voltage sample offset from nominal mid-rail in volts.</param>
   /// <param name="dtMicros">Elapsed microseconds since the previous sample.</param>
   ///
   void _updateVoltageWindow(double dv, double dtMicros)
   {
      if (!_voltHasPrev || dtMicros <= 0.0 || dtMicros > MAX_VALID_DT_MICROS)
      {
         _resetVoltageAccumulators();
         _voltPrevDV = dv;
         _voltHasPrev = true;
         return;
      }

      if ((_voltWinElapsedMicros + dtMicros) < RMS_SLICE_SPAN_MICROS)
      {
         _voltWinSumDV += 0.5 * (_voltPrevDV + dv) * dtMicros;
         _voltWinSumDV2 += 0.5 * ((_voltPrevDV * _voltPrevDV) + (dv * dv)) * dtMicros;
         _voltWinElapsedMicros += dtMicros;
         _voltPrevDV = dv;
         return;
      }

      double dt1 = RMS_SLICE_SPAN_MICROS - _voltWinElapsedMicros;
      double alpha = dt1 / dtMicros;
      double dvMid = _voltPrevDV + alpha * (dv - _voltPrevDV);
      _voltWinSumDV += 0.5 * (_voltPrevDV + dvMid) * dt1;
      _voltWinSumDV2 += 0.5 * ((_voltPrevDV * _voltPrevDV) + (dvMid * dvMid)) * dt1;

      double meanDV = _voltWinSumDV / RMS_SLICE_SPAN_MICROS;
      double varV = (_voltWinSumDV2 / RMS_SLICE_SPAN_MICROS) - (meanDV * meanDV);
      if (varV < 0.0)
      {
         varV = 0.0;
      }

      double dt2 = dtMicros - dt1;
      _voltWinSumDV = 0.5 * (dvMid + dv) * dt2;
      _voltWinSumDV2 = 0.5 * ((dvMid * dvMid) + (dv * dv)) * dt2;
      _voltWinElapsedMicros = dt2;
      _voltPrevDV = dv;

      float dcV = static_cast<float>(NOMINAL_MIDRAIL_V + meanDV);
      float rmsV = static_cast<float>(std::sqrt(varV));
      bool validBias = (dcV >= MIN_VALID_VOLTAGE_BIAS_V) && (dcV <= MAX_VALID_VOLTAGE_BIAS_V);
      bool connected = validBias && (rmsV >= MIN_VALID_VOLTAGE_RMS_V);

      _voltageDcVolts = dcV;
      if (!connected)
      {
         _voltageConnected = false;
         _voltageRmsVolts = 0.0f;
         _voltTraceCount = 0;
         _voltTraceWriteIndex = 0;
         _lineVoltageV = _nominalLineVoltageV;
         return;
      }

      _voltTraceRms[_voltTraceWriteIndex] = rmsV;
      _voltTraceWriteIndex = (_voltTraceWriteIndex + 1) % VOLTAGE_TRACE_SLICES;
      if (_voltTraceCount < VOLTAGE_TRACE_SLICES)
      {
         _voltTraceCount++;
      }

      float sumRms = 0.0f;
      for (size_t i = 0; i < _voltTraceCount; i++)
      {
         sumRms += _voltTraceRms[i];
      }

      float avgRmsV = sumRms / _voltTraceCount;
      _voltageRmsVolts = avgRmsV;
      _voltageConnected = true;
      _lineVoltageV = avgRmsV * _voltageScale;
   }

   ///
   /// <summary>
   /// Returns true if a CT channel's measured DC voltage lies inside the valid 1.65V mid-rail
   /// bias window (an unplugged channel is pulled toward 0V by the pull-down resistor on SIG).
   /// </summary>
   /// <param name="dcVolts">Mean DC voltage measured on the mux SIG pin.</param>
   /// <returns>True if a CT sensor is connected.</returns>
   ///
   static bool _isCtBiasConnected(float dcVolts)
   {
      return (dcVolts >= MIN_CONNECTED_BIAS_V) && (dcVolts <= MAX_CONNECTED_BIAS_V);
   }

   ///
   /// <summary>
   /// Lightweight esp_timer callback that wakes the dedicated sampler task without performing
   /// blocking ADC conversions inside the high-priority esp_timer task.
   /// </summary>
   /// <param name="arg">Pointer to the MuxAdcPowerSensor instance.</param>
   ///
   static void _onSampleTimerStatic(void* arg)
   {
      MuxAdcPowerSensor* self = static_cast<MuxAdcPowerSensor*>(arg);
      TaskHandle_t task = self->_samplerTask;
      if (task != nullptr)
      {
         xTaskNotifyGive(task);
      }
   }

   ///
   /// <summary>
   /// FreeRTOS task entry point for background ADC sampling.
   /// </summary>
   /// <param name="arg">Pointer to the MuxAdcPowerSensor instance.</param>
   ///
   static void _samplerTaskStatic(void* arg)
   {
      static_cast<MuxAdcPowerSensor*>(arg)->_samplerTaskLoop();
   }

   ///
   /// <summary>
   /// Waits for 1,000 Hz timer notifications (coalescing any missed ticks via pdTRUE so backlogs
   /// can never accumulate) and executes _onSampleTick() at normal application priority.
   /// </summary>
   ///
   void _samplerTaskLoop()
   {
      while (!_stopSampler)
      {
         if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50)) > 0 && !_stopSampler)
         {
            _onSampleTick();
         }
      }

      _samplerTask = nullptr;
      vTaskDelete(nullptr);
   }

   ///
   /// <summary>
   /// Reads one time-aligned (V, I) sample pair, updates the continuous AC voltage window, and
   /// integrates the active CT channel over an exact 50.000 ms (3-cycle) trapezoidal window
   /// (or probes an unconnected channel) before advancing the 74HC4067 multiplexer.
   /// </summary>
   ///
   void _onSampleTick()
   {
      float vVolts = 0.0f;
      float iVolts = 0.0f;
      if (!_readSamplePair(&vVolts, &iVolts))
      {
         return;
      }

      int64_t nowMicros = esp_timer_get_time();
      double dtMicros = _lastSampleMicros > 0 ? (nowMicros - _lastSampleMicros) : 0.0;
      _lastSampleMicros = nowMicros;

      _sampleCounter++;
      uint32_t nowMs = millis();
      if ((nowMs - _lastRateMillis) >= 1000)
      {
         _samplesPerSec = (_sampleCounter * 1000) / (nowMs - _lastRateMillis);
         _sampleCounter = 0;
         _lastRateMillis = nowMs;
      }

      double dv = vVolts - NOMINAL_MIDRAIL_V;
      double di = iVolts - NOMINAL_MIDRAIL_V;
      _updateVoltageWindow(dv, dtMicros);

      if (_settleSamplesRemaining > 0)
      {
         _settleSamplesRemaining--;
         return;
      }

      if (!_chHasPrev || dtMicros <= 0.0 || dtMicros > MAX_VALID_DT_MICROS)
      {
         _resetChannelAccumulators();
         _chPrevDV = dv;
         _chPrevDI = di;
         _chHasPrev = true;
         return;
      }

      if (_probingUnconnected)
      {
         _chSumDI += 0.5 * (_chPrevDI + di) * dtMicros;
         _chElapsedMicros += dtMicros;
         _chPrevDV = dv;
         _chPrevDI = di;

         if (_chElapsedMicros >= PROBE_SPAN_MICROS)
         {
            float dcVolts = static_cast<float>(NOMINAL_MIDRAIL_V + (_chSumDI / _chElapsedMicros));
            if (_isCtBiasConnected(dcVolts))
            {
               _channels[_activeChannel].connected = true;
               _probingUnconnected = false;
               _resetChannelAccumulators();
               _chPrevDV = dv;
               _chPrevDI = di;
               _chHasPrev = true;
               return;
            }

            _resetChannelAccumulators();
            _recordSlice(_activeChannel, dcVolts, 0.0f, false, false, true, 0.0f);
            _advanceChannel();
            if (_settleSamplesRemaining == 0)
            {
               _chPrevDV = dv;
               _chPrevDI = di;
               _chHasPrev = true;
            }
         }
         return;
      }

      if ((_chElapsedMicros + dtMicros) < RMS_SLICE_SPAN_MICROS)
      {
         _chSumDV += 0.5 * (_chPrevDV + dv) * dtMicros;
         _chSumDI += 0.5 * (_chPrevDI + di) * dtMicros;
         _chSumDI2 += 0.5 * ((_chPrevDI * _chPrevDI) + (di * di)) * dtMicros;
         _chSumDVI += 0.5 * ((_chPrevDV * _chPrevDI) + (dv * di)) * dtMicros;
         _chElapsedMicros += dtMicros;
         _chPrevDV = dv;
         _chPrevDI = di;
         return;
      }

      double dt1 = RMS_SLICE_SPAN_MICROS - _chElapsedMicros;
      double alpha = dt1 / dtMicros;
      double dvMid = _chPrevDV + alpha * (dv - _chPrevDV);
      double diMid = _chPrevDI + alpha * (di - _chPrevDI);

      _chSumDV += 0.5 * (_chPrevDV + dvMid) * dt1;
      _chSumDI += 0.5 * (_chPrevDI + diMid) * dt1;
      _chSumDI2 += 0.5 * ((_chPrevDI * _chPrevDI) + (diMid * diMid)) * dt1;
      _chSumDVI += 0.5 * ((_chPrevDV * _chPrevDI) + (dvMid * diMid)) * dt1;

      double meanDV = _chSumDV / RMS_SLICE_SPAN_MICROS;
      double meanDI = _chSumDI / RMS_SLICE_SPAN_MICROS;
      double varI = (_chSumDI2 / RMS_SLICE_SPAN_MICROS) - (meanDI * meanDI);
      if (varI < 0.0)
      {
         varI = 0.0;
      }
      double covVI = (_chSumDVI / RMS_SLICE_SPAN_MICROS) - (meanDV * meanDI);

      _resetChannelAccumulators();

      float iDcVolts = static_cast<float>(NOMINAL_MIDRAIL_V + meanDI);
      float iRmsVolts = static_cast<float>(std::sqrt(varI));
      bool connected = _isCtBiasConnected(iDcVolts);
      float ampsPerVolt = _channels[_activeChannel].ampsPerVolt;

      float signedWatts = 0.0f;
      if (connected)
      {
         if (_voltageConnected)
         {
            signedWatts = static_cast<float>(covVI) * _voltageScale * ampsPerVolt;
         }
         else
         {
            signedWatts = (iRmsVolts * ampsPerVolt) * _nominalLineVoltageV;
         }
      }

      _recordSlice(_activeChannel, iDcVolts, iRmsVolts, connected, false, true, signedWatts);
      _advanceChannel();
      if (_settleSamplesRemaining == 0)
      {
         _chPrevDV = dv;
         _chPrevDI = di;
         _chHasPrev = true;
      }
   }

   ///
   /// <summary>
   /// Releases ESP-IDF ADC calibration and oneshot unit handles if allocated.
   /// </summary>
   ///
   void _cleanupAdc()
   {
      if (_caliHandle != nullptr)
      {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
         adc_cali_delete_scheme_curve_fitting(_caliHandle);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
         adc_cali_delete_scheme_line_fitting(_caliHandle);
#endif
         _caliHandle = nullptr;
      }

      if (_adcHandle != nullptr)
      {
         adc_oneshot_del_unit(_adcHandle);
         _adcHandle = nullptr;
      }
   }

public:
   ///
   /// <summary>
   /// Creates an internal-ADC1 + 74HC4067 mux + ZMPT101B real-power sensor.
   /// </summary>
   /// <param name="pinS0">GPIO connected to 74HC4067 S0.</param>
   /// <param name="pinS1">GPIO connected to 74HC4067 S1.</param>
   /// <param name="pinS2">GPIO connected to 74HC4067 S2.</param>
   /// <param name="pinS3">GPIO connected to 74HC4067 S3.</param>
   /// <param name="pinMuxSig">ADC1 GPIO connected to 74HC4067 SIG (e.g. GPIO22).</param>
   /// <param name="pinAcVoltage">ADC1 GPIO connected to ZMPT101B OUT (e.g. GPIO21).</param>
   /// <param name="channelConfigs">Array of per-channel CT configurations (mux C0..C15).</param>
   /// <param name="numChannels">Number of active channels (1 to 16).</param>
   /// <param name="nominalLineVoltageV">Fallback AC line voltage when ZMPT101B is unplugged.</param>
   /// <param name="voltageScale">Line volts per 1.0V RMS at ZMPT101B OUT (default 1200.0).</param>
   ///
   MuxAdcPowerSensor(
      uint8_t pinS0,
      uint8_t pinS1,
      uint8_t pinS2,
      uint8_t pinS3,
      uint8_t pinMuxSig,
      uint8_t pinAcVoltage,
      const ChannelConfig* channelConfigs,
      size_t numChannels = MAX_CHANNELS,
      float nominalLineVoltageV = DEFAULT_LINE_VOLTAGE_V,
      float voltageScale = DEFAULT_VOLTAGE_SCALE)
   {
      ASSERT(channelConfigs != nullptr);
      ASSERT(numChannels >= 1 && numChannels <= MAX_CHANNELS);

      _pinSelect = { pinS0, pinS1, pinS2, pinS3 };
      _pinMuxSig = pinMuxSig;
      _pinAcVoltage = pinAcVoltage;
      _numChannels = numChannels;
      _nominalLineVoltageV = nominalLineVoltageV;
      _lineVoltageV = nominalLineVoltageV;
      _voltageScale = voltageScale;

      for (size_t i = 0; i < _numChannels; i++)
      {
         _channels[i].name = channelConfigs[i].name != nullptr ? channelConfigs[i].name : "";
         _channels[i].ampsPerVolt = channelConfigs[i].ampsPerVolt;
      }
   }

   ///
   /// <summary>
   /// Stops the background sampling timer and sampler task, then frees ADC resources.
   /// </summary>
   ///
   ~MuxAdcPowerSensor()
   {
      if (_sampleTimer != nullptr)
      {
         esp_timer_stop(_sampleTimer);
         esp_timer_delete(_sampleTimer);
         _sampleTimer = nullptr;
      }

      _stopSampler = true;
      if (_samplerTask != nullptr)
      {
         xTaskNotifyGive(_samplerTask);
         for (uint8_t wait = 0; wait < 20 && _samplerTask != nullptr; wait++)
         {
            delay(5);
         }
      }

      _cleanupAdc();
   }

   ///
   /// <summary>
   /// Configures the 74HC4067 address pins, initializes ESP32 ADC1 oneshot channels and
   /// floating-point calibration slope around mid-rail, verifies an initial conversion, and
   /// starts the 1,000 Hz background sampler task and timer.
   /// </summary>
   /// <returns>True if both ADC1 channels initialized and background sampling started.</returns>
   ///
   bool begin() override
   {
      for (uint8_t pin : _pinSelect)
      {
         pinMode(pin, OUTPUT);
      }
      _applyMuxChannel(0);
      _activeChannel = 0;
      _probingUnconnected = false;
      _settleSamplesRemaining = MUX_SETTLE_SAMPLES;
      _lastSampleMicros = 0;
      _resetChannelAccumulators();
      _resetVoltageAccumulators();

      adc_unit_t sigUnit = ADC_UNIT_1;
      adc_unit_t voltUnit = ADC_UNIT_1;
      if (adc_oneshot_io_to_channel(_pinMuxSig, &sigUnit, &_currentChannel) != ESP_OK ||
          adc_oneshot_io_to_channel(_pinAcVoltage, &voltUnit, &_voltageChannel) != ESP_OK ||
          sigUnit != voltUnit)
      {
         return false;
      }
      _adcUnit = sigUnit;

      adc_oneshot_unit_init_cfg_t unitCfg = {};
      unitCfg.unit_id = _adcUnit;
      unitCfg.ulp_mode = ADC_ULP_MODE_DISABLE;
      if (adc_oneshot_new_unit(&unitCfg, &_adcHandle) != ESP_OK)
      {
         _adcHandle = nullptr;
         return false;
      }

      adc_oneshot_chan_cfg_t chanCfg = {};
      chanCfg.atten = ADC_ATTEN_DB_12;
      chanCfg.bitwidth = ADC_BITWIDTH_12;
      if (adc_oneshot_config_channel(_adcHandle, _voltageChannel, &chanCfg) != ESP_OK ||
          adc_oneshot_config_channel(_adcHandle, _currentChannel, &chanCfg) != ESP_OK)
      {
         _cleanupAdc();
         return false;
      }

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
      adc_cali_curve_fitting_config_t caliCfg = {};
      caliCfg.unit_id = _adcUnit;
      caliCfg.atten = ADC_ATTEN_DB_12;
      caliCfg.bitwidth = ADC_BITWIDTH_12;
      if (adc_cali_create_scheme_curve_fitting(&caliCfg, &_caliHandle) != ESP_OK)
      {
         _caliHandle = nullptr;
      }
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
      adc_cali_line_fitting_config_t caliCfg = {};
      caliCfg.unit_id = _adcUnit;
      caliCfg.atten = ADC_ATTEN_DB_12;
      caliCfg.bitwidth = ADC_BITWIDTH_12;
      if (adc_cali_create_scheme_line_fitting(&caliCfg, &_caliHandle) != ESP_OK)
      {
         _caliHandle = nullptr;
      }
#endif

      _voltsPerCount = RAW_REF_VOLTS / RAW_FULL_SCALE_COUNTS;
      _voltsOffset = 0.0f;
      if (_caliHandle != nullptr)
      {
         constexpr int CAL_RAW_LOW = 1500;
         constexpr int CAL_RAW_HIGH = 2600;
         int mvLow = 0;
         int mvHigh = 0;
         if (adc_cali_raw_to_voltage(_caliHandle, CAL_RAW_LOW, &mvLow) == ESP_OK &&
             adc_cali_raw_to_voltage(_caliHandle, CAL_RAW_HIGH, &mvHigh) == ESP_OK &&
             mvLow >= 800 && mvHigh > (mvLow + 500) && mvHigh <= 2800)
         {
            _voltsPerCount = ((mvHigh - mvLow) * 0.001f) / (CAL_RAW_HIGH - CAL_RAW_LOW);
            _voltsOffset = (mvLow * 0.001f) - (CAL_RAW_LOW * _voltsPerCount);
         }
      }

      int primeV = 0;
      float testV = 0.0f;
      float testI = 0.0f;
      if (adc_oneshot_read(_adcHandle, _voltageChannel, &primeV) != ESP_OK ||
          !_readSamplePair(&testV, &testI))
      {
         _cleanupAdc();
         return false;
      }

      _lastRateMillis = millis();
      _stopSampler = false;
      if (_samplerTask == nullptr)
      {
         BaseType_t created = xTaskCreate(
            &_samplerTaskStatic,
            "MuxAdcSampler",
            SAMPLER_TASK_STACK_BYTES,
            this,
            SAMPLER_TASK_PRIORITY,
            &_samplerTask);
         if (created != pdPASS || _samplerTask == nullptr)
         {
            _samplerTask = nullptr;
            _cleanupAdc();
            return false;
         }
      }

      if (_sampleTimer == nullptr)
      {
         esp_timer_create_args_t timerArgs = {};
         timerArgs.callback = &_onSampleTimerStatic;
         timerArgs.arg = this;
         timerArgs.dispatch_method = ESP_TIMER_TASK;
         timerArgs.name = "MuxAdcPowerSensor";

         if (esp_timer_create(&timerArgs, &_sampleTimer) != ESP_OK)
         {
            _sampleTimer = nullptr;
            _stopSampler = true;
            xTaskNotifyGive(_samplerTask);
            _cleanupAdc();
            return false;
         }

         if (esp_timer_start_periodic(_sampleTimer, SAMPLE_INTERVAL_MICROS) != ESP_OK)
         {
            esp_timer_delete(_sampleTimer);
            _sampleTimer = nullptr;
            _stopSampler = true;
            xTaskNotifyGive(_samplerTask);
            _cleanupAdc();
            return false;
         }
      }

      return true;
   }

   ///
   /// <summary>
   /// Gets the measured sample-pair rate in samples per second.
   /// </summary>
   /// <returns>Measured (V, I) sample pairs per second.</returns>
   ///
   uint32_t sampleRate() const override
   {
      return _samplesPerSec;
   }

   ///
   /// <summary>
   /// Returns true because MuxAdcPowerSensor includes dedicated ZMPT101B AC voltage sensing.
   /// </summary>
   /// <returns>Always true.</returns>
   ///
   bool hasVoltageSensor() const override
   {
      return true;
   }

   ///
   /// <summary>
   /// Returns true when a valid AC voltage signal is currently present on the ZMPT101B input.
   /// </summary>
   /// <returns>True if AC line voltage is detected.</returns>
   ///
   bool voltageConnected() const override
   {
      return _voltageConnected;
   }

   ///
   /// <summary>
   /// Gets the smoothed AC RMS signal voltage (in ADC volts) measured at ZMPT101B OUT.
   /// </summary>
   /// <returns>AC RMS voltage at the ADC pin in volts.</returns>
   ///
   float voltageSensorRmsVolts() const override
   {
      return _voltageRmsVolts;
   }

   ///
   /// <summary>
   /// Gets the latest DC bias voltage (in ADC volts) measured at ZMPT101B OUT.
   /// </summary>
   /// <returns>DC bias voltage at the ADC pin in volts.</returns>
   ///
   float voltageSensorDcVolts() const override
   {
      return _voltageDcVolts;
   }

   ///
   /// <summary>
   /// Sets the ZMPT101B calibration scale in AC line volts per 1.0V RMS at the ADC pin,
   /// rescaling buffered voltage and real-power readings immediately.
   /// </summary>
   /// <param name="voltsPerAdcVolt">AC line volts per 1.0V RMS at ZMPT101B OUT.</param>
   ///
   void setVoltageScale(float voltsPerAdcVolt)
   {
      if (voltsPerAdcVolt <= 0.0f)
      {
         return;
      }

      if (_voltageScale > 0.0f && _voltageScale != voltsPerAdcVolt && _voltageConnected)
      {
         float ratio = voltsPerAdcVolt / _voltageScale;
         _lineVoltageV *= ratio;
         for (size_t i = 0; i < _numChannels; i++)
         {
            ChannelState& ch = _channels[i];
            for (size_t j = 0; j < ch.traceCount; j++)
            {
               ch.traceWatts[j] *= ratio;
            }
            ch.rawSignedWatts *= ratio;
            ch.latchedWatts *= ratio;
         }
      }

      _voltageScale = voltsPerAdcVolt;
   }

   ///
   /// <summary>
   /// Gets the current ZMPT101B calibration scale in AC line volts per 1.0V RMS at the ADC pin.
   /// </summary>
   /// <returns>AC line volts per 1.0V RMS at ZMPT101B OUT.</returns>
   ///
   float voltageScale() const
   {
      return _voltageScale;
   }
};
