//
// Measures sensor self-heating by stepping through decreasing sampling rates and
// recording how temperature trends over a fixed duration at each rate.
//
// Behavior:
// - Runs the fastest rate first (100 samples/second) to establish how long a test
//   takes; every subsequent rate runs for that same fixed duration.
// - Uses fixed target rates, fastest to slowest: 100, 50, 30, 20, 10, and 2 samples/second.
// - Cools down between tests by sampling once per second until the temperature returns
//   within 0.01 F of the temperature recorded when the just-finished test started.
// - Records the peak (steady-state) temperature reached during each test, the number
//   of samples needed to reach it, and the delta from the first (100/s) test.
//
// Display:
// - While a rate test is collecting samples, shows "Collecting data at N/s" (the
//   actual measured rate); during the cooldown period it shows "Cooling down...".
// - A single scatter plot accumulates live, with each rate's samples plotted in
//   its own color against elapsed time, and axes that automatically scale.
// - Once every rate has been tested, shows the summary results table (rate, peak
//   temperature, delta, and samples to steady state). Button A cycles between the
//   summary table, a raw-sample scatter plot, and a rolling-average scatter plot.
//
#include "ArduinoBoard.h"

#ifndef ARDUINO_BUTTON_SUPPORTED
#error "This sketch requires a board with button support (e.g. Feather ESP32-S3 or Feather M0)."
#endif
#ifndef ARDUINO_DISPLAY_SUPPORTED
#error "This sketch requires a board with a display (e.g. Feather ESP32-S3 or Feather M0)."
#endif

#include <new>
#include <Wire.h>

#include "SerialTable.h"
#include "SerialX.h"
#include "ScatterPlot.h"
#include "RollingRate.h"
#include "TempSensor.h"
#include "TimedAverage.h"
#include "Timer.h"

///
/// <summary>
/// Which post-run view is currently displayed: the summary results table, a raw-sample
/// scatter plot, or a rolling-average scatter plot.
/// </summary>
///
enum class ResultView : uint8_t { Summary, RawScatter, RollingAvgScatter };

///
/// <summary>
/// Advances to the next post-run view: summary table, raw-sample scatter plot,
/// rolling-average scatter plot, and back to the summary table.
/// </summary>
/// <param name="view">View to advance.</param>
/// <returns>Reference to the updated view.</returns>
///
inline ResultView& operator++(ResultView& view)
{
   switch (view)
   {
   case ResultView::Summary:
      view = ResultView::RawScatter;
      break;

   case ResultView::RawScatter:
      view = ResultView::RollingAvgScatter;
      break;

   default:
      view = ResultView::Summary;
      break;
   }
   return view;
}

///
/// <summary>
/// Advances to the next post-run view and returns the view's prior value.
/// </summary>
/// <param name="view">View to advance.</param>
/// <returns>The view's value before advancing.</returns>
///
inline ResultView operator++(ResultView& view, int)
{
   ResultView previous = view;
   ++view;
   return previous;
}

// ----------- Target Sample Rates
constexpr unsigned long TARGET_SAMPLE_RATES[] = { 100UL, 50UL, 30UL, 20UL, 10UL, 2UL };
constexpr size_t NUM_TARGET_SAMPLES = sizeof(TARGET_SAMPLE_RATES) / sizeof(TARGET_SAMPLE_RATES[0]);

///
/// <summary>
/// Finds the largest value in the target sample rate array at compile time.
/// </summary>
/// <returns>The maximum target sample rate.</returns>
///
constexpr unsigned long _maxTargetSampleRate()
{
   unsigned long maxRate = TARGET_SAMPLE_RATES[0];
   for (size_t i = 1; i < NUM_TARGET_SAMPLES; i++)
   {
      if (TARGET_SAMPLE_RATES[i] > maxRate)
      {
         maxRate = TARGET_SAMPLE_RATES[i];
      }
   }
   return maxRate;
}
constexpr unsigned long MAX_TARGET_SAMPLE_RATE = _maxTargetSampleRate();

// ----------- Test Completion
constexpr float ROLLING_AVERAGE_SPAN_S = 2.0f;
constexpr float NO_INCREASE_EPSILON_F = 0.001f;
constexpr unsigned long MAX_CASE_DURATION_MS = 30000UL;
constexpr unsigned long PLATEAU_CHECK_INTERVAL_MS = 500UL;
constexpr int PLATEAU_STABLE_CHECK_COUNT = 3;
constexpr unsigned long COOLDOWN_SAMPLE_INTERVAL_MS = 1000UL;
constexpr float COOLDOWN_CONVERGENCE_EPSILON_F = 0.01f;
constexpr size_t MAX_RESULTS = NUM_TARGET_SAMPLES;
constexpr size_t RAW_SAMPLE_MARGIN = 200; // headroom added to the theoretical max sample count
constexpr size_t MAX_RAW_SAMPLES_PER_TEST = static_cast<size_t>(MAX_TARGET_SAMPLE_RATE * (MAX_CASE_DURATION_MS / 1000UL)) + RAW_SAMPLE_MARGIN;

// ----------- Display Geometry
constexpr uint16_t DISPLAY_WIDTH = 240;
constexpr uint16_t DISPLAY_HEIGHT = 135;
constexpr uint16_t COLLECTING_HEADER_HEIGHT = 3 * 8 + 2 * 8 + 4; // title (size 3) + status line (size 2) plus padding

// ----------- Scatter Plot
constexpr uint16_t DISPLAY_UPDATE_INTERVAL_MS = 150;
constexpr Color SERIES_COLORS[] = {
   Color::GREEN,
   Color::YELLOW,
   Color::CYAN,
   Color::MAGENTA,
   Color::ORANGE,
   Color::RED,
};
constexpr size_t NUM_SERIES_COLORS = sizeof(SERIES_COLORS) / sizeof(SERIES_COLORS[0]);

// ----------- The Board
Arduino arduino;
TempSensor sensor;

// ----------- Display Formats
Format rateFormat("###", 4, Format::Alignment::RIGHT);
Format tempFormat("###.##", 6, Format::Alignment::RIGHT);
Format deltaFormat("+#.##", 6, Format::Alignment::RIGHT);
Format samplesFormat("####", Format::Alignment::RIGHT);
Format collectingRateFormat("###/s", Format::Alignment::RIGHT);
Format scatterAxisFormat("###.#", Format::Alignment::LEFT);
Format scatterDurationFormat("##.#s", Format::Alignment::LEFT);

// ----------- Summary Results Table
constexpr char RESULT_TABLE_TITLE[] = "Warm-Up Test Results";
constexpr SerialTable::Column RESULT_TABLE_COLUMNS[] = {
   { "Rate(/s)", 12 },
   { "Temp(F)", 10 },
   { "Delta(F)", 10 },
   { "Samples", 10 },
};
constexpr size_t NUM_RESULT_TABLE_COLUMNS = sizeof(RESULT_TABLE_COLUMNS) / sizeof(RESULT_TABLE_COLUMNS[0]);
SerialTable resultTable(RESULT_TABLE_TITLE, RESULT_TABLE_COLUMNS, NUM_RESULT_TABLE_COLUMNS);

// ----------- Scatter Plot State
ScatterPlot scatterPlot(&arduino, 0, COLLECTING_HEADER_HEIGHT, DISPLAY_WIDTH, DISPLAY_HEIGHT - COLLECTING_HEADER_HEIGHT);
Timer displayUpdateTimer(DISPLAY_UPDATE_INTERVAL_MS);
int16_t collectingRateRowY = 0;
constexpr size_t NO_RATE_INDEX = static_cast<size_t>(-1);
float lastPlottedYMin = NAN;
float lastPlottedYMax = NAN;
unsigned long lastPlottedXMaxMs = 0;
size_t lastPlottedCurrentSampleCount = 0;
size_t lastPlottedRateIndex = NO_RATE_INDEX;
bool lastPlottedUseRollingAvg = false;

// ----------- Status Line State
bool hasDrawnStatusLine = false;
bool lastStatusWasCooldown = false;

// ----------- View State
ResultView resultView = ResultView::Summary;
bool sensorReady = false;

///
/// <summary>
/// Runs a single warm-up test at a target sample rate. When no fixed duration is
/// given, samples until a timed rolling average fails to exceed its peak for
/// several consecutive checks (or the maximum test duration elapses). When a fixed
/// duration is given, samples for exactly that long. Records the peak temperature
/// reached, the achieved rate, and the raw samples (with elapsed time) collected
/// during the run.
/// </summary>
///
class TestCase
{
private:
   TempSensor& _sensor;
   TimedAverage _rollingAvg;
   RollingRate _rollingRate;
   Timer _sampleTimer = Timer(1UL);
   Timer _plateauCheckTimer = Timer(PLATEAU_CHECK_INTERVAL_MS);

   unsigned long _startMs = 0;
   float _startTempF = NAN;
   unsigned long _fixedDurationMs = 0;
   unsigned long _samples = 0;
   float _previousRollingAvg = NAN;
   float _peakRollingAvg = NAN;
   unsigned long _samplesAtPeak = 0;
   int _stableCheckCount = 0;
   bool _complete = true;

   bool _hasResult = false;
   float _resultTempF = NAN;
   float _resultRate = NAN;
   unsigned long _resultElapsedMs = 0;
   unsigned long _resultSteadyStateSamples = 0;

   float _rawSamples[MAX_RAW_SAMPLES_PER_TEST];
   float _rollingAvgSamples[MAX_RAW_SAMPLES_PER_TEST];
   unsigned long _rawElapsedMs[MAX_RAW_SAMPLES_PER_TEST];
   size_t _rawSampleCount = 0;

   /// <summary>
   /// Records the completed test's result from the given peak temperature and elapsed time.
   /// </summary>
   /// <param name="tempF">Peak (steady-state) temperature reached, in Fahrenheit.</param>
   /// <param name="elapsedMs">Elapsed time since the test started, in milliseconds.</param>
   void _finish(float tempF, unsigned long elapsedMs)
   {
      _resultRate = (elapsedMs > 0) ? (1000.0f * static_cast<float>(_samples) / static_cast<float>(elapsedMs)) : NAN;
      _resultTempF = tempF;
      _resultElapsedMs = elapsedMs;
      _resultSteadyStateSamples = _samplesAtPeak;
      _hasResult = true;
      _complete = true;
   }

public:
   ///
   /// <summary>
   /// Initializes a new instance of the TestCase class.
   /// </summary>
   /// <param name="sensor">Sensor to sample during the test.</param>
   ///
   explicit TestCase(TempSensor& sensor)
      : _sensor(sensor),
      _rollingAvg(static_cast<unsigned long>(ROLLING_AVERAGE_SPAN_S * 1000.0f))
   {
   }

   ///
   /// <summary>
   /// Starts a new test run at the given target sample rate.
   /// </summary>
   /// <param name="sampleRate">Target samples per second.</param>
   /// <param name="fixedDurationMs">
   /// When non-zero, the test runs for exactly this duration instead of using
   /// plateau detection to decide when to stop.
   /// </param>
   ///
   void start(unsigned long sampleRate, unsigned long fixedDurationMs = 0)
   {
      _rollingAvg.reset();
      _rollingRate.reset();
      _previousRollingAvg = NAN;
      _peakRollingAvg = NAN;
      _samplesAtPeak = 0;
      _stableCheckCount = 0;
      _samples = 0;
      _startMs = millis();
      _startTempF = _sensor.readTemperatureF();
      _fixedDurationMs = fixedDurationMs;
      _complete = false;
      _hasResult = false;
      _resultTempF = NAN;
      _resultRate = NAN;
      _resultElapsedMs = 0;
      _resultSteadyStateSamples = 0;
      _rawSampleCount = 0;

      unsigned long intervalMs = (sampleRate == 0) ? 1UL : max(1UL, 1000UL / sampleRate);
      _sampleTimer = Timer(intervalMs);
      _plateauCheckTimer = Timer(PLATEAU_CHECK_INTERVAL_MS);
   }

   ///
   /// <summary>
   /// Samples the sensor at the configured rate, tracks the peak rolling average, and
   /// completes the test once a fixed duration elapses, or (when no fixed duration is
   /// set) once the average fails to exceed its peak for several consecutive checks or
   /// the maximum test duration is reached.
   /// </summary>
   ///
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
      _rollingAvg.set(tempF);
      _rollingRate.tick();
      unsigned long elapsedMs = millis() - _startMs;

      float rollingAvg = _rollingAvg.get();

      if (_rawSampleCount < MAX_RAW_SAMPLES_PER_TEST)
      {
         _rawSamples[_rawSampleCount] = tempF;
         _rollingAvgSamples[_rawSampleCount] = rollingAvg;
         _rawElapsedMs[_rawSampleCount] = elapsedMs;
         _rawSampleCount++;
      }
      _samples++;

      if (isfinite(rollingAvg) && (!isfinite(_peakRollingAvg) || (rollingAvg > _peakRollingAvg)))
      {
         _peakRollingAvg = rollingAvg;
         _samplesAtPeak = _samples;
      }

      if (_fixedDurationMs > 0)
      {
         if (elapsedMs >= _fixedDurationMs)
         {
            _finish(_peakRollingAvg, elapsedMs);
         }
         return;
      }

      if (elapsedMs >= MAX_CASE_DURATION_MS)
      {
         _finish(_peakRollingAvg, elapsedMs);
         return;
      }

      if (elapsedMs < static_cast<unsigned long>(ROLLING_AVERAGE_SPAN_S * 1000.0f))
      {
         return;
      }

      if (!_plateauCheckTimer.ready())
      {
         return;
      }

      if (isfinite(_previousRollingAvg) && (rollingAvg <= (_previousRollingAvg + NO_INCREASE_EPSILON_F)))
      {
         _stableCheckCount++;
         if (_stableCheckCount >= PLATEAU_STABLE_CHECK_COUNT)
         {
            _finish(_peakRollingAvg, elapsedMs);
            return;
         }
      }
      else
      {
         _stableCheckCount = 0;
      }

      _previousRollingAvg = rollingAvg;
   }

   ///
   /// <summary>
   /// Gets whether the test has finished (either completed normally or not yet started).
   /// </summary>
   /// <returns>True if the test is complete.</returns>
   ///
   bool isComplete() const { return _complete; }

   ///
   /// <summary>
   /// Gets whether a completed result is available and has not yet been consumed.
   /// </summary>
   /// <returns>True if a result is available.</returns>
   ///
   bool hasResult() const { return _hasResult; }

   ///
   /// <summary>
   /// Marks the current result as consumed.
   /// </summary>
   ///
   void clearResult() { _hasResult = false; }

   ///
   /// <summary>
   /// Gets the temperature recorded when the test completed.
   /// </summary>
   /// <returns>Recorded temperature in Fahrenheit, or NAN if no result is available.</returns>
   ///
   float resultTempF() const { return _resultTempF; }

   ///
   /// <summary>
   /// Gets the sample rate achieved when the test completed.
   /// </summary>
   /// <returns>Achieved samples per second, or NAN if no result is available.</returns>
   ///
   float resultRate() const { return _resultRate; }

   ///
   /// <summary>
   /// Gets the elapsed time when the test completed.
   /// </summary>
   /// <returns>Elapsed time in milliseconds.</returns>
   ///
   unsigned long resultElapsedMs() const { return _resultElapsedMs; }

   ///
   /// <summary>
   /// Gets the number of samples collected before the rolling average reached its peak
   /// (steady-state) value during the completed test.
   /// </summary>
   /// <returns>Sample count at steady state.</returns>
   ///
   unsigned long resultSteadyStateSamples() const { return _resultSteadyStateSamples; }

   ///
   /// <summary>
   /// Gets the elapsed time since the in-progress test started.
   /// </summary>
   /// <returns>Elapsed time in milliseconds.</returns>
   ///
   unsigned long currentElapsedMs() const
   {
      return millis() - _startMs;
   }

   ///
   /// <summary>
   /// Gets the smoothed sample rate achieved so far during the in-progress test.
   /// </summary>
   /// <returns>Current samples per second.</returns>
   ///
   float currentRate()
   {
      return _rollingRate.get();
   }

   ///
   /// <summary>
   /// Gets the temperature recorded when the current (or most recently started) test began.
   /// </summary>
   /// <returns>Starting temperature in Fahrenheit.</returns>
   ///
   float startTempF() const { return _startTempF; }

   ///
   /// <summary>
   /// Samples the sensor and checks whether the temperature has returned to within the
   /// given tolerance of the temperature recorded when the test started.
   /// </summary>
   /// <param name="epsilonF">Maximum allowed difference from the starting temperature.</param>
   /// <returns>True if the current temperature is within tolerance of the starting temperature.</returns>
   ///
   bool isNearStartTempF(float epsilonF)
   {
      float tempF = _sensor.readTemperatureF();
      return isfinite(tempF) && isfinite(_startTempF) && (fabsf(tempF - _startTempF) <= epsilonF);
   }

   ///
   /// <summary>
   /// Gets the number of raw samples collected during the test.
   /// </summary>
   /// <returns>Raw sample count.</returns>
   ///
   size_t rawSampleCount() const { return _rawSampleCount; }

   ///
   /// <summary>
   /// Gets the raw samples collected during the test.
   /// </summary>
   /// <returns>Pointer to the raw sample array.</returns>
   ///
   const float* rawSamples() const { return _rawSamples; }

   ///
   /// <summary>
   /// Gets the rolling-average value computed at each raw sample collected during the
   /// test, parallel to rawSamples().
   /// </summary>
   /// <returns>Pointer to the rolling-average sample array.</returns>
   ///
   const float* rollingAvgSamples() const { return _rollingAvgSamples; }

   ///
   /// <summary>
   /// Gets the elapsed time (from test start) for each raw sample collected.
   /// </summary>
   /// <returns>Pointer to the raw elapsed-time array, parallel to rawSamples().</returns>
   ///
   const unsigned long* rawElapsedMs() const { return _rawElapsedMs; }
};

///
/// <summary>
/// Steps a TestCase through each target sample rate in turn, recording the achieved
/// rate, temperature, delta from the baseline, and raw samples for each completed
/// rate. Stops early if a rate cannot keep up with its target, or after the last
/// target rate completes.
/// </summary>
///
class TestRunner
{
private:
   TestCase* _testCase = nullptr;

   float _baselineTempF = NAN;
   bool _complete = true;
   bool _cooldown = false;
   size_t _currentRateIndex = 0;
   unsigned long _fixedDurationMs = 0;
   Timer _cooldownSampleTimer = Timer(COOLDOWN_SAMPLE_INTERVAL_MS);

   size_t _resultCount = 0;
   float _resultTemps[MAX_RESULTS] = { NAN };
   float _resultRates[MAX_RESULTS] = { NAN };
   unsigned long _resultSteadyStateSamples[MAX_RESULTS] = { 0 };
   int _pendingResultIndex = -1;

   float* _rawDatasets[MAX_RESULTS] = { nullptr };
   float* _rollingAvgDatasets[MAX_RESULTS] = { nullptr };
   unsigned long* _rawElapsedDatasets[MAX_RESULTS] = { nullptr };
   size_t _rawDatasetCounts[MAX_RESULTS] = { 0 };

   /// <summary>
   /// Releases and clears any raw/rolling-average sample datasets retained from a prior run.
   /// </summary>
   void _freeDatasets()
   {
      for (size_t i = 0; i < MAX_RESULTS; i++)
      {
         delete[] _rawDatasets[i];
         delete[] _rollingAvgDatasets[i];
         delete[] _rawElapsedDatasets[i];
         _rawDatasets[i] = nullptr;
         _rollingAvgDatasets[i] = nullptr;
         _rawElapsedDatasets[i] = nullptr;
         _rawDatasetCounts[i] = 0;
      }
   }

public:
   ///
   /// <summary>
   /// Releases the raw sample datasets retained for scatter plotting.
   /// </summary>
   ///
   ~TestRunner()
   {
      _freeDatasets();
   }

   ///
   /// <summary>
   /// Binds the runner to the TestCase it will drive.
   /// </summary>
   /// <param name="testCase">Test case to run at each target rate.</param>
   ///
   void begin(TestCase& testCase)
   {
      _testCase = &testCase;
   }

   ///
   /// <summary>
   /// Starts a new run, clearing prior results and beginning the first target rate.
   /// </summary>
   ///
   void start()
   {
      if (_testCase == nullptr)
      {
         return;
      }

      _complete = false;
      _cooldown = false;
      _baselineTempF = NAN;
      _currentRateIndex = 0;
      _fixedDurationMs = 0;
      _resultCount = 0;
      _pendingResultIndex = -1;
      _freeDatasets();
      _testCase->start(TARGET_SAMPLE_RATES[_currentRateIndex]);
   }

   ///
   /// <summary>
   /// Advances the run: while cooling down, samples the temperature once per second and,
   /// once it returns within COOLDOWN_CONVERGENCE_EPSILON_F of the just-finished test's
   /// starting temperature, starts the next target rate; otherwise advances the current
   /// test case and, once it completes, records its result and either starts a cooldown
   /// before the next target rate or ends the run. The first test establishes the fixed
   /// duration (via plateau detection) that every subsequent test runs for.
   /// </summary>
   ///
   void loop()
   {
      if (_complete)
      {
         return;
      }

      if (_cooldown)
      {
         if (!_cooldownSampleTimer.ready())
         {
            return;
         }

         if (!_testCase->isNearStartTempF(COOLDOWN_CONVERGENCE_EPSILON_F))
         {
            return;
         }

         _cooldown = false;
         _currentRateIndex++;
         _testCase->start(TARGET_SAMPLE_RATES[_currentRateIndex], _fixedDurationMs);
         return;
      }

      _testCase->loop();
      if (!_testCase->isComplete() || !_testCase->hasResult())
      {
         return;
      }

      float achievedRate = _testCase->resultRate();
      float temp = _testCase->resultTempF();
      unsigned long elapsedMs = _testCase->resultElapsedMs();
      unsigned long steadyStateSamples = _testCase->resultSteadyStateSamples();
      _testCase->clearResult();

      if (_currentRateIndex == 0)
      {
         _fixedDurationMs = elapsedMs;
      }

      if (_resultCount < MAX_RESULTS)
      {
         _resultRates[_resultCount] = achievedRate;
         _resultTemps[_resultCount] = temp;
         _resultSteadyStateSamples[_resultCount] = steadyStateSamples;

         size_t rawCount = _testCase->rawSampleCount();
         if (rawCount > 0)
         {
            const float* rawSamples = _testCase->rawSamples();
            const float* rollingAvgSamples = _testCase->rollingAvgSamples();
            const unsigned long* rawElapsedMs = _testCase->rawElapsedMs();
            float* dataset = new (std::nothrow) float[rawCount];
            float* rollingAvgDataset = new (std::nothrow) float[rawCount];
            unsigned long* elapsedDataset = new (std::nothrow) unsigned long[rawCount];
            if (dataset != nullptr && rollingAvgDataset != nullptr && elapsedDataset != nullptr)
            {
               for (size_t i = 0; i < rawCount; i++)
               {
                  dataset[i] = rawSamples[i];
                  rollingAvgDataset[i] = rollingAvgSamples[i];
                  elapsedDataset[i] = rawElapsedMs[i];
               }
               _rawDatasets[_resultCount] = dataset;
               _rollingAvgDatasets[_resultCount] = rollingAvgDataset;
               _rawElapsedDatasets[_resultCount] = elapsedDataset;
               _rawDatasetCounts[_resultCount] = rawCount;
            }
            else
            {
               delete[] dataset;
               delete[] rollingAvgDataset;
               delete[] elapsedDataset;
               Serial.println("Warning: Unable to allocate raw sample storage for scatter plot");
            }
         }

         if (_resultCount == 0)
         {
            _baselineTempF = temp;
         }

         _pendingResultIndex = static_cast<int>(_resultCount);
         _resultCount++;
      }

      if ((_currentRateIndex + 1) >= NUM_TARGET_SAMPLES)
      {
         _complete = true;
         return;
      }

      _cooldown = true;
      _cooldownSampleTimer.reset();
   }

   ///
   /// <summary>
   /// Gets the index of the target rate currently running (or about to run, during
   /// cooldown).
   /// </summary>
   /// <returns>Index into TARGET_SAMPLE_RATES.</returns>
   ///
   size_t currentRateIndex() const { return _currentRateIndex; }

   ///
   /// <summary>
   /// Gets the fixed test duration established by the first (max-rate) test, which
   /// every subsequent test runs for.
   /// </summary>
   /// <returns>Duration in milliseconds, or 0 before the first test completes.</returns>
   ///
   unsigned long fixedDurationMs() const { return _fixedDurationMs; }

   ///
   /// <summary>
   /// Gets whether the run has finished (either completed normally or not yet started).
   /// </summary>
   /// <returns>True if the run is complete.</returns>
   ///
   bool isComplete() const { return _complete; }

   ///
   /// <summary>
   /// Gets whether the run is currently in the cooldown period between tests.
   /// </summary>
   /// <returns>True if cooling down before the next target rate.</returns>
   ///
   bool isCooldown() const { return _cooldown; }

   ///
   /// <summary>
   /// Gets whether a newly completed result is available and has not yet been consumed.
   /// </summary>
   /// <returns>True if a pending result is available.</returns>
   ///
   bool hasPendingResult() const
   {
      return _pendingResultIndex >= 0;
   }

   ///
   /// <summary>
   /// Gets the index of the newly completed pending result.
   /// </summary>
   /// <returns>Index into the result/raw sample arrays.</returns>
   ///
   size_t pendingResultIndex() const
   {
      return static_cast<size_t>(_pendingResultIndex);
   }

   ///
   /// <summary>
   /// Marks the pending result as consumed.
   /// </summary>
   ///
   void clearPendingResult()
   {
      _pendingResultIndex = -1;
   }

   ///
   /// <summary>
   /// Gets the number of completed results recorded so far.
   /// </summary>
   /// <returns>Completed result count.</returns>
   ///
   size_t resultCount() const { return _resultCount; }

   ///
   /// <summary>
   /// Gets the achieved sample rate for a completed result.
   /// </summary>
   /// <param name="index">Result index.</param>
   /// <returns>Achieved samples per second, or NAN if the index is out of range.</returns>
   ///
   float resultRate(size_t index) const
   {
      if (index >= _resultCount)
      {
         return NAN;
      }
      return _resultRates[index];
   }

   ///
   /// <summary>
   /// Gets the recorded temperature for a completed result.
   /// </summary>
   /// <param name="index">Result index.</param>
   /// <returns>Temperature in Fahrenheit, or NAN if the index is out of range.</returns>
   ///
   float resultTemp(size_t index) const
   {
      if (index >= _resultCount)
      {
         return NAN;
      }
      return _resultTemps[index];
   }

   ///
   /// <summary>
   /// Gets the temperature delta from the baseline (first result) for a completed result.
   /// </summary>
   /// <param name="index">Result index.</param>
   /// <returns>Delta in Fahrenheit, or NAN if no baseline is available.</returns>
   ///
   float resultDelta(size_t index) const
   {
      if (!isfinite(_baselineTempF))
      {
         return NAN;
      }

      return resultTemp(index) - _baselineTempF;
   }

   ///
   /// <summary>
   /// Gets the number of samples collected before the rolling average reached its peak
   /// (steady-state) value for a completed result.
   /// </summary>
   /// <param name="index">Result index.</param>
   /// <returns>Sample count at steady state, or 0 if the index is out of range.</returns>
   ///
   unsigned long resultSteadyStateSamples(size_t index) const
   {
      if (index >= _resultCount)
      {
         return 0;
      }
      return _resultSteadyStateSamples[index];
   }

   ///
   /// <summary>
   /// Gets the number of raw samples retained for a completed result.
   /// </summary>
   /// <param name="index">Result index.</param>
   /// <returns>Raw sample count, or 0 if the index is out of range.</returns>
   ///
   size_t rawSampleCount(size_t index) const
   {
      if (index >= _resultCount)
      {
         return 0;
      }
      return _rawDatasetCounts[index];
   }

   ///
   /// <summary>
   /// Gets the raw samples retained for a completed result.
   /// </summary>
   /// <param name="index">Result index.</param>
   /// <returns>Pointer to the raw sample array, or nullptr if the index is out of range.</returns>
   ///
   const float* rawSamples(size_t index) const
   {
      if (index >= _resultCount)
      {
         return nullptr;
      }
      return _rawDatasets[index];
   }

   ///
   /// <summary>
   /// Gets the rolling-average samples retained for a completed result, parallel to
   /// rawSamples(index).
   /// </summary>
   /// <param name="index">Result index.</param>
   /// <returns>Pointer to the rolling-average sample array, or nullptr if the index is out of range.</returns>
   ///
   const float* rollingAvgSamples(size_t index) const
   {
      if (index >= _resultCount)
      {
         return nullptr;
      }
      return _rollingAvgDatasets[index];
   }

   ///
   /// <summary>
   /// Gets the elapsed time (from test start) for each raw sample retained for a
   /// completed result.
   /// </summary>
   /// <param name="index">Result index.</param>
   /// <returns>Pointer to the raw elapsed-time array, or nullptr if the index is out of range.</returns>
   ///
   const unsigned long* rawElapsedMs(size_t index) const
   {
      if (index >= _resultCount)
      {
         return nullptr;
      }
      return _rawElapsedDatasets[index];
   }
};

TestCase testCase(sensor);
TestRunner testRunner;

///
/// <summary>
/// Clears the display and draws the collecting-view title, recording the Y position
/// of the status line drawn below it.
/// </summary>
///
void drawCollectingHeader()
{
   arduino.setTextSize(3);
   arduino.clearDisplay();
   arduino.println("Warm-Up Tests", Color::HEADING);

   collectingRateRowY = arduino.getCursorY();
}

///
/// <summary>
/// Redraws the status line below the title: the current test's measured sample rate
/// (in that series' color) while collecting, or a cooldown message between tests. Only
/// clears the line first when switching between cooldown/collecting modes; otherwise the
/// fixed-width text overwrites its own background in place, avoiding a visible flash.
/// </summary>
///
void updateStatusLine()
{
   arduino.setTextSize(2);

   bool isCooldown = testRunner.isCooldown();
   bool modeChanged = !hasDrawnStatusLine || (isCooldown != lastStatusWasCooldown);

   if (modeChanged)
   {
      arduino.fillRect(0, collectingRateRowY, DISPLAY_WIDTH, arduino.charH(), Color::BLACK);
   }

   arduino.setCursor(0, collectingRateRowY);

   if (isCooldown)
   {
      arduino.print("Cooling down...", Color::LABEL);
   }
   else
   {
      Color seriesColor = SERIES_COLORS[testRunner.currentRateIndex() % NUM_SERIES_COLORS];
      arduino.print("Collecting data at ", testCase.currentRate(), collectingRateFormat, seriesColor);
   }

   hasDrawnStatusLine = true;
   lastStatusWasCooldown = isCooldown;
}

///
/// <summary>
/// Rounds a value down to one decimal place.
/// </summary>
/// <param name="value">Value to round.</param>
/// <returns>Value rounded down to the nearest tenth.</returns>
///
float floorToOneDecimal(float value)
{
   return floorf(value * 10.0f) / 10.0f;
}

///
/// <summary>
/// Rounds a value up to one decimal place.
/// </summary>
/// <param name="value">Value to round.</param>
/// <returns>Value rounded up to the nearest tenth.</returns>
///
float ceilToOneDecimal(float value)
{
   return ceilf(value * 10.0f) / 10.0f;
}

///
/// <summary>
/// Computes the shared Y-axis range across every series with data: each completed
/// test's samples, plus the currently in-progress test's samples (if the run
/// is active and not cooling down). Ranges over rolling-average values instead of raw
/// samples when requested.
/// </summary>
/// <param name="useRollingAvg">If true, ranges over rolling-average values instead of raw samples.</param>
/// <param name="outMin">Receives the minimum finite value found, rounded down to one decimal, or NAN if no data exists.</param>
/// <param name="outMax">Receives the maximum finite value found, rounded up to one decimal, or NAN if no data exists.</param>
///
void computeScatterYRange(bool useRollingAvg, float& outMin, float& outMax)
{
   float minValue = NAN;
   float maxValue = NAN;

   size_t resultCount = testRunner.resultCount();
   for (size_t series = 0; series < resultCount; series++)
   {
      const float* samples = useRollingAvg ? testRunner.rollingAvgSamples(series) : testRunner.rawSamples(series);
      size_t count = testRunner.rawSampleCount(series);
      for (size_t i = 0; i < count; i++)
      {
         float value = samples[i];
         if (!isfinite(value))
         {
            continue;
         }
         if (!isfinite(minValue) || (value < minValue)) minValue = value;
         if (!isfinite(maxValue) || (value > maxValue)) maxValue = value;
      }
   }

   if (!testRunner.isComplete() && !testRunner.isCooldown())
   {
      const float* samples = useRollingAvg ? testCase.rollingAvgSamples() : testCase.rawSamples();
      size_t count = testCase.rawSampleCount();
      for (size_t i = 0; i < count; i++)
      {
         float value = samples[i];
         if (!isfinite(value))
         {
            continue;
         }
         if (!isfinite(minValue) || (value < minValue)) minValue = value;
         if (!isfinite(maxValue) || (value > maxValue)) maxValue = value;
      }
   }

   outMin = isfinite(minValue) ? floorToOneDecimal(minValue) : NAN;
   outMax = isfinite(maxValue) ? ceilToOneDecimal(maxValue) : NAN;
}

///
/// <summary>
/// Gets the shared X-axis duration in milliseconds: the fixed duration established by
/// the first (max-rate) test once known, otherwise the in-progress elapsed time of that
/// first test rounded up to the next whole second. Rounding keeps the growing axis
/// stable for most of each second instead of changing on every redraw, which avoids
/// forcing a full chart redraw more often than necessary.
/// </summary>
/// <returns>Duration in milliseconds, or 0 if no test has produced any elapsed time yet.</returns>
///
unsigned long scatterXMaxMs()
{
   unsigned long fixedMs = testRunner.fixedDurationMs();
   if (fixedMs > 0)
   {
      return fixedMs;
   }

   if (!testRunner.isComplete() && !testRunner.isCooldown())
   {
      unsigned long elapsedMs = testCase.currentElapsedMs();
      return ((elapsedMs / 1000UL) + 1UL) * 1000UL;
   }

   return 0;
}

///
/// <summary>
/// Draws the combined multi-series scatter plot: every completed test's samples, plus
/// the currently in-progress test's samples (if any), each in its series color, against
/// automatically scaled axes shared by every series. Unless forced, a full clear and
/// redraw only happens when the axis range, view mode, or active series changes;
/// otherwise only newly collected points on the active series are plotted, which avoids
/// the flicker of repeatedly clearing and redrawing everything already on screen.
/// </summary>
/// <param name="useRollingAvg">If true, plots rolling-average values instead of raw samples.</param>
/// <param name="forceFullRedraw">If true, always performs a full clear and redraw.</param>
///
void drawScatterView(bool useRollingAvg, bool forceFullRedraw)
{
   float yMin;
   float yMax;
   computeScatterYRange(useRollingAvg, yMin, yMax);

   unsigned long xMaxMs = scatterXMaxMs();

   if (!isfinite(yMin) || !isfinite(yMax) || (xMaxMs == 0))
   {
      return;
   }

   size_t activeRateIndex = testRunner.isComplete() ? NO_RATE_INDEX : testRunner.currentRateIndex();
   bool axisChanged = (yMin != lastPlottedYMin) || (yMax != lastPlottedYMax) || (xMaxMs != lastPlottedXMaxMs);
   bool seriesChanged = (activeRateIndex != lastPlottedRateIndex) || (useRollingAvg != lastPlottedUseRollingAvg);
   bool needsFullRedraw = forceFullRedraw || axisChanged || seriesChanged;

   if (needsFullRedraw)
   {
      scatterPlot.beginOverlay(yMin, yMax, xMaxMs, scatterAxisFormat, scatterDurationFormat);

      size_t resultCount = testRunner.resultCount();
      for (size_t series = 0; series < resultCount; series++)
      {
         Color color = SERIES_COLORS[series % NUM_SERIES_COLORS];
         const float* samples = useRollingAvg ? testRunner.rollingAvgSamples(series) : testRunner.rawSamples(series);
         scatterPlot.plotSeries(samples, testRunner.rawElapsedMs(series), testRunner.rawSampleCount(series), 0, color);
      }
   }

   if (!testRunner.isComplete() && !testRunner.isCooldown())
   {
      size_t plotStartIndex = needsFullRedraw ? 0 : lastPlottedCurrentSampleCount;
      Color color = SERIES_COLORS[testRunner.currentRateIndex() % NUM_SERIES_COLORS];
      const float* samples = useRollingAvg ? testCase.rollingAvgSamples() : testCase.rawSamples();
      scatterPlot.plotSeries(samples, testCase.rawElapsedMs(), testCase.rawSampleCount(), plotStartIndex, color);

      lastPlottedCurrentSampleCount = testCase.rawSampleCount();
   }
   else
   {
      lastPlottedCurrentSampleCount = 0;
   }

   lastPlottedYMin = yMin;
   lastPlottedYMax = yMax;
   lastPlottedXMaxMs = xMaxMs;
   lastPlottedRateIndex = activeRateIndex;
   lastPlottedUseRollingAvg = useRollingAvg;
}

///
/// <summary>
/// Redraws the status line every loop iteration and, at the display refresh interval,
/// incrementally updates the live raw-sample scatter plot.
/// </summary>
///
void updateLiveView()
{
   updateStatusLine();

   if (!displayUpdateTimer.ready())
   {
      return;
   }

   drawScatterView(false, false);
}

///
/// <summary>
/// Draws the summary results table listing rate, temperature, delta, and the number of
/// samples needed to reach steady state for each completed test.
/// </summary>
///
void drawSummaryView()
{
   arduino.clearDisplay();
   arduino.setTextSize(3);
   arduino.println("Warm-Up Test", Color::HEADING);
   arduino.setTextSize(2);

   int16_t rateColX = 0;
   int16_t tempColX = rateColX + static_cast<int16_t>(rateFormat.length() * arduino.charW());
   int16_t deltaColX = tempColX + static_cast<int16_t>(tempFormat.length() * arduino.charW());
   int16_t samplesColX = deltaColX + static_cast<int16_t>(deltaFormat.length() * arduino.charW());

   int16_t headerY = arduino.getCursorY();
   arduino.setCursor(rateColX, headerY);
   arduino.print("Rate", rateFormat, Color::LABEL);
   arduino.setCursor(tempColX, headerY);
   arduino.print("Temp", tempFormat, Color::LABEL);
   arduino.setCursor(deltaColX, headerY);
   arduino.print("Delta", deltaFormat, Color::LABEL);
   arduino.setCursor(samplesColX, headerY);
   arduino.println("Smpl", samplesFormat, Color::LABEL);

   int16_t rowY = arduino.getCursorY();
   size_t resultCount = testRunner.resultCount();
   for (size_t i = 0; i < resultCount; i++)
   {
      if ((rowY + arduino.charH()) > arduino.height())
      {
         break;
      }

      Color rowColor = SERIES_COLORS[i % NUM_SERIES_COLORS];

      arduino.setCursor(rateColX, rowY);
      arduino.print(testRunner.resultRate(i), rateFormat, rowColor);

      arduino.setCursor(tempColX, rowY);
      arduino.print(testRunner.resultTemp(i), tempFormat, rowColor);

      arduino.setCursor(deltaColX, rowY);
      arduino.print(testRunner.resultDelta(i), deltaFormat, rowColor);

      arduino.setCursor(samplesColX, rowY);
      arduino.println(testRunner.resultSteadyStateSamples(i), samplesFormat, rowColor);

      rowY = arduino.getCursorY();
   }

   if ((rowY + arduino.charH()) <= arduino.height())
   {
      arduino.setCursor(0, rowY);
      arduino.println("A: Raw Scatter", Color::LABEL);
   }
}

///
/// <summary>
/// Draws the currently selected post-run view: the summary table, a raw-sample scatter
/// plot, or a rolling-average scatter plot, each covering every completed test.
/// </summary>
///
void drawResultView()
{
   if (resultView == ResultView::Summary)
   {
      drawSummaryView();
      return;
   }

   bool useRollingAvg = (resultView == ResultView::RollingAvgScatter);

   drawCollectingHeader();
   arduino.setTextSize(2);
   arduino.setCursor(0, collectingRateRowY);
   arduino.print(useRollingAvg ? "A: Summary Table" : "A: Rolling Avg Plot", Color::LABEL);
   drawScatterView(useRollingAvg, true);
}

///
/// <summary>
/// Starts a full test run: prints the serial results header, resets the runner and
/// result view, and draws the initial collecting view.
/// </summary>
///
void startTestRun()
{
   resultTable.printHeader();

   testRunner.start();
   resultView = ResultView::Summary;

   drawCollectingHeader();
   updateStatusLine();
   displayUpdateTimer.reset();
}

void setup()
{
   SerialX::begin();
   Wire.begin();

   arduino.begin();

   arduino.setTextSize(3);
   arduino.clearDisplay();
   arduino.println("Warm-Up Test", Color::HEADING);

   sensor.begin();

   if (!sensor.exists())
   {
      arduino.println();
      arduino.println("No Sensor Detected", Color::RED);
      Serial.println("Error: No temperature sensor detected");
      return;
   }

   sensorReady = true;
   testRunner.begin(testCase);
   startTestRun();
}

void loop()
{
   if (!sensorReady)
   {
      return;
   }

   if (testRunner.isComplete())
   {
      if (arduino.buttonA.wasPressed())
      {
         resultView++;
         drawResultView();
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
      unsigned long steadyStateSamples = testRunner.resultSteadyStateSamples(index);

      resultTable.printRow(
         SerialTable::fixed(rate, 2),
         SerialTable::fixed(temp, 2),
         SerialTable::fixed(delta, 2),
         steadyStateSamples);
      testRunner.clearPendingResult();

      if (testRunner.isComplete())
      {
         Serial.println("Tests complete");
         resultView = ResultView::Summary;
         drawResultView();
         return;
      }
   }

   updateLiveView();
}
