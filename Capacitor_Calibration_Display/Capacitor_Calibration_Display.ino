// -------------------------------------------------------------------------------------------------
// Capacitor_Calibration_Display
//
// This sketch tunes capacitor sensor timing settings and visualizes stability/rate behavior on-device.
// It supports two primary modes:
//
// 1) Normal monitoring mode
//    - Continuously updates rolling capacitor statistics.
//    - Displays discharge delay, buffer size, average charge time, variation, raw rate, effective rate,
//      and estimated possible level error (inches).
//
// 2) Delay sweep calibration mode (Button A)
//    - Runs a discharge-delay sweep for multiple target effective output rates.
//    - For each sweep point, first estimates raw sensor rate, chooses a buffer size to match target
//      effective rate, then measures stability for TEST_DURATION_MS.
//    - Prints a serial table with each test result and restores defaults when complete.
//
// Startup behavior:
// - Performs a short submerged-delta calibration period to estimate full-range charge delta used by the
//   possible-error calculation.
// -------------------------------------------------------------------------------------------------
#include <Arduino.h>
#include "CapacitorCalibrationSensor.h"
#include "Feather.h"
#include "RollingStats.h"
#include "SerialX.h"
#include "Timer.h"

Feather feather;
Format chargeFormat("####.# us");
Format effectiveRateFormat("###/s");
Format rawRateFormat("######/s");
Format rangeFormat("##.# us");
Format errorFormat("#.## in");

// Milliseconds between display refreshes.
constexpr size_t DISPLAY_INTERVAL_MS = 100;

// Values used for the primary display
constexpr size_t DEFAULT_ROLLING_BUFFER_SIZE = 300;
constexpr uint16_t DEFAULT_DISCHARGE_DELAY_MICROS = 200;

//
// Test Sweep Parameters
// 
// Duration of each measured test phase before writing a result row.
constexpr unsigned long TEST_DURATION_MS = 5000;
// Target effective output rates in Hz (raw rate / buffer size).
constexpr float TARGET_EFFECTIVE_RATES[] = { 10.0f, 20.0f, 30.0f };
constexpr size_t TARGET_EFFECTIVE_RATE_COUNT = sizeof(TARGET_EFFECTIVE_RATES) / sizeof(TARGET_EFFECTIVE_RATES[0]);
// First discharge delay value in the sweep.
constexpr uint16_t TEST_DISCHARGE_DELAY_MIN_MICROS = 100;
// Final discharge delay value in the sweep.
constexpr uint16_t TEST_DISCHARGE_DELAY_MAX_MICROS = 1000;
// Starting step size for the discharge delay sweep.
constexpr uint16_t TEST_DISCHARGE_DELAY_STEP_MICROS = 10;
// Multiplier used to gradually increase the sweep step size.
constexpr float TEST_DISCHARGE_DELAY_STEP_GROWTH = 1.5f;

// Samples collected in the estimation phase before choosing buffer size.
constexpr size_t RAW_RATE_SAMPLE_COUNT = 100;
// Startup calibration duration to estimate submerged delta charge time.
constexpr unsigned long SUBMERGED_DELTA_CALIBRATION_DURATION_MS = 1000;
// Length of the sensor in inches.
constexpr float SENSOR_LENGTH_INCHES = 60.0f;
// Minimum allowed computed buffer size.
constexpr size_t MIN_TARGET_BUFFER_SIZE = 1;
// Maximum allowed computed buffer size.
constexpr size_t MAX_TARGET_BUFFER_SIZE = 500;

Timer displayTimer(DISPLAY_INTERVAL_MS);

// Hardware pin assignments.
constexpr uint8_t SENSE_PIN = 5;
constexpr uint8_t CHARGE_PIN_1M = 6;
constexpr uint8_t CHARGE_PIN_470K = 9;
constexpr uint8_t CHARGE_PIN_100K = 10;
constexpr uint8_t CHARGE_PIN_47K = 11;


constexpr uint8_t CHARGE_PIN = CHARGE_PIN_1M;

class CalibrationSweepSensor
{
private:
   CapacitorCalibrationSensor _sensor;
   RollingStats _stats;
   RollingStats _averageStats;

public:
   CalibrationSweepSensor(
      uint8_t chargePin,
      uint8_t sensePin,
      size_t rollingBufferSize,
      uint16_t dischargeDelayMicros)
      : _sensor(chargePin, sensePin, dischargeDelayMicros),
      _stats(rollingBufferSize),
      _averageStats(rollingBufferSize)
   {
   }

   void begin()
   {
      _sensor.begin();
   }

   void loop()
   {
      _sensor.loop();

      float chargeTime = 0;
      while (_sensor.tryDequeue(chargeTime))
      {
         _stats.set(chargeTime);

         float avg = _stats.average();
         if (isfinite(avg))
         {
            _averageStats.set(avg);
         }
      }
   }

   void setDischargeDelayMicros(uint16_t dischargeDelayMicros)
   {
      _sensor.setDischargeDelayMicros(dischargeDelayMicros);
   }

   uint16_t dischargeDelayMicros() const
   {
      return _sensor.dischargeDelayMicros();
   }

   float average() const
   {
      if (_stats.count() < _stats.size())
      {
         return NAN;
      }

      return _stats.average();
   }

   float averageRange() const
   {
      if (_stats.count() < _stats.size())
      {
         return NAN;
      }

      return _averageStats.range();
   }

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

   size_t count() const
   {
      return _stats.count();
   }

   size_t bufferSize() const
   {
      return _stats.size();
   }

   float rate()
   {
      return _sensor.rate();
   }
};

CalibrationSweepSensor calibrationSensor(CHARGE_PIN, SENSE_PIN, DEFAULT_ROLLING_BUFFER_SIZE, DEFAULT_DISCHARGE_DELAY_MICROS);
float submergedDeltaChargeTimeMicros = NAN;

float calculatePossibleError(float chargeVariationMicros)
{
   if (!isfinite(chargeVariationMicros) || !isfinite(submergedDeltaChargeTimeMicros) || submergedDeltaChargeTimeMicros <= 0.0f)
   {
      return NAN;
   }

   return (chargeVariationMicros / submergedDeltaChargeTimeMicros) * SENSOR_LENGTH_INCHES;
}

size_t calculateDelayTestCount()
{
   size_t total = 1;
   uint16_t delayMicros = TEST_DISCHARGE_DELAY_MIN_MICROS;
   uint16_t stepMicros = TEST_DISCHARGE_DELAY_STEP_MICROS;

   while (delayMicros < TEST_DISCHARGE_DELAY_MAX_MICROS)
   {
      uint16_t nextStepMicros = (uint16_t)(stepMicros * TEST_DISCHARGE_DELAY_STEP_GROWTH + 0.5f);
      if (nextStepMicros <= stepMicros)
      {
         nextStepMicros = stepMicros + 1;
      }

      stepMicros = nextStepMicros;
      uint32_t nextDelayMicros = delayMicros + stepMicros;
      delayMicros = (nextDelayMicros > TEST_DISCHARGE_DELAY_MAX_MICROS) ? TEST_DISCHARGE_DELAY_MAX_MICROS : (uint16_t)nextDelayMicros;
      total++;
   }

   return total;
}

const size_t DELAY_TEST_COUNT = calculateDelayTestCount();
const size_t TOTAL_TESTS = DELAY_TEST_COUNT * TARGET_EFFECTIVE_RATE_COUNT;

enum class TestPhase
{
   CollectingRawRate,
   MeasuringEffectiveRate
};

class DelaySweepTestLoop
{
private:
   CalibrationSweepSensor& _sensor;
   bool _running = false;
   TestPhase _phase = TestPhase::CollectingRawRate;
   size_t _currentBufferSize = DEFAULT_ROLLING_BUFFER_SIZE;
   uint16_t _currentDischargeDelayMicros = TEST_DISCHARGE_DELAY_MIN_MICROS;
   uint16_t _currentStepMicros = TEST_DISCHARGE_DELAY_STEP_MICROS;
   unsigned long _currentTestStartMs = 0;
   size_t _currentTestNumber = 0;
   size_t _currentTargetRateIndex = 0;
   float _currentTargetEffectiveRate = TARGET_EFFECTIVE_RATES[0];
   float _estimatedRawRate = NAN;
   float _measuredAverageMin = NAN;
   float _measuredAverageMax = NAN;

   size_t _calculateTargetBufferSize(float rawRate)
   {
      if (!isfinite(rawRate) || rawRate <= 0.0f)
      {
         return DEFAULT_ROLLING_BUFFER_SIZE;
      }

      float targetBufferSize = rawRate / _currentTargetEffectiveRate;
      if (targetBufferSize < MIN_TARGET_BUFFER_SIZE)
      {
         targetBufferSize = MIN_TARGET_BUFFER_SIZE;
      }

      if (targetBufferSize > MAX_TARGET_BUFFER_SIZE)
      {
         targetBufferSize = MAX_TARGET_BUFFER_SIZE;
      }

      return (size_t)(targetBufferSize + 0.5f);
   }

   void _beginDelayTestIteration()
   {
      _phase = TestPhase::CollectingRawRate;
      _currentBufferSize = RAW_RATE_SAMPLE_COUNT;
      _measuredAverageMin = NAN;
      _measuredAverageMax = NAN;
      _sensor.reset(_currentDischargeDelayMicros, RAW_RATE_SAMPLE_COUNT);
      _currentTestStartMs = millis();
   }

   void _setupNextDelayTest()
   {
      uint16_t nextStepMicros = (uint16_t)(_currentStepMicros * TEST_DISCHARGE_DELAY_STEP_GROWTH + 0.5f);
      if (nextStepMicros <= _currentStepMicros)
      {
         nextStepMicros = _currentStepMicros + 1;
      }

      _currentStepMicros = nextStepMicros;

      uint32_t nextDelayMicros = _currentDischargeDelayMicros + _currentStepMicros;
      _currentDischargeDelayMicros = (nextDelayMicros > TEST_DISCHARGE_DELAY_MAX_MICROS) ? TEST_DISCHARGE_DELAY_MAX_MICROS : (uint16_t)nextDelayMicros;

      _currentTestNumber++;
      _beginDelayTestIteration();
   }

   void _setupNextTargetRate()
   {
      _currentTargetRateIndex++;
      _currentTargetEffectiveRate = TARGET_EFFECTIVE_RATES[_currentTargetRateIndex];
      _currentDischargeDelayMicros = TEST_DISCHARGE_DELAY_MIN_MICROS;
      _currentStepMicros = TEST_DISCHARGE_DELAY_STEP_MICROS;
      _currentTestNumber++;
      _beginDelayTestIteration();
   }

   void _completeCurrentDelayTest()
   {
      float rawRate = _sensor.rate();
      float effectiveRate = (_sensor.bufferSize() > 0) ? (rawRate / _sensor.bufferSize()) : 0;

      float measuredAverageRange = NAN;
      if (isfinite(_measuredAverageMin) && isfinite(_measuredAverageMax))
      {
         measuredAverageRange = _measuredAverageMax - _measuredAverageMin;
      }

      float possibleError = calculatePossibleError(measuredAverageRange);

      float avgChargeTime = _sensor.average();

      SerialX::print(String((int)round(_currentTargetEffectiveRate)) + " hz", 10);
      SerialX::print(String((unsigned long)_sensor.dischargeDelayMicros()) + " us", 12);
      if (isfinite(avgChargeTime))
      {
         SerialX::print(String(avgChargeTime, 1) + " us", 12);
      }
      else
      {
         SerialX::print("----", 12);
      }
      if (isfinite(measuredAverageRange))
      {
         SerialX::print(String(measuredAverageRange, 2) + " us", 12);
      }
      else
      {
         SerialX::print("----", 12);
      }
      SerialX::print((unsigned long)_sensor.bufferSize(), 8);
      SerialX::print(String((int)round(_estimatedRawRate)) + "/s", 10);
      SerialX::print(String((int)round(rawRate)) + "/s", 10);
      SerialX::print(String((int)round(effectiveRate)) + "/s", 11);
      if (isfinite(possibleError))
      {
         SerialX::println(String(possibleError, 2) + " in", 10);
      }
      else
      {
         SerialX::println("----", 10);
      }

      if (_currentDischargeDelayMicros < TEST_DISCHARGE_DELAY_MAX_MICROS)
      {
         _setupNextDelayTest();
      }
      else if (_currentTargetRateIndex + 1 < TARGET_EFFECTIVE_RATE_COUNT)
      {
         _setupNextTargetRate();
      }
      else
      {
         _running = false;
         _sensor.reset(DEFAULT_DISCHARGE_DELAY_MICROS, DEFAULT_ROLLING_BUFFER_SIZE);
         Serial.println("Testing Complete");

         feather.clearDisplay();
      }
   }

public:
   explicit DelaySweepTestLoop(CalibrationSweepSensor& sensor)
      : _sensor(sensor)
   {}

   void start()
   {
      _running = true;
      _currentTestNumber = 1;
      _currentTargetRateIndex = 0;
      _currentTargetEffectiveRate = TARGET_EFFECTIVE_RATES[_currentTargetRateIndex];
      _currentDischargeDelayMicros = TEST_DISCHARGE_DELAY_MIN_MICROS;
      _currentStepMicros = TEST_DISCHARGE_DELAY_STEP_MICROS;

      Serial.println();
      Serial.println("Testing... Target Effective Rates: 10, 20, 30 hz");
      Serial.println();

      SerialX::print("Target", 10);
      SerialX::print("Discharge", 12);
      SerialX::print("Avg", 12);
      SerialX::print("Variation", 12);
      SerialX::print("Buffer", 8);
      SerialX::print("Est Raw", 10);
      SerialX::print("Raw", 10);
      SerialX::print("Effective", 11);
      SerialX::println("Possible", 10);

      SerialX::print("Rate", 10);
      SerialX::print("Delay", 12);
      SerialX::print("Charge", 12);
      SerialX::print("Charge", 12);
      SerialX::print("Size", 8);
      SerialX::print("Rate", 10);
      SerialX::print("Rate", 10);
      SerialX::print("Rate", 11);
      SerialX::println("Error", 10);

      SerialX::print("--------", 10);
      SerialX::print("----------", 12);
      SerialX::print("----------", 12);
      SerialX::print("----------", 12);
      SerialX::print("------", 8);
      SerialX::print("--------", 10);
      SerialX::print("--------", 10);
      SerialX::print("---------", 11);
      SerialX::println("--------", 10);

      _beginDelayTestIteration();
      feather.clearDisplay();
   }

   void update()
   {
      if (!_running)
      {
         return;
      }

      if (_phase == TestPhase::CollectingRawRate)
      {
         if (_sensor.count() >= RAW_RATE_SAMPLE_COUNT)
         {
            _estimatedRawRate = _sensor.rate();
            _currentBufferSize = _calculateTargetBufferSize(_estimatedRawRate);
            _sensor.reset(_currentDischargeDelayMicros, _currentBufferSize);
            _phase = TestPhase::MeasuringEffectiveRate;
            _measuredAverageMin = NAN;
            _measuredAverageMax = NAN;
            _currentTestStartMs = millis();
         }
      }
      else
      {
         float average = _sensor.average();
         if (isfinite(average))
         {
            if (!isfinite(_measuredAverageMin) || average < _measuredAverageMin)
            {
               _measuredAverageMin = average;
            }

            if (!isfinite(_measuredAverageMax) || average > _measuredAverageMax)
            {
               _measuredAverageMax = average;
            }
         }

         if (millis() - _currentTestStartMs >= TEST_DURATION_MS)
         {
            _completeCurrentDelayTest();
         }
      }
   }

   bool running() const
   {
      return _running;
   }

   size_t currentTestNumber() const
   {
      return _currentTestNumber;
   }
};

DelaySweepTestLoop delaySweepTestLoop(calibrationSensor);

void runSubmergedDeltaCalibration()
{
   calibrationSensor.reset(DEFAULT_DISCHARGE_DELAY_MICROS, DEFAULT_ROLLING_BUFFER_SIZE);

   Timer calibrationTimer(SUBMERGED_DELTA_CALIBRATION_DURATION_MS);
   while (!calibrationTimer.ready())
   {
      calibrationSensor.loop();
   }

   float measuredChargeTime = calibrationSensor.average();
   if (isfinite(measuredChargeTime) && measuredChargeTime > 0.0f)
   {
      submergedDeltaChargeTimeMicros = measuredChargeTime;
   }
}

void setup()
{
   SerialX::begin();
   feather.begin();
   calibrationSensor.begin();
   runSubmergedDeltaCalibration();
}

void loop()
{
   if (feather.buttonA.wasPressed() && !delaySweepTestLoop.running())
   {
      delaySweepTestLoop.start();
   }

   calibrationSensor.loop();
   delaySweepTestLoop.update();

   if (displayTimer.ready())
   {
      feather.setCursor(0, 0);
      feather.setTextSize(2);
      if (delaySweepTestLoop.running())
      {
         feather.print("Testing ", Color::HEADING);
         feather.print(delaySweepTestLoop.currentTestNumber(), Color::HEADING2);
         feather.print("/", Color::HEADING2);
         feather.println(TOTAL_TESTS, Color::HEADING2);
      }
      else
      {
         feather.println("Capacitor Sensor Tuner", Color::HEADING);
      }

      feather.setTextSize(2);

      float effectiveRate = (calibrationSensor.bufferSize() > 0) ? (calibrationSensor.rate() / calibrationSensor.bufferSize()) : 0;
      float avgRange = calibrationSensor.averageRange();

      feather.println(" Discharge Time: ", calibrationSensor.dischargeDelayMicros(), chargeFormat, Color::VALUE2);
      feather.println("    Buffer Size: ", calibrationSensor.bufferSize(), Color::VALUE2);

      if (!delaySweepTestLoop.running())
      {
         feather.println("Avg Charge Time: ", calibrationSensor.average(), chargeFormat);
         feather.println("      Variation: ", calibrationSensor.averageRange(), rangeFormat);
         feather.println("       Raw Rate: ", calibrationSensor.rate(), rawRateFormat);
      }

      feather.println(" Effective Rate: ", effectiveRate, effectiveRateFormat, Color::VALUE3);

      feather.print(" Possible Error: ", Color::LABEL);
      float possibleError = calculatePossibleError(avgRange);
      if (isfinite(possibleError))
      {
         feather.print(possibleError, errorFormat, Color::VALUE3);
      }
      else
      {
         feather.print("----", Color::GRAY);
      }
      feather.println();
   }
}
