//
// Measures sensor self-heating (or drift) by stepping through decreasing sampling rates
// and recording how a sensor's value trends over a fixed 15 second duration at each rate.
//
// Behavior:
// - Cools down before the first test and between each subsequent test: samples the
//   sensor every 500ms and tracks a rolling average of the last 3 readings, ending
//   the cooldown once that average stays flat or increases.
// - Runs every target rate for a fixed 15 second duration.
// - Uses fixed target rates, slowest to fastest: 2, 10, 20, 30, 50, and 100 samples/second.
// - Records the peak (steady-state) value reached during each test and the increase
//   from the initial (pre-test) value.
//
// Display:
// - While a rate test is collecting samples, shows the target and actual (measured)
//   sample rates; during a cooldown period it shows "Cooling down..." along with the
//   live rolling-average value. The live scatter plot redraws at most once per
//   second while collecting.
// - A single scatter plot accumulates live, with each rate's samples plotted in
//   its own color against elapsed time. Both the X-axis and Y-axis automatically
//   scale to fit whatever is currently plotted.
// - Once every rate has been tested, shows the summary results table (target rate,
//   actual rate, starting value, and peak value increase) with 2 spaces
//   between columns. Rotating encoderA cycles between the summary table, a
//   points-and-lines scatter plot, a points-only scatter plot, and a lines-only
//   scatter plot; the scatter plot views show a color legend of the tested sample
//   rates (e.g. "100/s") in place of a button prompt.
//
// Hardware: ESP32_S3_Playground board (LGX_Hosyond_ST7796 display, rotary encoder),
// with the screen rotated 180 degrees from the default landscape orientation. Uses a
// TestSensor (see TestSensor.h) instead of physical hardware, so the sensor source can
// be swapped by changing TEST_SENSOR_TYPE there.
//
// System/standard library headers
#include <Wire.h>

// Local library headers (from libraries/Woyak)
#include "ESP32_S3_Playground.h"
#include "RollingAverage.h"
#include "RollingRate.h"
#include "ScatterPlot.h"
#include "SerialTable.h"
#include "SerialX.h"
#include "TestSensor.h"
#include "Timer.h"

///
/// <summary>
/// Which post-run view is currently displayed: the summary results table, a scatter plot
/// showing both raw-sample points and the rolling-average line, a raw-sample points-only
/// plot, or a rolling-average lines-only plot.
/// </summary>
///
enum class ResultView : uint8_t { Summary, PointsAndLines, Points, Lines };

///
/// <summary>
/// Advances or moves back the view by the given number of steps, wrapping around as
/// needed (e.g. as driven by a rotary encoder's delta()).
/// </summary>
/// <param name="view">View to adjust.</param>
/// <param name="delta">Number of steps to advance (positive) or move back (negative).</param>
/// <returns>Reference to the updated view.</returns>
///
inline ResultView& operator+=(ResultView& view, int32_t delta)
{
   constexpr int32_t numViews = static_cast<int32_t>(ResultView::Lines) + 1;
   int32_t index = (static_cast<int32_t>(view) + (delta % numViews) + numViews) % numViews;
   view = static_cast<ResultView>(index);
   return view;
}

// ----------- Target Sample Rates and Series Colors
///
/// <summary>
/// Pairs each target sample rate with its assigned scatter plot color for consistent
/// visual identification across all displays and plots.
/// </summary>
///
struct SampleRateSeries
{
   unsigned long rate;
   Color color;
};

constexpr SampleRateSeries TARGET_SERIES[] = {
   { 2UL, Color::GREEN },
   { 10UL, Color::YELLOW },
   { 20UL, Color::CYAN },
   { 30UL, Color::MAGENTA },
   { 50UL, Color::ORANGE },
   { 100UL, Color::PINK },
};
constexpr size_t NUM_TARGET_SAMPLES = sizeof(TARGET_SERIES) / sizeof(TARGET_SERIES[0]);

///
/// <summary>
/// Finds the largest value in the target sample rate array at compile time.
/// </summary>
/// <returns>The maximum target sample rate.</returns>
///
constexpr unsigned long _maxTargetSampleRate()
{
   unsigned long maxRate = TARGET_SERIES[0].rate;
   for (size_t i = 1; i < NUM_TARGET_SAMPLES; i++)
   {
      if (TARGET_SERIES[i].rate > maxRate)
      {
         maxRate = TARGET_SERIES[i].rate;
      }
   }
   return maxRate;
}
constexpr unsigned long MAX_TARGET_SAMPLE_RATE = _maxTargetSampleRate();

// ----------- Test Completion
constexpr unsigned long FIXED_TEST_DURATION_S = 15UL;
constexpr unsigned long ROLLING_AVERAGE_MIN_MS = 500UL;
constexpr unsigned long ROLLING_AVERAGE_TARGET_SAMPLE_COUNT = 10UL;
constexpr size_t MAX_RESULTS = NUM_TARGET_SAMPLES;
constexpr size_t RAW_SAMPLE_MARGIN = 200; // headroom added to the theoretical max sample count
constexpr size_t MAX_RAW_SAMPLES_PER_TEST = static_cast<size_t>(MAX_TARGET_SAMPLE_RATE * FIXED_TEST_DURATION_S) + RAW_SAMPLE_MARGIN;

///
/// <summary>
/// Computes the width of the centered moving-average smoothing period for a given
/// target sample rate: enough time to span ROLLING_AVERAGE_TARGET_SAMPLE_COUNT samples,
/// with a fixed minimum duration so low rates still get reasonable smoothing. All
/// moving-average math itself lives in ScatterPlot; this only decides how wide a period
/// to ask it for.
/// </summary>
/// <param name="sampleRate">Target samples per second, or 0 for an unbounded rate.</param>
/// <returns>Smoothing period width, in milliseconds.</returns>
///
unsigned long smoothingPeriodMs(unsigned long sampleRate)
{
   return (sampleRate == 0)
      ? ROLLING_AVERAGE_MIN_MS
      : max(ROLLING_AVERAGE_MIN_MS, (ROLLING_AVERAGE_TARGET_SAMPLE_COUNT * 1000UL) / sampleRate);
}

// ----------- Cooldown
constexpr unsigned long COOLDOWN_STARTUP_DELAY_MS = 2000UL;
constexpr unsigned long COOLDOWN_SAMPLE_PERIOD_MS = 500UL;
constexpr size_t NUM_COOLDOWN_ROLLING_SAMPLES = 4;

// ----------- Display Geometry
constexpr uint16_t DISPLAY_WIDTH = 480;
constexpr uint16_t DISPLAY_HEIGHT = 320;
constexpr uint8_t TITLE_TEXT_SIZE = 3;
constexpr uint8_t BODY_TEXT_SIZE = 2;

// ----------- Scatter Plot
constexpr uint16_t DISPLAY_UPDATE_INTERVAL_MS = 100;

// ----------- The Board
ESP32_S3_Playground arduino;
TestSensor sensor;

// ----------- Display Formats
Format targetRateFormat("###", 4, Format::Alignment::RIGHT);
Format actualRateFormat("###", 4, Format::Alignment::RIGHT);

// ----------- Summary Results Table
constexpr uint8_t COLUMN_SPACING_CHARS = 2;
constexpr const char* RESULT_TABLE_TITLE = "Warm-Up Test Results";
constexpr SerialTable::Column RESULT_TABLE_COLUMNS[] = {
   { "Target(/s)", 14 },
   { "Actual(/s)", 14 },
   { "Start", 12 },
   { "Delta", 12 },
};
constexpr size_t NUM_RESULT_TABLE_COLUMNS = sizeof(RESULT_TABLE_COLUMNS) / sizeof(RESULT_TABLE_COLUMNS[0]);
SerialTable resultTable(RESULT_TABLE_TITLE, RESULT_TABLE_COLUMNS, NUM_RESULT_TABLE_COLUMNS);

// ----------- Scatter Plot State
ScatterPlot scatterPlot(&arduino, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
Timer displayUpdateTimer(DISPLAY_UPDATE_INTERVAL_MS);

// ----------- Status Line State
bool hasDrawnStatusLine = false;
bool lastStatusWasCooldown = false;
float lastCooldownAvg = NAN;
size_t lastDrawnRatesCount = 0;
int16_t lastDrawnRatesWidth = 0;
int16_t statusLineY = 0;

// ----------- View State
ResultView resultView = ResultView::Summary;
bool sensorReady = false;

///
/// <summary>
/// Runs a single warm-up test at a target sample rate for the fixed test duration.
/// Records the peak value reached, the achieved rate, and appends each raw
/// sample (with elapsed time) to the ScatterPlotSeries assigned to this test.
/// </summary>
///
class TestCase
{
private:
   TestSensor* _sensor;
   ScatterPlot* _scatterPlot;
   ScatterPlotSeries* _series = nullptr;
   RollingRate _rollingRate;
   Timer _sampleTimer = Timer(1UL);

   unsigned long _startMs = 0;
   float _startValue = NAN;
   unsigned long _targetRate = 0;
   unsigned long _samples = 0;
   bool _complete = true;

   // Width of the centered moving-average period (a span of elapsed time), derived from
   // the target sample rate so the average always mixes ~10 samples' worth of data
   // regardless of rate. Assigned to the series' movingSampleSize so the moving
   // average itself is computed internally by ScatterPlotSeries.
   unsigned long _smoothingPeriodMs = ROLLING_AVERAGE_MIN_MS;

   bool _hasResult = false;
   float _resultValue = NAN;
   float _resultRate = NAN;
   float _resultStartValue = NAN;

   ///
   /// <summary>
   /// Records the completed test's result: the peak (steady-state) moving-average
   /// value reached, computed internally by the series from its raw samples, the
   /// achieved rate, and the starting value.
   /// </summary>
   /// <param name="elapsedMs">Elapsed time since the test started, in milliseconds.</param>
   ///
   void _finish(unsigned long elapsedMs)
   {
      _resultRate = (elapsedMs > 0) ? (1000.0f * static_cast<float>(_samples) / static_cast<float>(elapsedMs)) : NAN;
      if (_series != nullptr)
      {
         _series->finalized = true;
         _resultValue = _series->findMovingAveragePeak();
      }
      else
      {
         _resultValue = NAN;
      }
      _resultStartValue = _startValue;
      _hasResult = true;
      _complete = true;
   }

public:
   ///
   /// <summary>
   /// Initializes a new instance of the TestCase class.
   /// </summary>
   /// <param name="sensor">Sensor to sample during the test.</param>
   /// <param name="scatterPlot">Scatter plot that owns each rate's data series.</param>
   ///
   TestCase(TestSensor* sensor, ScatterPlot* scatterPlot)
      : _sensor(sensor), _scatterPlot(scatterPlot)
   {
   }

   ///
   /// <summary>
   /// Starts a new test run at the given target sample rate, appending samples to the
   /// series at seriesIndex. The centered smoothing duration is derived from the
   /// target rate so the moving average always mixes ~10 samples' worth of data (500ms
   /// minimum), keeping visual smoothness consistent across rates.
   /// </summary>
   /// <param name="sampleRate">Target samples per second.</param>
   /// <param name="seriesIndex">Index of the ScatterPlot series to append this test's samples to.</param>
   ///
   void start(unsigned long sampleRate, size_t seriesIndex)
   {
      _smoothingPeriodMs = smoothingPeriodMs(sampleRate);
      _series = (_scatterPlot != nullptr) ? _scatterPlot->getSeries(seriesIndex) : nullptr;

      if (_series != nullptr)
      {
         _series->clear();
         _series->movingSampleSize = static_cast<float>(_smoothingPeriodMs) / 1000.0f;
      }

      _rollingRate.reset();
      _samples = 0;
      _startMs = millis();
      _startValue = NAN;
      _targetRate = sampleRate;
      _complete = false;
      _hasResult = false;
      _resultValue = NAN;
      _resultRate = NAN;
      _resultStartValue = NAN;

      unsigned long intervalMs = (sampleRate == 0) ? 1UL : max(1UL, 1000UL / sampleRate);
      _sampleTimer = Timer(intervalMs);
   }

   ///
   /// <summary>
   /// Samples the sensor at the configured rate, appending the raw value and elapsed time
   /// to this test's series, and completes the test once the fixed test duration elapses.
   /// The peak (steady-state) moving-average value is computed internally by the
   /// series once the test finishes.
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

      float currentValue = _sensor->get();
      if (!isfinite(_startValue))
      {
         _startValue = currentValue;
         // Adjust start time so the first reading's elapsed time is exactly 0.
         _startMs = millis();
      }

      float value = currentValue - _startValue;
      unsigned long elapsedMs = millis() - _startMs;
      _rollingRate.tick();

      if (_series != nullptr)
      {
         _series->add(static_cast<float>(elapsedMs) / 1000.0f, value);
      }
      _samples++;

      if (elapsedMs >= (FIXED_TEST_DURATION_S * 1000UL))
      {
         _finish(elapsedMs);
      }
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
   /// Gets the value recorded when the test completed.
   /// </summary>
   /// <returns>Recorded value, or NAN if no result is available.</returns>
   ///
   float resultValue() const { return _resultValue; }

   ///
   /// <summary>
   /// Gets the sample rate achieved when the test completed.
   /// </summary>
   /// <returns>Achieved samples per second, or NAN if no result is available.</returns>
   ///
   float resultRate() const { return _resultRate; }

   ///
   /// <summary>
   /// Gets the starting value recorded when the test completed.
   /// </summary>
   /// <returns>Starting value, or NAN if no result is available.</returns>
   ///
   float resultStartValue() const { return _resultStartValue; }

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
   /// Gets the target sample rate for the in-progress (or most recently started) test.
   /// </summary>
   /// <returns>Target samples per second.</returns>
   ///
   unsigned long targetRate() const { return _targetRate; }
};

///
/// <summary>
/// Monitors the sensor while cooling down between tests (and before the first test),
/// sampling at a fixed period and completing once a short rolling average of readings
/// stops decreasing.
/// </summary>
///
class CooldownMonitor
{
private:
   TestSensor* _sensor;
   RollingAverage _rollingAvg;
   Timer _sampleTimer = Timer(COOLDOWN_SAMPLE_PERIOD_MS);
   Timer _startupTimer = Timer(COOLDOWN_STARTUP_DELAY_MS);

   bool _complete = true;
   bool _hasPrevAvg = false;
   float _prevAvg = NAN;
   float _currentAvg = NAN;

public:
   ///
   /// <summary>
   /// Initializes a new instance of the CooldownMonitor class.
   /// </summary>
   /// <param name="sensor">Sensor to sample while cooling down.</param>
   ///
   explicit CooldownMonitor(TestSensor* sensor)
      : _sensor(sensor),
      _rollingAvg(NUM_COOLDOWN_ROLLING_SAMPLES)
   {
   }

   ///
   /// <summary>
   /// Starts a new cooldown, clearing prior readings. Cooling-criteria sampling does not
   /// begin until an automatic startup delay has elapsed, giving the sensor a moment to
   /// settle first.
   /// </summary>
   ///
   void start()
   {
      _rollingAvg.reset();
      _sampleTimer.reset();
      _startupTimer.reset();
      _complete = false;
      _hasPrevAvg = false;
      _prevAvg = NAN;
      _currentAvg = NAN;
   }

   ///
   /// <summary>
   /// Waits out the startup delay, then samples the sensor at the configured period and
   /// completes the cooldown once the rolling average stays flat or increases from one
   /// sample to the next.
   /// </summary>
   ///
   void loop()
   {
      if (_complete)
      {
         return;
      }

      if (_startupTimer.remaining() > 0)
      {
         return;
      }

      if (!_sampleTimer.ready())
      {
         return;
      }

      _rollingAvg.set(_sensor->get());
      _currentAvg = _rollingAvg.get();

      if (_hasPrevAvg && isfinite(_currentAvg) && isfinite(_prevAvg) && (_currentAvg >= _prevAvg))
      {
         _complete = true;
         return;
      }

      if (isfinite(_currentAvg))
      {
         _prevAvg = _currentAvg;
         _hasPrevAvg = true;
      }
   }

   ///
   /// <summary>
   /// Gets whether the rolling average has stayed flat or increased, ending the cooldown.
   /// </summary>
   /// <returns>True if the cooldown is complete.</returns>
   ///
   bool isComplete() const { return _complete; }

   ///
   /// <summary>
   /// Gets the most recently computed rolling-average value.
   /// </summary>
   /// <returns>Rolling-average value, or NAN if no sample has been taken.</returns>
   ///
   float currentAvg() const { return _currentAvg; }
};

///
/// <summary>
/// Steps a TestCase through each target sample rate in turn, recording the target rate,
/// achieved rate, and value delta from the baseline for each completed rate (each
/// rate's raw samples remain in the ScatterPlot series TestCase appended them to). A
/// CooldownMonitor runs before the first test and between each subsequent test until the
/// sensor's value stops falling.
/// </summary>
///
class TestRunner
{
private:
   TestCase* _testCase = nullptr;
   CooldownMonitor* _cooldownMonitor = nullptr;

   bool _complete = true;
   bool _cooldown = false;
   size_t _currentRateIndex = 0;

   size_t _resultCount = 0;
   float _resultValues[MAX_RESULTS] = { NAN };
   unsigned long _resultTargetRates[MAX_RESULTS] = { 0 };
   float _resultRates[MAX_RESULTS] = { NAN };
   float _resultStartValues[MAX_RESULTS] = { NAN };
   int _pendingResultIndex = -1;

public:
   ///
   /// <summary>
   /// Binds the runner to the TestCase it will drive and the CooldownMonitor used
   /// before the first test and between subsequent tests.
   /// </summary>
   /// <param name="testCase">Test case to run at each target rate.</param>
   /// <param name="cooldownMonitor">Monitor used to detect when cooling down is complete.</param>
   ///
   void begin(TestCase* testCase, CooldownMonitor* cooldownMonitor)
   {
      _testCase = testCase;
      _cooldownMonitor = cooldownMonitor;
   }

   ///
   /// <summary>
   /// Starts a new run, clearing prior results and beginning a cooldown period before
   /// the first target rate.
   /// </summary>
   ///
   void start()
   {
      if (_testCase == nullptr || _cooldownMonitor == nullptr)
      {
         return;
      }

      _complete = false;
      _cooldown = true;
      _currentRateIndex = 0;
      _resultCount = 0;
      _pendingResultIndex = -1;
      _cooldownMonitor->start();
   }

   ///
   /// <summary>
   /// Advances the run: while cooling down, waits for the CooldownMonitor to detect the
   /// sensor has stopped cooling, then starts the current target rate; otherwise advances
   /// the current test case and, once it completes, records its result and either starts
   /// a cooldown before the next target rate or ends the run.
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
         _cooldownMonitor->loop();
         if (!_cooldownMonitor->isComplete())
         {
            return;
         }

         _cooldown = false;
         _testCase->start(TARGET_SERIES[_currentRateIndex].rate, _currentRateIndex);
         return;
      }

      _testCase->loop();
      if (!_testCase->isComplete() || !_testCase->hasResult())
      {
         return;
      }

      unsigned long targetRate = _testCase->targetRate();
      float achievedRate = _testCase->resultRate();
      float value = _testCase->resultValue();
      float startValue = _testCase->resultStartValue();
      _testCase->clearResult();

      if (_resultCount < MAX_RESULTS)
      {
         _resultTargetRates[_resultCount] = targetRate;
         _resultRates[_resultCount] = achievedRate;
         _resultValues[_resultCount] = value;
         _resultStartValues[_resultCount] = startValue;

         _pendingResultIndex = static_cast<int>(_resultCount);
         _resultCount++;
      }

      if ((_currentRateIndex + 1) >= NUM_TARGET_SAMPLES)
      {
         _complete = true;
         return;
      }

      _currentRateIndex++;
      _cooldown = true;
      _cooldownMonitor->start();
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
   /// Gets the current rolling-average value while cooling down.
   /// </summary>
   /// <returns>Rolling-average value, or NAN if not cooling down.</returns>
   ///
   float cooldownAvgValue() const
   {
      return _cooldown ? _cooldownMonitor->currentAvg() : NAN;
   }

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
   /// <returns>Index into the result arrays.</returns>
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
   /// Gets the target sample rate for a completed result.
   /// </summary>
   /// <param name="index">Result index.</param>
   /// <returns>Target samples per second, or 0 if the index is out of range.</returns>
   ///
   unsigned long resultTargetRate(size_t index) const
   {
      if (index >= _resultCount)
      {
         return 0;
      }
      return _resultTargetRates[index];
   }

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
   /// Gets the recorded value for a completed result.
   /// </summary>
   /// <param name="index">Result index.</param>
   /// <returns>Value, or NAN if the index is out of range.</returns>
   ///
   float resultValue(size_t index) const
   {
      if (index >= _resultCount)
      {
         return NAN;
      }
      return _resultValues[index];
   }

   ///
   /// <summary>
   /// Gets the starting value recorded for a completed result.
   /// </summary>
   /// <param name="index">Result index.</param>
   /// <returns>Starting value, or NAN if the index is out of range.</returns>
   ///
   float resultStartValue(size_t index) const
   {
      if (index >= _resultCount)
      {
         return NAN;
      }
      return _resultStartValues[index];
   }
};

TestCase testCase(&sensor, &scatterPlot);
CooldownMonitor cooldownMonitor(&sensor);
TestRunner testRunner;

///
/// <summary>
/// Clears the display and draws the collecting-view title, recording the Y position
/// of the status line drawn below it. Resizes the scatter plot to fill the remaining
/// space below the header, so its top edge always matches the actual rendered header
/// height instead of a hard-coded estimate.
/// </summary>
///
void drawCollectingHeader()
{
   arduino.setTextSize(TITLE_TEXT_SIZE);
   arduino.clearDisplay();
   arduino.println("Sensor Warm-Up", Color::HEADING);

   arduino.setTextSize(BODY_TEXT_SIZE);
   statusLineY = arduino.getCursorY();
   arduino.setCursorY(statusLineY + arduino.charH());
   arduino.setCursorY(arduino.getCursorY() + arduino.charH() / 4);

   int16_t headerHeight = arduino.getCursorY();
   scatterPlot.setRect(0, headerHeight, DISPLAY_WIDTH, DISPLAY_HEIGHT - headerHeight);
}

///
/// <summary>
/// Clears the top-right status row (a single line of BODY_TEXT_SIZE text on the line
/// just below the header) so it can be redrawn with new content.
/// </summary>
///
void clearTopRightStatus()
{
   arduino.setTextSize(BODY_TEXT_SIZE);
   int16_t rightContentStartX = DISPLAY_WIDTH * 40 / 100;  // Start clearing at 40% (keeping left 40%, clearing right 60%)
   arduino.fillRect(rightContentStartX, statusLineY, DISPLAY_WIDTH - rightContentStartX, arduino.charH(), Color::BLACK);
}

///
/// <summary>
/// Redraws the status line on the line just below the header: the current test's
/// measured sample rate (in that series' color) while collecting, or the live cooldown
/// rolling-average value between tests (and before the first test). The row is
/// cleared first whenever the content changes so switching between cooldown/collecting
/// modes never leaves stale text behind.
/// </summary>
///
void updateStatusLine()
{
   arduino.setTextSize(BODY_TEXT_SIZE);

   bool isCooldown = testRunner.isCooldown();
   float currentAvg = isCooldown ? testRunner.cooldownAvgValue() : NAN;

   bool modeChanged = !hasDrawnStatusLine || (isCooldown != lastStatusWasCooldown);
   bool avgChanged = isCooldown && ((isfinite(currentAvg) != isfinite(lastCooldownAvg)) || (isfinite(currentAvg) && (currentAvg != lastCooldownAvg)));

   if (!modeChanged && !avgChanged)
   {
      return;
   }

   if (!isCooldown)
   {
      // Not cooling down; clear any leftover cooldown text. The rates display
      // (drawn from drawScatterView) owns this row while actively collecting.
      if (modeChanged)
      {
         clearTopRightStatus();
      }
      hasDrawnStatusLine = true;
      lastStatusWasCooldown = isCooldown;
      lastCooldownAvg = currentAvg;
      return;
   }

   clearTopRightStatus();
   arduino.setCursor(0, statusLineY);

   if (isfinite(currentAvg))
   {
      arduino.printlnR("Cooling Down: ", currentAvg, sensor.getHighResFormat(), Color::GRAY, Color::GRAY, Color::BLACK);
   }
   else
   {
      arduino.printlnR("Cooling down...", Color::GRAY);
   }

   hasDrawnStatusLine = true;
   lastStatusWasCooldown = isCooldown;
   lastCooldownAvg = currentAvg;
}

///
/// <summary>
/// Draws the combined multi-series scatter plot: every completed test's samples, plus
/// the currently in-progress test's samples (if any), each in its series color, against
/// axes that automatically scale to fit whatever is currently plotted. Can display raw
/// samples as discrete points, rolling-average samples as connected lines, or both
/// overlaid, by toggling each series' showPoints/showMovingAverage flags before calling
/// ScatterPlot::render(). A full clear and redraw only happens when the computed axis
/// range changes or forceFullRedraw requests one (e.g. because the view mode just
/// changed); otherwise only newly collected points are plotted.
/// </summary>
/// <param name="viewMode">Which scatter plot view to display (Points, Lines, or PointsAndLines).</param>
/// <param name="forceFullRedraw">If true, always performs a full clear and redraw.</param>
///
void drawScatterView(ResultView viewMode, bool forceFullRedraw)
{
   bool showRaw = (viewMode != ResultView::Lines);
   bool showMovingAverage = (viewMode != ResultView::Points);

   for (size_t i = 0; i < scatterPlot.getSeriesCount(); i++)
   {
      ScatterPlotSeries* series = scatterPlot.getSeries(i);
      series->showPoints = showRaw;
      series->showMovingAverage = showMovingAverage;
   }

   if (forceFullRedraw)
   {
      scatterPlot.invalidate();
   }

   arduino.display.startWrite();
   scatterPlot.render();
   arduino.display.endWrite();

   // Draw the actual rates on the line just below the header, right-aligned and
   // color-coded per series, only if there are completed results and we're not showing
   // the cooldown message instead (updateStatusLine owns this row while cooling down).
   // Include the currently running test's rate if one is active. Clears the row first
   // whenever the rate count or the rendered text width changes so a shrinking width
   // (e.g. "100/s" back down to "99/s") never leaves stale leading characters behind.
   size_t resultCount = testRunner.resultCount();
   bool isRunning = !testRunner.isComplete() && !testRunner.isCooldown();
   bool shouldShowRates = (resultCount > 0 || isRunning) && !testRunner.isCooldown();

   if (shouldShowRates)
   {
      arduino.setTextSize(BODY_TEXT_SIZE);

      constexpr const char* RATE_LABEL = " Sample Rate: ";

      // Count how many rates to display (completed + current if running)
      size_t displayRateCount = resultCount + (isRunning ? 1 : 0);

      // Build each rate's text and measure total width (including the label) so the
      // group is right-aligned
      String rateStrs[MAX_RESULTS + 1];
      int16_t totalWidth = arduino.charW() * strlen(RATE_LABEL);

      // Add completed results' rates
      for (size_t i = 0; i < resultCount; i++)
      {
         rateStrs[i] = String(static_cast<unsigned long>(testRunner.resultRate(i))) + "/s";
         if (i < displayRateCount - 1) rateStrs[i] += " ";
         totalWidth += arduino.charW() * rateStrs[i].length();
      }

      // Add currently running test's rate if applicable
      if (isRunning)
      {
         rateStrs[resultCount] = String(static_cast<unsigned long>(testCase.currentRate())) + "/s";
         totalWidth += arduino.charW() * rateStrs[resultCount].length();
      }

      if (displayRateCount != lastDrawnRatesCount || totalWidth < lastDrawnRatesWidth)
      {
         clearTopRightStatus();
      }

      int16_t x = DISPLAY_WIDTH - totalWidth;
      arduino.setCursor(x, statusLineY);

      arduino.print(RATE_LABEL, Color::LABEL);

      // Print completed results' rates
      for (size_t i = 0; i < resultCount; i++)
      {
         Color color = TARGET_SERIES[i % NUM_TARGET_SAMPLES].color;
         arduino.print(rateStrs[i].c_str(), color);
      }

      // Print current rate if running
      if (isRunning)
      {
         Color color = TARGET_SERIES[testRunner.currentRateIndex() % NUM_TARGET_SAMPLES].color;
         arduino.print(rateStrs[resultCount].c_str(), color);
      }

      lastDrawnRatesCount = displayRateCount;
      lastDrawnRatesWidth = totalWidth;
   }
}

///
/// <summary>
/// At the display refresh interval, redraws the status line and incrementally updates
/// the live raw-sample scatter plot.
/// </summary>
///
void updateLiveView()
{
   if (!displayUpdateTimer.ready())
   {
      return;
   }

   updateStatusLine();
   drawScatterView(ResultView::PointsAndLines, false);
}

///
/// <summary>
/// Draws the summary results table: target rate, actual rate, starting value,
/// and value delta for each completed test.
/// </summary>
///
void drawSummaryView()
{
   arduino.clearDisplay();
   arduino.setTextSize(TITLE_TEXT_SIZE);
   arduino.println("Sensor Warm-Up", Color::HEADING);
   arduino.setCursorY(arduino.getCursorY() + arduino.charH() / 4);
   arduino.setTextSize(BODY_TEXT_SIZE);

   int16_t charWidth = arduino.charW();
   int16_t columnGap = COLUMN_SPACING_CHARS * charWidth;

   // Column headers with their widths (in characters)
   const char* targetHeader = "Target(/s)";
   const char* actualHeader = "Actual(/s)";
   const char* startHeader = "Start";
   const char* deltaHeader = "Delta";

   int16_t targetColWidth = 10 * charWidth; // "Target(/s)" = 10 chars
   int16_t actualColWidth = 10 * charWidth; // "Actual(/s)" = 10 chars
   int16_t startColWidth = 8 * charWidth;   // reserve the same column width as before

   int16_t targetColX = 0;
   int16_t actualColX = targetColX + targetColWidth + columnGap;
   int16_t startColX = actualColX + actualColWidth + columnGap;
   int16_t incrColX = startColX + startColWidth + columnGap;

   int16_t headerY = arduino.getCursorY();
   arduino.setCursor(targetColX, headerY);
   arduino.print(targetHeader, Color::LABEL);
   arduino.setCursor(actualColX, headerY);
   arduino.print(actualHeader, Color::LABEL);
   arduino.setCursor(startColX, headerY);
   arduino.print(startHeader, Color::LABEL);
   arduino.setCursor(incrColX, headerY);
   arduino.println(deltaHeader, Color::LABEL);

   int16_t rowY = arduino.getCursorY();
   size_t resultCount = testRunner.resultCount();
   for (size_t i = 0; i < resultCount; i++)
   {
      if ((rowY + arduino.charH()) > arduino.height())
      {
         break;
      }

      Color rowColor = TARGET_SERIES[i % NUM_TARGET_SAMPLES].color;

      arduino.setCursor(targetColX, rowY);
      arduino.print(testRunner.resultTargetRate(i), targetRateFormat, rowColor);

      arduino.setCursor(actualColX, rowY);
      arduino.print(testRunner.resultRate(i), actualRateFormat, rowColor);

      arduino.setCursor(startColX, rowY);
      arduino.print(testRunner.resultStartValue(i), sensor.getFormat(), rowColor);

      arduino.setCursor(incrColX, rowY);
      arduino.println(testRunner.resultValue(i), sensor.getHighResFormat(), rowColor);

      rowY = arduino.getCursorY();
   }
}

///
/// <summary>
/// Draws the currently selected post-run view: the summary table, a points-and-lines
/// scatter plot, a points-only scatter plot, or a lines-only scatter plot, each covering
/// every completed test. Rotating encoderA cycles between views.
/// </summary>
///
void drawResultView()
{
   if (resultView == ResultView::Summary)
   {
      drawSummaryView();
      return;
   }

   drawCollectingHeader();
   arduino.setTextSize(BODY_TEXT_SIZE);

   drawScatterView(resultView, true);
}

///
/// <summary>
/// Starts a full test run: prints the serial results header, resets the runner and
/// result view, clears every scatter plot series so old data disappears immediately,
/// and draws the initial collecting view.
/// </summary>
///
void startTestRun()
{
   resultTable.printHeader();

   testRunner.start();
   resultView = ResultView::Summary;

   for (size_t i = 0; i < scatterPlot.getSeriesCount(); i++)
   {
      scatterPlot.getSeries(i)->clear();
   }
   scatterPlot.invalidate();

   drawCollectingHeader();
   hasDrawnStatusLine = false;
   lastStatusWasCooldown = false;
   lastCooldownAvg = NAN;
   lastDrawnRatesCount = 0;
   lastDrawnRatesWidth = 0;
   updateStatusLine();
   displayUpdateTimer.reset();
}

void setup()
{
   SerialX::begin();
   Wire.begin();

   arduino.begin();
   arduino.setTextSize(TITLE_TEXT_SIZE);
   arduino.clearDisplay();
   arduino.println("Sensor Warm-Up", Color::HEADING);
   arduino.setCursorY(arduino.getCursorY() + arduino.charH() / 4);

   sensorReady = sensor.begin();

   if (!sensorReady)
   {
      arduino.println();
      arduino.println("No Sensor Detected", Color::RED);
      Serial.println("Error: sensor initialization failed");
      return;
   }

   for (size_t i = 0; i < NUM_TARGET_SAMPLES; i++)
   {
      ScatterPlotSeries* series = scatterPlot.createSeries(MAX_RAW_SAMPLES_PER_TEST);
      series->color = TARGET_SERIES[i].color;
   }

   scatterPlot.setYAxisFormat("##.##");
   scatterPlot.setXAxisFormat("##.#s");
   scatterPlot.setInitialYRange(-0.1f, 0.5f);

   testRunner.begin(&testCase, &cooldownMonitor);
   startTestRun();
}

void loop()
{
   if (!sensorReady)
   {
      return;
   }

   if (arduino.buttonA.wasPressed())
   {
      startTestRun();
      return;
   }

   if (testRunner.isComplete())
   {
      int32_t delta = arduino.encoderA.delta();
      if (delta != 0)
      {
         resultView += delta;
         drawResultView();
      }
      return;
   }

   testRunner.loop();

   if (testRunner.hasPendingResult())
   {
      size_t index = testRunner.pendingResultIndex();
      unsigned long targetRate = testRunner.resultTargetRate(index);
      float actualRate = testRunner.resultRate(index);
      float startValue = testRunner.resultStartValue(index);
      float incr = testRunner.resultValue(index);

      resultTable.printRow(
         targetRate,
         SerialTable::fixed(actualRate, 2),
         SerialTable::fixed(startValue, 2),
         SerialTable::fixed(incr, 2));
      testRunner.clearPendingResult();

      if (testRunner.isComplete())
      {
         Serial.println("Tests complete");
         arduino.encoderA.reset();

         // The live view already looks identical to the PointsAndLines result view,
         // so just switch the view state without redrawing.
         resultView = ResultView::PointsAndLines;
         return;
      }
   }

   updateLiveView();
}
