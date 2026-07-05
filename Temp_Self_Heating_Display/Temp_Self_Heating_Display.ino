//
// Measures sensor self-heating by stepping through increasing sampling rates and
// recording the temperature where each rate level stops trending upward.
//
// Behavior:
// - Runs an initial 2 samples/second test to establish the baseline temperature.
// - Uses fixed target rates: 2, 5, 10, 20, 30, 50, and 100 samples/second.
// - Ends each rate test when the timed rolling average no longer increases.
// - Records that test temperature and computes delta from the 2/s baseline.
// - Stops when achieved sampling rate can no longer keep up with target.
//
#include "ArduinoBoard.h"

#ifndef ARDUINO_BUTTON_SUPPORTED
#error "This sketch requires a board with button support (e.g. Feather ESP32-S3 or Feather M0)."
#endif
#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include "TempSensor.h"
#include "SerialX.h"
#include "SerialTable.h"
#include "TimedAverage.h"
#include "Timer.h"
#include <Wire.h>

constexpr unsigned long TARGET_SAMPLE_RATES[] = { 2UL, 5UL, 10UL, 20UL, 30UL, 50UL, 100UL };
constexpr size_t TARGET_SAMPLE_RATE_COUNT = sizeof(TARGET_SAMPLE_RATES) / sizeof(TARGET_SAMPLE_RATES[0]);

constexpr float ROLLING_BUFFER_SPAN_S = 2.0f;
constexpr unsigned long ROLLING_BUFFER_SPAN_MS = static_cast<unsigned long>(ROLLING_BUFFER_SPAN_S * 1000.0f);
constexpr float NO_INCREASE_EPSILON_F = 0.0001f;
constexpr float MIN_RATE_RATIO = 0.90f;
constexpr unsigned long MAX_CASE_DURATION_MS = 30000UL;
constexpr size_t MAX_RESULTS = TARGET_SAMPLE_RATE_COUNT;

constexpr int16_t RATE_COL_X = 0;
constexpr int16_t TEMP_COL_X = 86;
constexpr int16_t DELTA_COL_X = 172;

Arduino arduino;
TempSensor sensor;
Format rateFormat("###.#", 7, Format::Alignment::RIGHT);
Format tempFormat("###.##", 7, Format::Alignment::RIGHT);
Format deltaFormat("+#.##", 7, Format::Alignment::RIGHT);

const char RESULT_TABLE_TITLE[] = "Self Heating Test Results";
const SerialTable::Column RESULT_TABLE_COLUMNS[] = {
   { "Rate(/s)", 12 },
   { "Temp(F)", 10 },
   { "Delta(F)", 10 },
};
const size_t RESULT_TABLE_COLUMN_COUNT = sizeof(RESULT_TABLE_COLUMNS) / sizeof(RESULT_TABLE_COLUMNS[0]);
SerialTable resultTable(RESULT_TABLE_TITLE, RESULT_TABLE_COLUMNS, RESULT_TABLE_COLUMN_COUNT);

int16_t nextDisplayRowY = 0;

class TestCase
{
private:
   TempSensor& _sensor;
   TimedAverage _rollingBuffer;
   Timer _sampleTimer = Timer(1UL);

   unsigned long _startMs = 0;
   unsigned long _samples = 0;
   float _previousRollingAvg = NAN;
   bool _complete = true;

   bool _hasResult = false;
   float _resultTempF = NAN;
   float _resultRate = NAN;

   void complete(float tempF, unsigned long elapsedMs)
   {
      _resultRate = (elapsedMs > 0) ? (1000.0f * static_cast<float>(_samples) / static_cast<float>(elapsedMs)) : NAN;
      _resultTempF = tempF;
      _hasResult = true;
      _complete = true;
   }

public:
   explicit TestCase(TempSensor& sensor)
      : _sensor(sensor),
      _rollingBuffer(ROLLING_BUFFER_SPAN_MS)
   {
   }

   void start(unsigned long sampleRate)
   {
      _rollingBuffer.reset();
      _previousRollingAvg = NAN;
      _samples = 0;
      _startMs = millis();
      _complete = false;
      _hasResult = false;
      _resultTempF = NAN;
      _resultRate = NAN;

      unsigned long intervalMs = (sampleRate == 0) ? 1UL : max(1UL, 1000UL / sampleRate);
      _sampleTimer = Timer(intervalMs);
   }

   void loop()
   {
      if (_complete)
      {
         return;
      }

      if (!_sampleTimer.ready())
      {
         return;
      }

      float tempF = _sensor.readTemperatureF();
      _rollingBuffer.set(tempF);
      _samples++;

      float rollingAvg = _rollingBuffer.get();
      unsigned long elapsedMs = millis() - _startMs;

      if (elapsedMs >= MAX_CASE_DURATION_MS)
      {
         complete(rollingAvg, elapsedMs);
         return;
      }

      if (elapsedMs < ROLLING_BUFFER_SPAN_MS)
      {
         return;
      }

      if (isfinite(_previousRollingAvg) && (rollingAvg <= (_previousRollingAvg + NO_INCREASE_EPSILON_F)))
      {
         complete(rollingAvg, elapsedMs);
         return;
      }

      _previousRollingAvg = rollingAvg;
   }

   bool isComplete() const { return _complete; }
   bool hasResult() const { return _hasResult; }
   void clearResult() { _hasResult = false; }
   float resultTempF() const { return _resultTempF; }
   float resultRate() const { return _resultRate; }
};

class TestRunner
{
private:
   TestCase* _testCase = nullptr;

   float _baselineTempF = NAN;
   bool _complete = true;
   size_t _currentRateIndex = 0;

   size_t _resultCount = 0;
   float _resultTemps[MAX_RESULTS] = { NAN };
   float _resultRates[MAX_RESULTS] = { NAN };
   int _pendingResultIndex = -1;

public:
   void begin(TestCase& testCase, TempSensor&)
   {
      _testCase = &testCase;
   }

   void start()
   {
      if (_testCase == nullptr)
      {
         return;
      }

      _complete = false;
      _baselineTempF = NAN;
      _currentRateIndex = 0;
      _resultCount = 0;
      _pendingResultIndex = -1;
      _testCase->start(TARGET_SAMPLE_RATES[_currentRateIndex]);
   }

   void loop()
   {
      if (_complete)
      {
         return;
      }

      _testCase->loop();
      if (!_testCase->isComplete() || !_testCase->hasResult())
      {
         return;
      }

      float achievedRate = _testCase->resultRate();
      float temp = _testCase->resultTempF();
      _testCase->clearResult();

      if (_resultCount < MAX_RESULTS)
      {
         _resultRates[_resultCount] = achievedRate;
         _resultTemps[_resultCount] = temp;

         if (_resultCount == 0)
         {
            _baselineTempF = temp;
         }

         _pendingResultIndex = static_cast<int>(_resultCount);
         _resultCount++;
      }

      unsigned long targetRate = TARGET_SAMPLE_RATES[_currentRateIndex];
      if (!isfinite(achievedRate) || (achievedRate < (static_cast<float>(targetRate) * MIN_RATE_RATIO)))
      {
         _complete = true;
         return;
      }

      if ((_currentRateIndex + 1) >= TARGET_SAMPLE_RATE_COUNT)
      {
         _complete = true;
         return;
      }

      _currentRateIndex++;
      _testCase->start(TARGET_SAMPLE_RATES[_currentRateIndex]);
   }

   bool isComplete() const { return _complete; }

   bool hasPendingResult() const
   {
      return _pendingResultIndex >= 0;
   }

   size_t pendingResultIndex() const
   {
      return static_cast<size_t>(_pendingResultIndex);
   }

   void clearPendingResult()
   {
      _pendingResultIndex = -1;
   }

   float resultRate(size_t index) const
   {
      if (index >= _resultCount)
      {
         return NAN;
      }
      return _resultRates[index];
   }

   float resultTemp(size_t index) const
   {
      if (index >= _resultCount)
      {
         return NAN;
      }
      return _resultTemps[index];
   }

   float resultDelta(size_t index) const
   {
      if (!isfinite(_baselineTempF))
      {
         return NAN;
      }

      return resultTemp(index) - _baselineTempF;
   }
};

TestCase testCase(sensor);
TestRunner testRunner;

void appendDisplayResultRow(float rate, float temp, float delta)
{
   if ((nextDisplayRowY + arduino.charH()) > arduino.height())
   {
      return;
   }

   arduino.setTextSize(2);

   arduino.setCursor(RATE_COL_X, nextDisplayRowY);
   arduino.print(rate, rateFormat, Color::VALUE);

   arduino.setCursor(TEMP_COL_X, nextDisplayRowY);
   arduino.print(temp, tempFormat, Color::VALUE2);

   arduino.setCursor(DELTA_COL_X, nextDisplayRowY);
   arduino.println(delta, deltaFormat, Color::VALUE3);

   nextDisplayRowY = arduino.getCursorY();
}

void drawRunHeader()
{
   arduino.setTextSize(3);
   arduino.clearDisplay();
   arduino.println("Self Heating Test", Color::HEADING);
   arduino.setTextSize(2);

   int16_t headerY = arduino.getCursorY();
   arduino.setCursor(RATE_COL_X, headerY);
   arduino.print("   Rate", Color::LABEL);
   arduino.setCursor(TEMP_COL_X, headerY);
   arduino.print("   Temp", Color::LABEL);
   arduino.setCursor(DELTA_COL_X, headerY);
   arduino.print("  Delta", Color::LABEL);
   arduino.println();

   nextDisplayRowY = arduino.getCursorY();
}

void finalizeDisplayRun()
{
   if ((nextDisplayRowY + arduino.charH()) <= arduino.height())
   {
      arduino.setTextSize(2);
      arduino.setCursor(0, nextDisplayRowY);
      arduino.println("Done", Color::HEADING);
      nextDisplayRowY = arduino.getCursorY();
   }

   if ((nextDisplayRowY + arduino.charH()) <= arduino.height())
   {
      arduino.setCursor(0, nextDisplayRowY);
      arduino.println("Press A to rerun", Color::LABEL);
      nextDisplayRowY = arduino.getCursorY();
   }
}

void startTestRun()
{
   resultTable.printHeader();

   testRunner.start();
   drawRunHeader();
}

void setup()
{
   SerialX::begin();
   Wire.begin();

   arduino.begin();

   if (!sensor.begin())
   {
      arduino.setTextSize(3);
      arduino.clearDisplay();
      arduino.println("Sensor FAILED", Color::RED);
      Serial.println("Sensor initialization failed");
      return;
   }

   testRunner.begin(testCase, sensor);
   startTestRun();
}

void loop()
{
   if (testRunner.isComplete())
   {
      if (arduino.buttonA.wasPressed())
      {
         startTestRun();
      }
      return;
   }

   testRunner.loop();

   if (testRunner.hasPendingResult())
   {
      size_t index = testRunner.pendingResultIndex();
      float rate = testRunner.resultRate(index);
      float temp = testRunner.resultTemp(index);
      float delta = testRunner.resultDelta(index);

      resultTable.printRow(
         SerialTable::fixed(rate, 2),
         SerialTable::fixed(temp, 2),
         SerialTable::fixed(delta, 2));
      appendDisplayResultRow(rate, temp, delta);
      testRunner.clearPendingResult();

      if (testRunner.isComplete())
      {
         finalizeDisplayRun();
         Serial.println("Self Heating Test complete");
      }
   }
}
