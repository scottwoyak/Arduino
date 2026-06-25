#include <Arduino.h>
#include "CapacitorDepthSensor.h"
#include "Feather.h"
#include "SerialX.h"
#include "Timer.h"

Feather feather;
Format chargeFormat("###.# us");
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
constexpr unsigned long TEST_DURATION_MS = 2000;
// Target effective output rate in Hz (raw rate / buffer size).
constexpr float TARGET_EFFECTIVE_RATE = 30.0f;
// First discharge delay value in the sweep.
constexpr uint16_t TEST_DISCHARGE_DELAY_MIN_MICROS = 10;
// Final discharge delay value in the sweep.
constexpr uint16_t TEST_DISCHARGE_DELAY_MAX_MICROS = 500;
// Starting step size for the discharge delay sweep.
constexpr uint16_t TEST_DISCHARGE_DELAY_STEP_MICROS = 10;
// Multiplier used to gradually increase the sweep step size.
constexpr float TEST_DISCHARGE_DELAY_STEP_GROWTH = 1.1f;

// Samples collected in the estimation phase before choosing buffer size.
constexpr size_t RAW_RATE_SAMPLE_COUNT = 100;
// Minimum allowed computed buffer size.
constexpr size_t MIN_TARGET_BUFFER_SIZE = 1;
// Maximum allowed computed buffer size.
constexpr size_t MAX_TARGET_BUFFER_SIZE = 500;

Timer displayTimer(DISPLAY_INTERVAL_MS);

// Hardware Pin Assignments
const int CHARGE_PIN = 5;
const int SENSE_PIN = 6;

RollingCapacitiveSensor sensor(CHARGE_PIN, SENSE_PIN, DEFAULT_ROLLING_BUFFER_SIZE, DEFAULT_DISCHARGE_DELAY_MICROS);

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
const size_t TOTAL_TESTS = DELAY_TEST_COUNT;

enum class TestPhase
{
   CollectingRawRate,
   MeasuringEffectiveRate
};

class DelaySweepTestLoop
{
private:
   RollingCapacitiveSensor& _sensor;
   bool _running = false;
   TestPhase _phase = TestPhase::CollectingRawRate;
   size_t _currentBufferSize = DEFAULT_ROLLING_BUFFER_SIZE;
   uint16_t _currentDischargeDelayMicros = TEST_DISCHARGE_DELAY_MIN_MICROS;
   uint16_t _currentStepMicros = TEST_DISCHARGE_DELAY_STEP_MICROS;
   unsigned long _currentTestStartMs = 0;
   size_t _currentTestNumber = 0;
   float _estimatedRawRate = NAN;

   size_t _calculateTargetBufferSize(float rawRate)
   {
      if (!isfinite(rawRate) || rawRate <= 0.0f)
      {
         return DEFAULT_ROLLING_BUFFER_SIZE;
      }

      float targetBufferSize = rawRate / TARGET_EFFECTIVE_RATE;
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

   void _completeCurrentDelayTest()
   {
      float rawRate = _sensor.rate();
      float effectiveRate = (_sensor.bufferSize() > 0) ? (rawRate / _sensor.bufferSize()) : 0;
      float possibleError = _sensor.averageRange() / 65.0f * 60.0f;

      SerialX::print(String((unsigned long)_sensor.dischargeDelayMicros()) + " us", 12);
      SerialX::print((unsigned long)_sensor.bufferSize(), 8);
      SerialX::print(String((int)round(_estimatedRawRate)) + "/s", 10);
      SerialX::print(String((int)round(rawRate)) + "/s", 10);
      SerialX::print(String((int)round(effectiveRate)) + "/s", 11);
      SerialX::println(String(possibleError, 2) + " in", 10);

      if (_currentDischargeDelayMicros < TEST_DISCHARGE_DELAY_MAX_MICROS)
      {
         _setupNextDelayTest();
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
   explicit DelaySweepTestLoop(RollingCapacitiveSensor& sensor)
      : _sensor(sensor)
   {}

   void start()
   {
      _running = true;
      _currentTestNumber = 1;
      _currentDischargeDelayMicros = TEST_DISCHARGE_DELAY_MIN_MICROS;
      _currentStepMicros = TEST_DISCHARGE_DELAY_STEP_MICROS;

      Serial.println();
      Serial.print("Testing... ");
      Serial.print("Target Effective Rate: ");
      Serial.println(TARGET_EFFECTIVE_RATE, 0);
      Serial.println();

      SerialX::print("Discharge", 12);
      SerialX::print("Buffer", 8);
      SerialX::print("Est Raw", 10);
      SerialX::print("Raw", 10);
      SerialX::print("Effective", 11);
      SerialX::println("Possible", 10);

      SerialX::print("Delay", 12);
      SerialX::print("Size", 8);
      SerialX::print("Rate", 10);
      SerialX::print("Rate", 10);
      SerialX::print("Rate", 11);
      SerialX::println("Error", 10);

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
            _currentTestStartMs = millis();
         }
      }
      else if (millis() - _currentTestStartMs >= TEST_DURATION_MS)
      {
         _completeCurrentDelayTest();
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

DelaySweepTestLoop delaySweepTestLoop(sensor);

void setup()
{
   SerialX::begin();
   feather.begin();
   sensor.begin();
}

void loop()
{
   if (feather.buttonA.wasPressed() && !delaySweepTestLoop.running())
   {
      delaySweepTestLoop.start();
   }

   sensor.loop();
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

      feather.print(" Discharge Time: ", Color::LABEL);
      feather.print(sensor.dischargeDelayMicros(), chargeFormat, Color::VALUE2);
      feather.println();

      feather.print("    Buffer Size: ", Color::LABEL);
      feather.print(sensor.bufferSize(), Color::VALUE2);
      feather.println();

      feather.print("Avg Charge Time: ", Color::LABEL);
      feather.print(sensor.average(), chargeFormat, Color::VALUE);
      feather.println();

      feather.print("      Variation: ", Color::LABEL);
      feather.print(sensor.averageRange(), rangeFormat, Color::VALUE);
      feather.println();

      feather.print("       Raw Rate: ", Color::LABEL);
      feather.print(sensor.rate(), rawRateFormat, Color::VALUE);
      feather.println();

      float effectiveRate = (sensor.bufferSize() > 0) ? (sensor.rate() / sensor.bufferSize()) : 0;
      feather.print(" Effective Rate: ", Color::LABEL);
      feather.print(effectiveRate, effectiveRateFormat, Color::VALUE3);
      feather.println();

      // if we assume the charge time doubles when the capacitor is fully submerged,
      // then we can estimate errors as follows:
      constexpr auto DELTA_CHARGE_TIME_MICROS = 65.0f; // empirically observed difference in charge time between empty and full   
      constexpr auto SENSOR_LENGTH = 60.0f; // length of the sensor in inches
      feather.print(" Possible Error: ", Color::LABEL);
      float avgRange = sensor.averageRange();
      if (isfinite(avgRange))
      {
         float possibleError = (avgRange / DELTA_CHARGE_TIME_MICROS) * SENSOR_LENGTH;
         feather.print(possibleError, errorFormat, Color::VALUE3);
      }
      else
      {
         feather.print("----", Color::GRAY);
      }
      feather.println();
   }
}
