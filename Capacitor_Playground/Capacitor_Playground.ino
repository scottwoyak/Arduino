//
// Capacitor sensor tuner: displays real-time measurements and runs the selected test on Button A.
//
// Combines live monitoring with automated tests for CapacitorSensor. During normal runtime, the
// display shows active resistor selection, discharge delay, buffer size, test type, average
// charge time, sample rate, and effective output rate.
//
// Test Type field selects which test Button A runs:
// - Optimize: covers resistor/charge pin options (e.g., 1M, 470K, 100K, 47K), target effective
//   output rates (samples per second), and discharge delay values (200, 500, 750, 1000 us); the
//   deferred processing period is fixed internally by CapacitorSensor. For each test case, the
//   sensor is configured (discharge delay + buffer size), a fixed number of samples is captured, and
//   stats are computed (average charge time, variation, StdDev, raw rate, and effective rate).
//   Results are printed immediately to Serial in a fixed-width table, then ranked later by lowest
//   StdDev % per target rate. After the sweep,
//   a "best configurations" summary is printed for each target rate, and the best configuration
//   for 30/s is automatically applied and persisted for continued live viewing.
// - Buffer Size Sweep: collects 10,000 raw charge-time samples at the current resistor/discharge
//   configuration (updating the display only with the collected count to minimize interference),
//   then prints a table of range and StdDev for several rolling buffer sizes computed from the
//   same raw data.
//
// Button B cycles the embedded charge-time scatter plot through three states: off, on, and
// on with moving-average/StdDev overlays. The plot fills the space between the setup/measurement
// tables and the footer instructions.
//
// Live editable fields (Encoder A selects a field, Encoder B adjusts it, Encoder B Button resets
// to defaults) are shown directly on the live monitoring screen, so changing a value immediately
// re-applies it to the sensor and the resulting charge time/rate readouts update live:
// - Resistor: which charge pin/resistor is active (1M, 470K, 100K, 47K).
// - Discharge: discharge delay in microseconds.
// - Buffer: rolling-average buffer size.
// - Test Type: which test Button A runs (Optimize or Buffer Size Sweep).
// Values are persisted to Preferences as they change and reloaded automatically on startup. The
// best-known configuration saved by the Optimize sweep is also automatically applied to the
// sensor once it is ready after startup.
//
// Typical usage: adjust Resistor/Discharge/Buffer with Encoder A/B while watching the live
// charge time/rate readouts, select a Test Type, press Button A to run it, then use the serial
// output to choose resistor/delay/buffer combinations for your deployment.

// Hardware: ESP32_S3_Playground board (LGX_ST7796 display, encoders, standalone buttons).
//

#include <Arduino.h>
#include "ArduinoBoard.h"
#include "CapacitorSensor.h"
#include "RollingAverage.h"
#include "DisplayEditableTable.h"
#include "SerialTable.h"
#include "Stats.h"
#include "TimedStats.h"
#include "SerialX.h"
#include "DisplayEditableField.h"
#include "DisplayTextView.h"
#include "Timer.h"
#include "Stopwatch.h"
#include "TimedScatterPlot.h"
#include "ScatterPlot.h"
#include "Histogram.h"
#include "HistogramPlot.h"

///
/// <summary>
/// Forward declarations for types used in function signatures (Arduino's build system
/// auto-generates function prototypes that appear before these types are fully defined below).
/// </summary>
///

struct TestCase;
struct TestRunResult;

// ----------- Display
Arduino arduino;
Format chargeFormat("#####.# us");
Format resistorLabelFormat(4);
constexpr size_t DISPLAY_INTERVAL_MS = 200;
Timer displayTimer(DISPLAY_INTERVAL_MS);
bool forceDisplayUpdate = false; // Set true to force an immediate redraw outside the normal interval (e.g. on encoder changes).

// ----------- Test Output Log Viewer
DisplayTextView textViewer(&arduino, Rect16{ 0, 0, 0, 0 }, DEFAULT_TEXT_SIZE, Color::WHITE, Color::DARKGRAY);
bool viewingResults = false;

// ----------- Results Scatter Plot (bottom half of the results view, one-shot per test run)
constexpr size_t RESULTS_SCATTER_MAX_POINTS = 500;
ScatterPlot* resultsScatterPlot = nullptr;
Rect16 resultsScatterRect{ 0, 0, 0, 0 };

constexpr size_t RESULTS_HISTOGRAM_MIN_BINS = 10;
constexpr size_t RESULTS_HISTOGRAM_MAX_BINS = 40;
Histogram* resultsHistogram = nullptr;
HistogramPlot* resultsHistogramPlot = nullptr;
Rect16 resultsHistogramRect{ 0, 0, 0, 0 };

// ----------- Best Configuration Auto-Load
bool bestConfigLoaded = false; // Set true once loadBestConfiguration() has been attempted successfully after startup.

// ----------- Charge Time Scatter Plot (embedded on the main page, toggled via Button B)
/// <summary>Plot visibility states cycled through by Button B.</summary>
enum class PlotState
{
   OFF,
   ON,
   ON_WITH_STATS,
};

constexpr uint32_t SCATTER_HISTORY_S = 20;
TimedScatterPlot* chargeScatterPlot = nullptr;
IScatterPlotSeries* chargeScatterSeries = nullptr;
PlotState plotState = PlotState::OFF;
PlotState previousPlotState = PlotState::OFF;

// ----------- Test Sweep Parameters
constexpr uint32_t STATS_PERIOD_MS = 2000;
constexpr float TARGET_EFFECTIVE_RATES[] = { 15.0f, 30.0f, 50.0f };
constexpr size_t TARGET_EFFECTIVE_RATE_COUNT = sizeof(TARGET_EFFECTIVE_RATES) / sizeof(TARGET_EFFECTIVE_RATES[0]);
constexpr float PRIMARY_TARGET_EFFECTIVE_RATE = 30.0f;

constexpr uint16_t OPTIMIZATION_DISCHARGE_DELAYS_MICROS[] = { 200, 500, 750, 1000 };
constexpr size_t OPTIMIZATION_DISCHARGE_DELAY_COUNT = sizeof(OPTIMIZATION_DISCHARGE_DELAYS_MICROS) / sizeof(OPTIMIZATION_DISCHARGE_DELAYS_MICROS[0]);

// ----------- Rolling-Size Sweep Test Parameters
constexpr size_t ROLLING_SWEEP_SAMPLE_COUNT = 10000;
constexpr size_t ROLLING_SWEEP_SIZES[] = { 1, 10, 20, 50, 100, 200, 500, 1000 };
constexpr size_t ROLLING_SWEEP_SIZE_COUNT = sizeof(ROLLING_SWEEP_SIZES) / sizeof(ROLLING_SWEEP_SIZES[0]);
constexpr size_t ROLLING_SWEEP_DISPLAY_UPDATE_INTERVAL = 200; // Only update the display every N samples to minimize interference.

// ----------- Raw Data Capture Test Parameters
constexpr size_t RAW_DATA_CAPTURE_SAMPLE_COUNT = 10000;
constexpr size_t RAW_DATA_CAPTURE_DISPLAY_UPDATE_INTERVAL = 200; // Only update the display every N samples to minimize interference.
constexpr float RAW_DATA_CAPTURE_CONFIDENCE_PERCENTS[] = { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f, 20.0f, 30.0f };
constexpr size_t RAW_DATA_CAPTURE_CONFIDENCE_COUNT = sizeof(RAW_DATA_CAPTURE_CONFIDENCE_PERCENTS) / sizeof(RAW_DATA_CAPTURE_CONFIDENCE_PERCENTS[0]);

// ----------- Discharge Time Sweep Test Parameters
constexpr uint16_t DISCHARGE_SWEEP_MIN_MICROS = 100;
constexpr uint16_t DISCHARGE_SWEEP_MAX_MICROS = 1000;
constexpr uint16_t DISCHARGE_SWEEP_STEP_MICROS = 100;
constexpr size_t DISCHARGE_SWEEP_STEP_COUNT = (DISCHARGE_SWEEP_MAX_MICROS - DISCHARGE_SWEEP_MIN_MICROS) / DISCHARGE_SWEEP_STEP_MICROS + 1;
constexpr size_t DISCHARGE_SWEEP_SAMPLE_COUNT = 2000;
constexpr size_t DISCHARGE_SWEEP_DISPLAY_UPDATE_INTERVAL = 200; // Only update the display every N samples to minimize interference.

constexpr size_t MIN_TARGET_BUFFER_SIZE = 1;
constexpr size_t MAX_TARGET_BUFFER_SIZE = 500;

// ----------- Hardware Pin Assignments
constexpr uint8_t SENSE_PIN = CapacitorSensor::SENSE_PIN;
constexpr uint8_t CHARGE_PIN_1M = CapacitorSensor::CHARGE_PIN_1M;
constexpr uint8_t CHARGE_PIN_470K = CapacitorSensor::CHARGE_PIN_470K;
constexpr uint8_t CHARGE_PIN_100K = CapacitorSensor::CHARGE_PIN_100K;
constexpr uint8_t CHARGE_PIN_47K = CapacitorSensor::CHARGE_PIN_47K;

// ----------- Resistor (Charge Pin) Sweep
/// <summary>Pairs a charge pin with its resistor value label for the sweep.</summary>
struct ResistorOption
{
   /// <summary>Charge pin that drives this resistor.</summary>
   uint8_t chargePin = 0;
   /// <summary>Human-readable resistor value label.</summary>
   const char* label = nullptr;
};

constexpr ResistorOption RESISTOR_OPTIONS[] = {
   { CHARGE_PIN_1M, "1M" },
   { CHARGE_PIN_470K, "470K" },
   { CHARGE_PIN_100K, "100K" },
   { CHARGE_PIN_47K, "47K" },
};
constexpr size_t RESISTOR_OPTION_COUNT = sizeof(RESISTOR_OPTIONS) / sizeof(RESISTOR_OPTIONS[0]);
constexpr size_t MAX_TEST_RESULTS = RESISTOR_OPTION_COUNT * TARGET_EFFECTIVE_RATE_COUNT * OPTIMIZATION_DISCHARGE_DELAY_COUNT;

// ----------- Preferences Keys
const char PREF_NAMESPACE[] = "cap_tune";
const char PREF_VALID_KEY[] = "valid";
const char PREF_CHARGE_PIN_KEY[] = "cpin";
const char PREF_DISCHARGE_KEY[] = "dly";
const char PREF_BUFFER_KEY[] = "buf";

// ----------- Editable Fields (Encoder A selects a field, Encoder B adjusts it)
// Changing any of these immediately re-applies the configuration to the live sensor.
long resistorIndex = 0;
long dischargeDelayMicros = CapacitorSensor::DEFAULT_DISCHARGE_DELAY_MICROS;
long bufferSize = (long)CapacitorSensor::DEFAULT_BUFFER_SIZE;
long testType = 0;
float filter = CapacitorSensor::DEFAULT_FILTER;

Format resistorFieldFormat(8, Format::Alignment::LEFT);
Format dischargeFieldFormat("#### us", Format::Alignment::LEFT);
Format bufferFieldFormat("###", Format::Alignment::LEFT);
Format testTypeFieldFormat(20, Format::Alignment::LEFT);
Format filterFieldFormat("##.# %", Format::Alignment::LEFT);

// ----------- Measured Fields (read-only, share the same table as the editable fields)
// Updated each display refresh from the sensor/stats before table.draw() is called.
float measuredChargeTimeMicros = NAN;
float measuredStdDevMicros = NAN;
float measuredRangeMicros = NAN;
float measuredRawRate = 0.0f;
float measuredEffectiveRate = 0.0f;

Format measuredChargeFormat("#####.# us", Format::Alignment::LEFT);
Format measuredRateFormat("######/s", Format::Alignment::LEFT);
Format measuredEffectiveRateFormat("###/s", Format::Alignment::LEFT);

// ----------- Test Type Selection
/// <summary>Test types selectable via the Test Type live field, run by pressing Button A.</summary>
enum class TestType
{
   OPTIMIZE = 0,
   BUFFER_SIZE_SWEEP = 1,
   DISCHARGE_TIME_SWEEP = 2,
   RAW_DATA_CAPTURE = 3,
};

constexpr const char* TEST_TYPE_LABELS[] = { "Optimize", "Buffer Size Sweep", "Discharge Time Sweep", "Raw Data Capture" };
constexpr size_t TEST_TYPE_COUNT = sizeof(TEST_TYPE_LABELS) / sizeof(TEST_TYPE_LABELS[0]);

///
/// <summary>
/// Resistor-selection setup field that steps through RESISTOR_OPTIONS by index and displays
/// the resistor's label instead of a raw index number.
/// </summary>
///
class ResistorSetupField : public IntDisplayEditableField
{
public:
   using IntDisplayEditableField::IntDisplayEditableField;

   void adjust(int32_t direction) override
   {
      int32_t count = (int32_t)RESISTOR_OPTION_COUNT;
      long newValue = (*_value + (direction > 0 ? 1 : -1) + count) % count;
      *_value = newValue;
   }

   std::string valueText() override
   {
      long index = constrain(*_value, 0L, (int32_t)(RESISTOR_OPTION_COUNT - 1));
      return _format.toString(RESISTOR_OPTIONS[index].label);
   }
};

///
/// <summary>
/// Test-type-selection setup field that steps through TEST_TYPE_LABELS by index and displays
/// the test type's label instead of a raw index number.
/// </summary>
///
class TestTypeSetupField : public IntDisplayEditableField
{
public:
   using IntDisplayEditableField::IntDisplayEditableField;

   void adjust(int32_t direction) override
   {
      int32_t count = (int32_t)TEST_TYPE_COUNT;
      long newValue = (*_value + (direction > 0 ? 1 : -1) + count) % count;
      *_value = newValue;
   }

   std::string valueText() override
   {
      long index = constrain(*_value, 0L, (int32_t)(TEST_TYPE_COUNT - 1));
      return _format.toString(TEST_TYPE_LABELS[index]);
   }
};

ResistorSetupField resistorField("Resistor", &resistorIndex,
   0, (long)(RESISTOR_OPTION_COUNT - 1), 1, 0, resistorFieldFormat);
IntDisplayEditableField delayField("Discharge Time", &dischargeDelayMicros,
   50, 2000, 50, (long)CapacitorSensor::DEFAULT_DISCHARGE_DELAY_MICROS, dischargeFieldFormat);
IntDisplayEditableField bufferSizeField("Buffer Size", &bufferSize,
   (long)MIN_TARGET_BUFFER_SIZE, (long)MAX_TARGET_BUFFER_SIZE, 5, (long)CapacitorSensor::DEFAULT_BUFFER_SIZE, bufferFieldFormat);
TestTypeSetupField testTypeField("Test Type", &testType,
   0, (long)(TEST_TYPE_COUNT - 1), 1, 0, testTypeFieldFormat);
FloatDisplayEditableField filterField("Outlier Filter", &filter,
   0.0f, 50.0f, 1.0f, CapacitorSensor::DEFAULT_FILTER, filterFieldFormat);

ReadOnlyDisplayEditableField chargeTimeField("Avg Charge Time", &measuredChargeTimeMicros, measuredChargeFormat);
ReadOnlyDisplayEditableField stdDevField("StdDev", &measuredStdDevMicros, measuredChargeFormat);
ReadOnlyDisplayEditableField rangeField("Range", &measuredRangeMicros, measuredChargeFormat);
ReadOnlyDisplayEditableField rawRateField("Raw Rate", &measuredRawRate, measuredRateFormat);
ReadOnlyDisplayEditableField effectiveRateField("Effective Rate", &measuredEffectiveRate, measuredEffectiveRateFormat);

BlankDisplayEditableField testTypeSpacerField;

DisplayEditableField* setupFields[] = { &resistorField, &delayField, &bufferSizeField, &filterField, &testTypeSpacerField, &testTypeField };
DisplayEditableTable table(&arduino, PREF_NAMESPACE, setupFields, sizeof(setupFields) / sizeof(setupFields[0]), 0, 0);

DisplayEditableField* measurementFields[] = { &chargeTimeField, &stdDevField, &rangeField, &rawRateField, &effectiveRateField };
DisplayEditableTable measurementsTable(&arduino, PREF_NAMESPACE, measurementFields, sizeof(measurementFields) / sizeof(measurementFields[0]), 0, 0);

///
/// <summary>
/// Assigns the "Setup" and "Measurements" section headers to the field table (must run
/// after all field objects above are constructed).
/// </summary>
///
void initializeFieldSections()
{
   resistorField.setSection("Setup");
   chargeTimeField.setSection("Measurements");
}

///
/// <summary>Look up the resistor value label for a charge pin.</summary>
/// <param name="chargePin">Charge pin to look up</param>
/// <returns>Matching resistor value label, or "?" when the pin is unknown</returns>
///
const char* resistorLabel(uint8_t chargePin)
{
   for (size_t i = 0; i < RESISTOR_OPTION_COUNT; i++)
   {
      if (RESISTOR_OPTIONS[i].chargePin == chargePin)
      {
         return RESISTOR_OPTIONS[i].label;
      }
   }

   return "?";
}

///
/// <summary>Check whether a charge pin matches one of the configured resistor options.</summary>
/// <param name="chargePin">Charge pin to check</param>
/// <returns>True if the pin is a known resistor option</returns>
///
bool isKnownChargePin(uint8_t chargePin)
{
   for (size_t i = 0; i < RESISTOR_OPTION_COUNT; i++)
   {
      if (RESISTOR_OPTIONS[i].chargePin == chargePin)
      {
         return true;
      }
   }

   return false;
}

///
/// <summary>Look up the resistor option index for a charge pin.</summary>
/// <param name="chargePin">Charge pin to look up</param>
/// <returns>Matching resistor option index, or 0 when the pin is unknown</returns>
///
size_t resistorIndexFromChargePin(uint8_t chargePin)
{
   for (size_t i = 0; i < RESISTOR_OPTION_COUNT; i++)
   {
      if (RESISTOR_OPTIONS[i].chargePin == chargePin)
      {
         return i;
      }
   }

   return 0;
}

///
/// <summary>Look up the charge pin for a resistor value label.</summary>
/// <param name="label">Resistor value label to look up</param>
/// <returns>Matching charge pin, or the first resistor option's pin when the label is unknown</returns>
///
uint8_t chargePinFromResistorLabel(const char* label)
{
   if (label == nullptr)
   {
      return RESISTOR_OPTIONS[0].chargePin;
   }

   for (size_t i = 0; i < RESISTOR_OPTION_COUNT; i++)
   {
      if (strcmp(RESISTOR_OPTIONS[i].label, label) == 0)
      {
         return RESISTOR_OPTIONS[i].chargePin;
      }
   }

   return RESISTOR_OPTIONS[0].chargePin;
}

CapacitorSensor capacitorSensor(RESISTOR_OPTIONS[0].chargePin, SENSE_PIN);

// ----------- Live Display Statistics (StdDev/Range over a 500ms window)
TimedStats chargeStats(STATS_PERIOD_MS);
uint32_t chargeStatsLastCounter = 0;

///
/// <summary>Feeds newly-processed charge time samples into the live 500ms stats window.</summary>
///
void updateChargeStats()
{
   uint32_t counter = capacitorSensor.counter();
   if (counter == chargeStatsLastCounter)
   {
      return;
   }

   chargeStatsLastCounter = counter;
   float chargeTime = capacitorSensor.chargeTimeMicros();
   if (isfinite(chargeTime))
   {
      chargeStats.set(chargeTime);
   }
}

///
/// <summary>Applies a resistor/discharge/buffer configuration to the capacitor sensor.</summary>
/// <param name="chargePin">Charge pin to select</param>
/// <param name="dischargeDelayMicros">Discharge delay in microseconds</param>
/// <param name="bufferSize">Rolling average buffer size (falls back to the sensor default when zero)</param>
///
void applyConfiguration(uint8_t chargePin, uint16_t dischargeDelayMicros, size_t bufferSize)
{
   capacitorSensor.setChargePin(chargePin);
   capacitorSensor.setDischargeDelayMicros(dischargeDelayMicros);
   capacitorSensor.setBufferSize(bufferSize > 0 ? bufferSize : CapacitorSensor::DEFAULT_BUFFER_SIZE);
   chargeStats.reset();
}

///
/// <summary>Updates the editable fields (Resistor/Discharge/Buffer) to reflect a configuration
/// and persists the new values, so the on-screen fields stay in sync with the sensor.</summary>
/// <param name="newChargePin">Charge pin the sensor was configured with</param>
/// <param name="newDischargeDelayMicros">Discharge delay in microseconds the sensor was configured with</param>
/// <param name="newBufferSize">Rolling average buffer size the sensor was configured with</param>
///
void syncFields(uint8_t newChargePin, uint16_t newDischargeDelayMicros, size_t newBufferSize)
{
   resistorIndex = (long)resistorIndexFromChargePin(newChargePin);
   dischargeDelayMicros = (long)newDischargeDelayMicros;
   bufferSize = (long)newBufferSize;
   table.save();
}

///
/// <summary>Persists the best-known configuration to Preferences.</summary>
/// <param name="chargePin">Charge pin to save</param>
/// <param name="dischargeDelayMicros">Discharge delay in microseconds to save</param>
/// <param name="bufferSize">Rolling average buffer size to save</param>
///
void saveBestConfiguration(uint8_t chargePin, uint16_t dischargeDelayMicros, size_t bufferSize)
{
   arduino.preferences.begin(PREF_NAMESPACE, false);
   arduino.preferences.putBool(PREF_VALID_KEY, true);
   arduino.preferences.putUChar(PREF_CHARGE_PIN_KEY, chargePin);
   arduino.preferences.putUShort(PREF_DISCHARGE_KEY, dischargeDelayMicros);
   arduino.preferences.putUInt(PREF_BUFFER_KEY, (uint32_t)bufferSize);
   arduino.preferences.end();
}

///
/// <summary>
/// Loads the best-known configuration from Preferences and applies it, printing a summary to
/// Serial. Returns false if the sensor is not ready yet so the caller can retry later; returns
/// true once the attempt is complete (whether or not a saved configuration was applied).
/// </summary>
/// <returns>True if the load attempt is complete; false if the sensor is not ready yet and the caller should retry.</returns>
///
bool loadBestConfiguration()
{
   if (capacitorSensor.counter() == 0 || !isfinite(capacitorSensor.chargeTimeMicros()) || capacitorSensor.rate() <= 0.0f)
   {
      return false;
   }

   arduino.preferences.begin(PREF_NAMESPACE, true);
   bool hasBest = arduino.preferences.getBool(PREF_VALID_KEY, false);
   uint8_t chargePin = arduino.preferences.getUChar(PREF_CHARGE_PIN_KEY, RESISTOR_OPTIONS[0].chargePin);
   uint16_t dischargeDelayMicros = arduino.preferences.getUShort(PREF_DISCHARGE_KEY, CapacitorSensor::DEFAULT_DISCHARGE_DELAY_MICROS);
   uint32_t bufferSize = arduino.preferences.getUInt(PREF_BUFFER_KEY, CapacitorSensor::DEFAULT_BUFFER_SIZE);
   arduino.preferences.end();

   if (!hasBest)
   {
      Serial.println("No saved best configuration found.");
      return true;
   }

   if (!isKnownChargePin(chargePin))
   {
      chargePin = RESISTOR_OPTIONS[0].chargePin;
   }
   if (dischargeDelayMicros == 0)
   {
      dischargeDelayMicros = CapacitorSensor::DEFAULT_DISCHARGE_DELAY_MICROS;
   }
   if (bufferSize < MIN_TARGET_BUFFER_SIZE || bufferSize > MAX_TARGET_BUFFER_SIZE)
   {
      bufferSize = CapacitorSensor::DEFAULT_BUFFER_SIZE;
   }

   applyConfiguration(chargePin, dischargeDelayMicros, (size_t)bufferSize);

   const SerialTable::Column columns[] = {
      { "Field", 18 },
      { "Value", 24 },
   };
   SerialTable table("Loaded Saved Best Configuration", columns, sizeof(columns) / sizeof(columns[0]));
   SerialX::print(table.printHeader());
   SerialX::print(table.printRow("Resistor Value", String(resistorLabel(chargePin)) + " (Pin " + String((unsigned long)chargePin) + ")"));
   SerialX::print(table.printRow("Discharge Delay", String((unsigned long)dischargeDelayMicros) + " us"));
   SerialX::print(table.printRow("Buffer Size", (unsigned long)bufferSize));
   Serial.println();

   return true;
}

///
/// <summary>Target rate to be tested during a calibration sweep.</summary>
///
struct TestCase
{
   float targetEffectiveRate = NAN;
   uint16_t dischargeDelayMicros = 0;
};

///
/// <summary>Configuration and measured statistics captured from a single calibration test run.</summary>
///
struct TestRunResult
{
   const char* resistorLabel = nullptr;
   float targetEffectiveRate = NAN;
   uint16_t dischargeDelayMicros = 0;
   float averageChargeTimeMicros = NAN;
   float chargeTimeVariationMicros = NAN;
   float chargeStdDevMicros = NAN;
   size_t bufferSize = 0;
   float rawRateHz = NAN;
   float effectiveRateHz = NAN;
};

///
/// <summary>Runs a single calibration test case against a capacitor sensor and collects the resulting statistics.</summary>
///
class TestRun
{
private:
   CapacitorSensor& _sensor;

public:
   ///
   /// <summary>Create a test run for the given sensor.</summary>
   /// <param name="sensor">Reference to the capacitor sensor to test</param>
   ///
   explicit TestRun(CapacitorSensor& sensor)
      : _sensor(sensor)
   {
   }

   ///
   /// <summary>Execute a single test run with the specified parameters.</summary>
   /// <param name="targetEffectiveRate">Target effective output rate in Hz</param>
   /// <param name="dischargeDelayMicros">Discharge delay in microseconds</param>
   /// <returns>Test result data structure</returns>
   ///
   TestRunResult run(float targetEffectiveRate, uint16_t dischargeDelayMicros)
   {
      TestRunResult result;
      result.targetEffectiveRate = targetEffectiveRate;
      result.dischargeDelayMicros = dischargeDelayMicros;

      // Stabilize the sensor at the requested discharge delay, then read the rate
      _sensor.setDischargeDelayMicros(dischargeDelayMicros);
      delay(100);

      float rawRate = _sensor.rate();
      result.rawRateHz = rawRate;

      // Calculate target buffer size based on raw rate and target effective rate
      size_t targetBufferSize = _calculateTargetBufferSize(rawRate, targetEffectiveRate);
      result.bufferSize = targetBufferSize;

      // Configure sensor buffer for this test and collect output samples for a fixed time period
      _sensor.setBufferSize(targetBufferSize);
      TimedStats stats(STATS_PERIOD_MS);
      float measuredAverageMin = NAN;
      float measuredAverageMax = NAN;
      size_t sampleCount = 0;

      uint32_t lastCounter = _sensor.counter();
      Stopwatch stopwatch;
      stopwatch.start();
      while (stopwatch.elapsedMillis() < STATS_PERIOD_MS)
      {
         uint32_t counter = _sensor.counter();
         if (counter == lastCounter)
         {
            continue;
         }

         lastCounter = counter;
         float chargeTime = _sensor.chargeTimeMicros();
         if (isfinite(chargeTime))
         {
            stats.set(chargeTime);
            sampleCount++;

            float average = stats.average();
            if (isfinite(average))
            {
               if (!isfinite(measuredAverageMin) || average < measuredAverageMin)
               {
                  measuredAverageMin = average;
               }

               if (!isfinite(measuredAverageMax) || average > measuredAverageMax)
               {
                  measuredAverageMax = average;
               }
            }
         }
      }

      // Compile results
      result.averageChargeTimeMicros = stats.average();
      result.chargeStdDevMicros = stats.stdDev();
      if (isfinite(measuredAverageMin) && isfinite(measuredAverageMax))
      {
         result.chargeTimeVariationMicros = measuredAverageMax - measuredAverageMin;
      }

      float finalRawRate = _sensor.rate();
      result.effectiveRateHz = (result.bufferSize > 0) ? (finalRawRate / result.bufferSize) : 0;

      _sensor.setBufferSize(CapacitorSensor::DEFAULT_BUFFER_SIZE);

      return result;
   }

private:
   ///
   /// <summary>Calculate target buffer size based on raw rate and target effective rate.</summary>
   /// <param name="rawRate">Raw sensor rate in Hz</param>
   /// <param name="targetEffectiveRate">Target effective output rate in Hz</param>
   /// <returns>Computed buffer size constrained to valid range</returns>
   ///
   size_t _calculateTargetBufferSize(float rawRate, float targetEffectiveRate)
   {
      if (!isfinite(rawRate) || rawRate <= 0.0f)
      {
         return CapacitorSensor::DEFAULT_BUFFER_SIZE;
      }

      float targetBufferSize = rawRate / targetEffectiveRate;
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
};

/// <summary>Stores all test case parameters to be executed.</summary>
struct TestCaseParameters
{
   /// <summary>Array of test case parameters.</summary>
   TestCase cases[TARGET_EFFECTIVE_RATE_COUNT * OPTIMIZATION_DISCHARGE_DELAY_COUNT];
   /// <summary>Number of test cases.</summary>
   size_t count = 0;
};

TestCaseParameters testParameters;

///
/// <summary>
/// Applies the current editable field values (resistor/discharge/buffer) to the sensor.
/// </summary>
///
void applyFields()
{
   long selectedResistorIndex = constrain(resistorIndex, 0L, (long)(RESISTOR_OPTION_COUNT - 1));
   applyConfiguration(
      RESISTOR_OPTIONS[selectedResistorIndex].chargePin,
      (uint16_t)dischargeDelayMicros,
      (size_t)bufferSize);
   capacitorSensor.setFilter(filter, FilteredRollingAverage::FilterMode::PERCENT);
}

///
/// <summary>
/// Draws the heading and the given subheading for the log viewer, splits the remaining
/// space above the footer prompt between the text viewer (top) and the results plots
/// (bottom), splits the bottom plot area itself between a scatter plot (left) and a
/// histogram (right), and switches into viewer mode. When showPlots is false, the text viewer
/// fills the whole area instead and the plot rects are cleared so renderResultsScatterPlot()
/// has nothing to draw into (used for results that are aggregated summaries rather than raw
/// sample data, e.g. the Optimization Sweep).
/// </summary>
/// <param name="subheading">Subheading text describing which test's results are being shown (e.g. "Buffer Size Sweep Test Results...").</param>
/// <param name="showPlots">Whether to reserve space for the scatter plot/histogram below the text viewer.</param>
///
void initializeResultsView(const String& subheading, bool showPlots = true)
{
   arduino.clearDisplay();

   arduino.setCursor(0, 0);
   arduino.setTextSize(DEFAULT_HEADING_SIZE);
   arduino.println("Capacitor Tuning", Color::HEADING);

   arduino.setTextSize(DEFAULT_TEXT_SIZE);
   arduino.println(subheading, Color::SUB_HEADING);
   arduino.moveCursorY(8);

   int16_t viewerTop = arduino.getCursorY();
   int16_t availableHeight = arduino.height() - arduino.charH() - viewerTop;

   if (!showPlots)
   {
      textViewer.setRect(Rect16{ 0, static_cast<uint16_t>(viewerTop), static_cast<uint16_t>(arduino.width()),
                                static_cast<uint16_t>(availableHeight) });
      resultsScatterRect = Rect16{ 0, 0, 0, 0 };
      resultsHistogramRect = Rect16{ 0, 0, 0, 0 };
      viewingResults = true;
      return;
   }

   constexpr float TEXT_VIEWER_HEIGHT_FRACTION = 0.55f;
   constexpr int16_t PLOT_GAP_PIXELS = 8;
   int16_t textViewerHeight = static_cast<int16_t>(availableHeight * TEXT_VIEWER_HEIGHT_FRACTION);

   textViewer.setRect(Rect16{ 0, static_cast<uint16_t>(viewerTop), static_cast<uint16_t>(arduino.width()),
                             static_cast<uint16_t>(textViewerHeight) });

   int16_t plotTop = viewerTop + textViewerHeight + PLOT_GAP_PIXELS;
   int16_t plotHeight = availableHeight - textViewerHeight - PLOT_GAP_PIXELS;
   int16_t plotWidth = arduino.width() - PLOT_GAP_PIXELS;
   int16_t scatterWidth = plotWidth / 2;
   int16_t histogramWidth = plotWidth - scatterWidth;

   resultsScatterRect = Rect16{ 0, static_cast<uint16_t>(plotTop), static_cast<uint16_t>(scatterWidth),
                                static_cast<uint16_t>(plotHeight) };
   resultsHistogramRect = Rect16{ static_cast<uint16_t>(scatterWidth + PLOT_GAP_PIXELS), static_cast<uint16_t>(plotTop),
                                  static_cast<uint16_t>(histogramWidth), static_cast<uint16_t>(plotHeight) };

   viewingResults = true;
}

///
/// <summary>
/// Renders a one-shot scatter plot of raw sample values (index on X, value on Y) into
/// resultsScatterRect and a histogram of the same values into resultsHistogramRect,
/// replacing any previously shown results plots. Downsamples the scatter plot to at most
/// RESULTS_SCATTER_MAX_POINTS points to bound render cost for large sample sets.
/// </summary>
/// <param name="values">Sample values to plot.</param>
/// <param name="count">Number of entries in values.</param>
///
void renderResultsScatterPlot(const float* values, size_t count)
{
   delete resultsScatterPlot;
   resultsScatterPlot = nullptr;

   delete resultsHistogram;
   resultsHistogram = nullptr;
   delete resultsHistogramPlot;
   resultsHistogramPlot = nullptr;

   if (count == 0)
   {
      return;
   }

   if (resultsScatterRect.width > 0 && resultsScatterRect.height > 0)
   {
      size_t plottedCount = min(count, RESULTS_SCATTER_MAX_POINTS);
      size_t step = (count + plottedCount - 1) / plottedCount;

      resultsScatterPlot = new ScatterPlot(&arduino, resultsScatterRect);
      resultsScatterPlot->setXAxisFormat(Format("#####"));
      resultsScatterPlot->setYAxisFormat(Format("###.#"));
      ScatterPlotSeries* series = resultsScatterPlot->createSeries(plottedCount);
      series->showPoints = true;

      for (size_t i = 0; i < count; i += step)
      {
         series->add(i, values[i]);
      }

      // Ensure the final sample is always plotted so the X-axis max reflects the true last index,
      // even when the step size doesn't evenly divide into count - 1.
      size_t lastIndex = count - 1;
      if ((lastIndex % step) != 0)
      {
         series->add(lastIndex, values[lastIndex]);
      }

      resultsScatterPlot->render();
   }

   if (resultsHistogramRect.width > 0 && resultsHistogramRect.height > 0)
   {
      resultsHistogram = new Histogram(values, count, RESULTS_HISTOGRAM_MIN_BINS, RESULTS_HISTOGRAM_MAX_BINS);
      resultsHistogramPlot = new HistogramPlot(&arduino, *resultsHistogram, resultsHistogramRect, Color::GREEN, Format("###.#"));
      resultsHistogramPlot->render();
   }
}

///
/// <summary>
/// Collects ROLLING_SWEEP_SAMPLE_COUNT raw charge-time samples
/// resistor/discharge configuration, updating the display only with the collected count to
/// minimize interference with the measurements. After collection, prints a table of range and
/// StdDev for each rolling window size in ROLLING_SWEEP_SIZES, computed from the same raw data.
/// </summary>
///
void runRollingSweepTest()
{
   static float samples[ROLLING_SWEEP_SAMPLE_COUNT];

   textViewer.clear();

   // Use a buffer size of 1 so every raw charge-time reading is captured individually.
   capacitorSensor.setBufferSize(1);

   size_t collected = 0;
   uint32_t lastCounter = capacitorSensor.counter();

   arduino.clearDisplay();
   arduino.setCursor(0, 0);
   arduino.setTextSize(DEFAULT_HEADING_SIZE);
   int16_t headerRowY = arduino.getCursorY();
   arduino.println(String("Buffer Size Sweep: ") + String((int)(100.0f * collected / ROLLING_SWEEP_SAMPLE_COUNT)) + "%", Color::HEADING);
   arduino.setTextSize(DEFAULT_TEXT_SIZE);
   arduino.moveCursorY(8);

   long selectedResistorIndex = constrain(resistorIndex, 0L, (long)(RESISTOR_OPTION_COUNT - 1));
   arduino.println("      Resistor: ", RESISTOR_OPTIONS[selectedResistorIndex].label, resistorLabelFormat, Color::VALUE);
   arduino.println("         Delay: ", (int)dischargeDelayMicros, chargeFormat, Color::VALUE);
   arduino.println("Outlier Filter: ", filter, filterFieldFormat, Color::VALUE);

   Timer collectDisplayTimer(0);
   while (collected < ROLLING_SWEEP_SAMPLE_COUNT)
   {
      uint32_t counter = capacitorSensor.counter();
      if (counter == lastCounter)
      {
         yield();
         continue;
      }

      lastCounter = counter;
      float chargeTime = capacitorSensor.chargeTimeMicros();
      if (isfinite(chargeTime))
      {
         samples[collected] = chargeTime;
         collected++;

         // Only update the display periodically with the collected count to minimize interference.
         if ((collected % ROLLING_SWEEP_DISPLAY_UPDATE_INTERVAL) == 0 || collected == ROLLING_SWEEP_SAMPLE_COUNT)
         {
            arduino.setCursorY(headerRowY);
            arduino.setTextSize(DEFAULT_HEADING_SIZE);
            arduino.println(String("Buffer Size Sweep: ") + String((int)(100.0f * collected / ROLLING_SWEEP_SAMPLE_COUNT)) + "%", Color::HEADING);
            arduino.setTextSize(DEFAULT_TEXT_SIZE);
         }
      }
   }

   Serial.println();
   Serial.println("Rolling Sweep: computing stats...");

   textViewer.addLine(String("Resistor: ") + RESISTOR_OPTIONS[selectedResistorIndex].label);
   textViewer.addLine(String("Discharge Delay: ") + String(dischargeDelayMicros) + " us");
   textViewer.addLine(String("Outlier Filter: ") + String(filter, 1) + " %");
   textViewer.addLine("");

   const SerialTable::Column columns[] = {
      { "Buffer Size", 14 },
      { "Range(us)", 12 },
      { "StdDev(us)", 12 },
      { "StdDev %", 10 },
   };
   SerialTable table(nullptr, columns, sizeof(columns) / sizeof(columns[0]));
   String headerText = table.printHeader();
   textViewer.addText(headerText);

   for (size_t sizeIndex = 0; sizeIndex < ROLLING_SWEEP_SIZE_COUNT; sizeIndex++)
   {
      size_t rollingSize = ROLLING_SWEEP_SIZES[sizeIndex];
      Stats rollingAverageStats;
      RollingAverage rollingAverage(rollingSize);

      for (size_t i = 0; i < ROLLING_SWEEP_SAMPLE_COUNT; i++)
      {
         rollingAverage.set(samples[i]);
         if (rollingAverage.count() == rollingSize)
         {
            rollingAverageStats.add(rollingAverage.average());
         }
      }

      float rangeMicros = rollingAverageStats.max() - rollingAverageStats.min();
      float stdDevPercent = (rollingAverageStats.get() != 0) ? (rollingAverageStats.stdDev() / rollingAverageStats.get() * 100.0f) : 0.0f;
      String rowText = table.printRow(
         (unsigned long)rollingSize,
         SerialTable::fixed(rangeMicros, 3),
         SerialTable::fixed(rollingAverageStats.stdDev(), 3),
         SerialTable::fixed(stdDevPercent, 2));
      textViewer.addText(rowText);
   }

   Serial.println();
   Serial.println("Rolling Sweep Complete");

   capacitorSensor.setBufferSize((size_t)bufferSize);
   initializeResultsView("Buffer Size Sweep Test Results...", false);
}

///
/// <summary>
/// Temporarily grows the sensor's rolling buffer to RAW_DATA_CAPTURE_SAMPLE_COUNT so that it
/// accumulates that many raw charge-time samples using the currently selected configuration,
/// updating the display only with the collected count to minimize interference with the
/// measurements. Once full, stops background measurement so the buffer is stable, reads the
/// actual buffered sensor values back via rawValueAt(), and prints a confidence table to serial
/// showing how many (and what percent) of the samples fall within avg +/- range, for each
/// percent range in RAW_DATA_CAPTURE_CONFIDENCE_PERCENTS. Measurement restarts afterward.
/// </summary>
///
void runRawDataCaptureTest()
{
   static float samples[RAW_DATA_CAPTURE_SAMPLE_COUNT];

   textViewer.clear();

   // Temporarily grow the sensor's rolling buffer to hold RAW_DATA_CAPTURE_SAMPLE_COUNT raw
   // charge-time readings, then wait for the buffer to fill so the actual sensor values can be
   // read back afterward via rawValueAt().
   capacitorSensor.setBufferSize(RAW_DATA_CAPTURE_SAMPLE_COUNT);

   size_t collected = 0;

   arduino.clearDisplay();
   arduino.setCursor(0, 0);
   arduino.setTextSize(DEFAULT_HEADING_SIZE);
   int16_t headerRowY = arduino.getCursorY();
   arduino.println(String("Raw Data Capture: ") + String((int)(100.0f * collected / RAW_DATA_CAPTURE_SAMPLE_COUNT)) + "%", Color::HEADING);
   arduino.setTextSize(DEFAULT_TEXT_SIZE);
   arduino.moveCursorY(8);

   long selectedResistorIndex = constrain(resistorIndex, 0L, (long)(RESISTOR_OPTION_COUNT - 1));
   arduino.println("      Resistor: ", RESISTOR_OPTIONS[selectedResistorIndex].label, resistorLabelFormat, Color::VALUE);
   arduino.println("Discharge Time: ", (int)dischargeDelayMicros, chargeFormat, Color::VALUE);
   arduino.println("Outlier Filter: ", filter, filterFieldFormat, Color::VALUE);

   // Abort the capture if no new samples arrive for this long, e.g. when a too-strict outlier
   // filter causes every incoming sample to be rejected and the buffer never fills.
   constexpr double RAW_DATA_CAPTURE_STALL_TIMEOUT_MS = 500.0;
   Stopwatch stallStopwatch;
   size_t previousCollected = collected;
   bool stalled = false;

   while (collected < RAW_DATA_CAPTURE_SAMPLE_COUNT)
   {
      collected = capacitorSensor.rawValueCount();

      if (collected != previousCollected)
      {
         previousCollected = collected;
         stallStopwatch.reset();
      }
      else if (stallStopwatch.elapsedMillis() >= RAW_DATA_CAPTURE_STALL_TIMEOUT_MS)
      {
         stalled = true;
         break;
      }

      // Only update the display periodically with the collected count to minimize interference.
      if ((collected % RAW_DATA_CAPTURE_DISPLAY_UPDATE_INTERVAL) == 0 || collected == RAW_DATA_CAPTURE_SAMPLE_COUNT)
      {
         arduino.setCursorY(headerRowY);
         arduino.setTextSize(DEFAULT_HEADING_SIZE);
         arduino.println(String("Raw Data Capture: ") + String((int)(100.0f * collected / RAW_DATA_CAPTURE_SAMPLE_COUNT)) + "%", Color::HEADING);
         arduino.setTextSize(DEFAULT_TEXT_SIZE);
      }

      yield();
   }

   // Stop background measurement so the buffer contents are stable while we read them all back.
   capacitorSensor.stop();

   if (stalled)
   {
      Serial.println();
      Serial.println("Raw Data Capture: aborted, no new samples arrived (outlier filter may be rejecting all readings).");

      textViewer.addLine("Raw Data Capture aborted:");
      textViewer.addLine("No new samples arrived for " + String((int)RAW_DATA_CAPTURE_STALL_TIMEOUT_MS) + " ms.");
      textViewer.addLine("The outlier filter may be too strict, rejecting all readings.");

      capacitorSensor.setBufferSize((size_t)bufferSize);
      capacitorSensor.start();
      initializeResultsView("Raw Data Capture Test Results...");
      return;
   }

   Serial.println();
   Serial.println("Raw Data Capture: computing stats...");

   textViewer.addLine(String("Resistor: ") + RESISTOR_OPTIONS[selectedResistorIndex].label);
   textViewer.addLine(String("Discharge Time: ") + String(dischargeDelayMicros) + " us");
   textViewer.addLine(String("Outlier Filter: ") + String(filter, 1));
   textViewer.addLine("");

   // rawValueAt() is indexed relative to the most recent sample (0 = latest), so read samples
   // in reverse to store them in samples[] in true chronological order (0 = first captured).
   Stats captureStats;
   for (size_t i = 0; i < RAW_DATA_CAPTURE_SAMPLE_COUNT; i++)
   {
      samples[i] = capacitorSensor.rawValueAt(RAW_DATA_CAPTURE_SAMPLE_COUNT - 1 - i);
      captureStats.add(samples[i]);
   }

   float average = captureStats.get();
   textViewer.addLine(String("Average: ") + String(average, 3) + " us");
   textViewer.addLine(String("StdDev: ") + String(captureStats.stdDev(), 3) + " us");
   textViewer.addLine("");

   const SerialTable::Column columns[] = {
      { "Range", 5, "##.#%" },
      { "+-", 8, "##.#" },
      { "Min", 8, "##.#" },
      { "Max", 8, "##.#" },
      { "Count", 8 },
      { "Percent", 9, "##.##%" },
   };
   SerialTable table(nullptr, columns, sizeof(columns) / sizeof(columns[0]));
   String headerText = table.printHeader();
   textViewer.addText(headerText);

   for (size_t rangeIndex = 0; rangeIndex < RAW_DATA_CAPTURE_CONFIDENCE_COUNT; rangeIndex++)
   {
      float percent = RAW_DATA_CAPTURE_CONFIDENCE_PERCENTS[rangeIndex];
      float toleranceMicros = average * (percent / 100.0f);
      float rangeMin = average - toleranceMicros;
      float rangeMax = average + toleranceMicros;

      size_t withinRange = 0;
      for (size_t i = 0; i < RAW_DATA_CAPTURE_SAMPLE_COUNT; i++)
      {
         if (fabsf(samples[i] - average) <= toleranceMicros)
         {
            withinRange++;
         }
      }

      float withinRangePercent = 100.0f * withinRange / RAW_DATA_CAPTURE_SAMPLE_COUNT;
         String rowText = table.printRow(
            percent,
            toleranceMicros,
            rangeMin,
            rangeMax,
            (unsigned long)withinRange,
            withinRangePercent);
         textViewer.addText(rowText);
      }

      textViewer.addLine("");

      const SerialTable::Column binColumns[] = {
         { "Bin (us)", 12 },
         { "Count", 8 },
      };
      SerialTable binTable(nullptr, binColumns, sizeof(binColumns) / sizeof(binColumns[0]));
      String binHeaderText = binTable.printHeader();
      textViewer.addText(binHeaderText);

      int32_t binMin = (int32_t)floorf(captureStats.min());
      int32_t binMax = (int32_t)ceilf(captureStats.max());
      for (int32_t bin = binMin; bin < binMax; bin++)
      {
         size_t binCount = 0;
         for (size_t i = 0; i < RAW_DATA_CAPTURE_SAMPLE_COUNT; i++)
         {
            if (samples[i] >= (float)bin && samples[i] < (float)(bin + 1))
            {
               binCount++;
            }
         }

         if (binCount == 0)
         {
            continue;
         }

         String binRowText = binTable.printRow(
            String(bin) + "-" + String(bin + 1),
            (unsigned long)binCount);
         textViewer.addText(binRowText);
      }

      Serial.println();
      Serial.println("Raw Data Capture Complete");

      capacitorSensor.setBufferSize((size_t)bufferSize);
      capacitorSensor.start();
      initializeResultsView("Raw Data Capture Test Results...");
      renderResultsScatterPlot(samples, RAW_DATA_CAPTURE_SAMPLE_COUNT);
   }

///
/// <summary>
/// Sweeps the discharge delay from DISCHARGE_SWEEP_MIN_MICROS to DISCHARGE_SWEEP_MAX_MICROS in
/// steps of DISCHARGE_SWEEP_STEP_MICROS. For each discharge delay, collects
/// DISCHARGE_SWEEP_SAMPLE_COUNT raw charge-time samples using the currently selected resistor,
/// updating the display only with the collected count to minimize interference with the
/// measurements. After each delay's samples are collected, prints a table row of range and
/// StdDev for that discharge delay.
/// </summary>
///
void runDischargeSweepTest()
{
   static float samples[DISCHARGE_SWEEP_SAMPLE_COUNT];

   textViewer.clear();

   // Use the currently selected buffer size so results reflect the live averaging configuration.
   capacitorSensor.setBufferSize((size_t)bufferSize);

   const size_t totalSamples = DISCHARGE_SWEEP_STEP_COUNT * DISCHARGE_SWEEP_SAMPLE_COUNT;
   size_t totalCollected = 0;

   arduino.clearDisplay();
   arduino.setCursor(0, 0);
   arduino.setTextSize(DEFAULT_HEADING_SIZE);
   int16_t headerRowY = arduino.getCursorY();
   arduino.println(String("Discharge Time Sweep: ") + String((int)(100.0f * totalCollected / totalSamples)) + "%", Color::HEADING);
   arduino.setTextSize(DEFAULT_TEXT_SIZE);
   arduino.moveCursorY(8);

   long selectedResistorIndex = constrain(resistorIndex, 0L, (long)(RESISTOR_OPTION_COUNT - 1));
   arduino.println("      Resistor: ", RESISTOR_OPTIONS[selectedResistorIndex].label, resistorLabelFormat, Color::VALUE);
   arduino.println("        Buffer: ", (int)bufferSize, bufferFieldFormat, Color::VALUE);
   arduino.println("Outlier Filter: ", filter, filterFieldFormat, Color::VALUE);

   textViewer.addLine(String("Resistor: ") + RESISTOR_OPTIONS[selectedResistorIndex].label);
   textViewer.addLine(String("Buffer: ") + String(bufferSize));
   textViewer.addLine(String("Outlier Filter: ") + String(filter, 1) + " %");
   textViewer.addLine("");

   const SerialTable::Column columns[] = {
      { "Delay(us)", 12 },
      { "Range(us)", 12 },
      { "StdDev(us)", 12 },
   };
   SerialTable table(nullptr, columns, sizeof(columns) / sizeof(columns[0]));
   String headerText = table.printHeader();
   textViewer.addText(headerText);

   for (uint16_t sweepDischargeDelayMicros = DISCHARGE_SWEEP_MIN_MICROS; sweepDischargeDelayMicros <= DISCHARGE_SWEEP_MAX_MICROS; sweepDischargeDelayMicros += DISCHARGE_SWEEP_STEP_MICROS)
   {
      capacitorSensor.setDischargeDelayMicros(sweepDischargeDelayMicros);

      size_t collected = 0;
      uint32_t lastCounter = capacitorSensor.counter();
      while (collected < DISCHARGE_SWEEP_SAMPLE_COUNT)
      {
         uint32_t counter = capacitorSensor.counter();
         if (counter == lastCounter)
         {
            yield();
            continue;
         }

         lastCounter = counter;
         float chargeTime = capacitorSensor.chargeTimeMicros();
         if (isfinite(chargeTime))
         {
            samples[collected] = chargeTime;
            collected++;
            totalCollected++;

            // Only update the display periodically with the collected count to minimize interference.
            if ((totalCollected % DISCHARGE_SWEEP_DISPLAY_UPDATE_INTERVAL) == 0 || totalCollected == totalSamples)
            {
               arduino.setCursorY(headerRowY);
               arduino.setTextSize(DEFAULT_HEADING_SIZE);
               arduino.println(String("Discharge Time Sweep: ") + String((int)(100.0f * totalCollected / totalSamples)) + "%", Color::HEADING);
               arduino.setTextSize(DEFAULT_TEXT_SIZE);
            }
         }
      }

      Stats sweepStats;
      for (size_t i = 0; i < DISCHARGE_SWEEP_SAMPLE_COUNT; i++)
      {
         sweepStats.add(samples[i]);
      }

      float rangeMicros = sweepStats.max() - sweepStats.min();
      String rowText = table.printRow(
         (unsigned long)sweepDischargeDelayMicros,
         SerialTable::fixed(rangeMicros, 3),
         SerialTable::fixed(sweepStats.stdDev(), 3));
      textViewer.addText(rowText);
   }

   Serial.println();
   Serial.println("Discharge Sweep Complete");

   capacitorSensor.setDischargeDelayMicros((uint16_t)dischargeDelayMicros);
   capacitorSensor.setBufferSize((size_t)bufferSize);
   initializeResultsView("Discharge Time Sweep Test Results...", false);
}

///
/// <summary>
/// Runs the automated optimization sweep: iterates over every resistor/target-rate combination,
/// captures statistics for each, prints results immediately and in aggregate, then applies and
/// persists the best configuration for the primary target rate.
/// </summary>
///
void runOptimizedSweepTest()
{
   textViewer.clear();

   Serial.println();
   Serial.println("Starting tests...");
   Serial.println();

   TestRunResult allResults[MAX_TEST_RESULTS];
   size_t allResultCount = 0;

   const SerialTable::Column resultColumns[] = {
      { "Resistor", 10 },
      { "Target", 10 },
      { "Delay", 8 },
      { "Buffer", 8 },
      { "Avg(us)", 12 },
      { "Range(us)", 12 },
      { "StdDev %", 10 },
      { "Raw", 10 },
      { "Effective", 11 },
   };
   SerialTable resultsTable(nullptr, resultColumns, sizeof(resultColumns) / sizeof(resultColumns[0]));
   String resultsHeaderText = resultsTable.printHeader();
   textViewer.addText(resultsHeaderText);

   const size_t totalTests = RESISTOR_OPTION_COUNT * testParameters.count;
   arduino.clearDisplay();

   // Outer loop: test every resistor value (charge pin)
   for (size_t resistorIndex = 0; resistorIndex < RESISTOR_OPTION_COUNT; resistorIndex++)
   {
      capacitorSensor.setChargePin(RESISTOR_OPTIONS[resistorIndex].chargePin);

      // Execute all test cases for this resistor value
      for (size_t i = 0; i < testParameters.count; i++)
      {
         size_t currentTest = resistorIndex * testParameters.count + i + 1;

         // Update display with current test parameters and progress
         arduino.setCursor(0, 0);
         arduino.setTextSize(DEFAULT_HEADING_SIZE);
         arduino.println(String("Optimization Sweep: Test ") + String(currentTest) + "/" + String(totalTests), Color::HEADING);
         arduino.setTextSize(DEFAULT_TEXT_SIZE);
         arduino.moveCursorY(8);
         arduino.println("    Resistor: ", RESISTOR_OPTIONS[resistorIndex].label, resistorLabelFormat, Color::VALUE);
         arduino.println("      Target: ", String((int)round(testParameters.cases[i].targetEffectiveRate)) + "/s", Color::VALUE);
         arduino.println("       Delay: ", (int)testParameters.cases[i].dischargeDelayMicros, chargeFormat, Color::VALUE);

         TestRun testRun(capacitorSensor);
         TestRunResult result = testRun.run(testParameters.cases[i].targetEffectiveRate, testParameters.cases[i].dischargeDelayMicros);
         result.resistorLabel = RESISTOR_OPTIONS[resistorIndex].label;
         allResults[allResultCount++] = result;

         // Print result immediately after test completes
         String avgText = isfinite(result.averageChargeTimeMicros) ? String(result.averageChargeTimeMicros, 1) + " us" : "----";
         String rangeText = isfinite(result.chargeTimeVariationMicros) ? String(result.chargeTimeVariationMicros, 2) + " us" : "----";
         String stdDevPctText = "----";
         if (isfinite(result.chargeStdDevMicros) && isfinite(result.averageChargeTimeMicros) && result.averageChargeTimeMicros != 0.0f)
         {
            float chargeStdDevPercent = (result.chargeStdDevMicros / result.averageChargeTimeMicros) * 100.0f;
            stdDevPctText = String(chargeStdDevPercent, 2) + " %";
         }

         String resultRowText = resultsTable.printRow(
            RESISTOR_OPTIONS[resistorIndex].label,
            String((int)round(result.targetEffectiveRate)) + "/s",
            (unsigned long)result.dischargeDelayMicros,
            (unsigned long)result.bufferSize,
            avgText,
            rangeText,
            stdDevPctText,
            String((int)round(result.rawRateHz)) + "/s",
            String((int)round(result.effectiveRateHz)) + "/s");
         textViewer.addText(resultRowText);

         // Yield to allow display and other updates
         delay(10);
      }
   }

   printAggregateStatistics(allResults, allResultCount);
   printBestConfigurations(allResults, allResultCount);
   Serial.println();
   Serial.println("Testing Complete");

   // Apply and persist the best configuration for the primary target rate
   const TestRunResult* best = findBestResult(allResults, allResultCount, PRIMARY_TARGET_EFFECTIVE_RATE);
   if (best != nullptr)
   {
         uint8_t bestChargePin = chargePinFromResistorLabel(best->resistorLabel);
         applyConfiguration(
            bestChargePin,
            best->dischargeDelayMicros,
            best->bufferSize);
         syncFields(
            bestChargePin,
            best->dischargeDelayMicros,
            best->bufferSize);
         saveBestConfiguration(
            bestChargePin,
            best->dischargeDelayMicros,
            best->bufferSize);
      }
      else
      {
         applyConfiguration(
            RESISTOR_OPTIONS[0].chargePin,
            CapacitorSensor::DEFAULT_DISCHARGE_DELAY_MICROS,
            CapacitorSensor::DEFAULT_BUFFER_SIZE);
      }

   initializeResultsView("Optimization Sweep Test Results...", false);
}

///
/// <summary>
/// Lazily creates the charge-time scatter plot and its data series, sized to the given
/// rectangle. The plot itself is created once (on the first call) and reused thereafter;
/// subsequent calls do nothing.
/// </summary>
/// <param name="plotRect">Screen rectangle the plot should fill.</param>
///
void ensureChargeScatterPlot(Rect16 plotRect)
{
   if (chargeScatterPlot != nullptr)
   {
      return;
   }

   chargeScatterPlot = new TimedScatterPlot(&arduino, plotRect, SCATTER_HISTORY_S * 1000UL, chargeFormat);
   chargeScatterSeries = chargeScatterPlot->createSeries();
   chargeScatterSeries->showPoints = true;
   chargeScatterPlot->setColors(Color::BLACK, Color::BLACK, Color::GRAY, Color::LABEL);
}

void setup()
{
   SerialX::begin();
   arduino.begin();
   capacitorSensor.begin();

   arduino.setTextSize(DEFAULT_TEXT_SIZE);

   initializeFieldSections();
   table.load();
   applyFields();

   size_t caseIndex = 0;
   for (size_t rateIndex = 0; rateIndex < TARGET_EFFECTIVE_RATE_COUNT; rateIndex++)
   {
      float targetRate = TARGET_EFFECTIVE_RATES[rateIndex];

      for (size_t delayIndex = 0; delayIndex < OPTIMIZATION_DISCHARGE_DELAY_COUNT; delayIndex++)
      {
         testParameters.cases[caseIndex].targetEffectiveRate = targetRate;
         testParameters.cases[caseIndex].dischargeDelayMicros = OPTIMIZATION_DISCHARGE_DELAYS_MICROS[delayIndex];
         caseIndex++;
      }
   }

   testParameters.count = caseIndex;
}

///
/// <summary>Find the result with the lowest StdDev % for a given target rate.</summary>
/// <param name="results">Array of all test results</param>
/// <param name="count">Number of results in the array</param>
/// <param name="targetRate">Target effective rate to match</param>
/// <returns>Pointer to the best result, or nullptr if none found</returns>
///
const TestRunResult* findBestResult(TestRunResult* results, size_t count, float targetRate)
{
   const TestRunResult* best = nullptr;
   float bestPct = NAN;

   for (size_t i = 0; i < count; i++)
   {
      const TestRunResult& r = results[i];
      if (!isfinite(r.targetEffectiveRate) || fabs(r.targetEffectiveRate - targetRate) >= 0.01f)
         continue;
      if (!isfinite(r.chargeStdDevMicros) || !isfinite(r.averageChargeTimeMicros) || r.averageChargeTimeMicros == 0.0f)
         continue;

      float pct = (r.chargeStdDevMicros / r.averageChargeTimeMicros) * 100.0f;
      if (best == nullptr || pct < bestPct)
      {
         best = &results[i];
         bestPct = pct;
      }
   }

   return best;
}

///
/// <summary>Print the top 3 configurations per target rate ranked by lowest StdDev %.</summary>
/// <param name="results">Array of all test results</param>
/// <param name="count">Number of results in the array</param>
///
void printBestConfigurations(TestRunResult* results, size_t count)
{
   textViewer.addLine("");
   textViewer.addLine("------- Best Configurations (Top 3 Lowest StdDev %) -------");

   for (size_t rateIndex = 0; rateIndex < TARGET_EFFECTIVE_RATE_COUNT; rateIndex++)
   {
      float targetRate = TARGET_EFFECTIVE_RATES[rateIndex];

      // Collect indices of results matching this target rate
      size_t indices[RESISTOR_OPTION_COUNT];
      size_t indexCount = 0;
      for (size_t i = 0; i < count; i++)
      {
         if (isfinite(results[i].targetEffectiveRate) &&
             fabs(results[i].targetEffectiveRate - targetRate) < 0.01f)
         {
            indices[indexCount++] = i;
         }
      }

       // Sort by stddev % ascending (selection sort)
      for (size_t a = 0; a < indexCount; a++)
      {
         for (size_t b = a + 1; b < indexCount; b++)
         {
            const TestRunResult& ra = results[indices[a]];
            const TestRunResult& rb = results[indices[b]];
            float pctA = NAN, pctB = NAN;
            if (isfinite(ra.chargeStdDevMicros) && isfinite(ra.averageChargeTimeMicros) && ra.averageChargeTimeMicros != 0.0f)
               pctA = (ra.chargeStdDevMicros / ra.averageChargeTimeMicros) * 100.0f;
            if (isfinite(rb.chargeStdDevMicros) && isfinite(rb.averageChargeTimeMicros) && rb.averageChargeTimeMicros != 0.0f)
               pctB = (rb.chargeStdDevMicros / rb.averageChargeTimeMicros) * 100.0f;

            bool shouldSwap = (!isfinite(pctA) && isfinite(pctB)) ||
                              (isfinite(pctA) && isfinite(pctB) && pctB < pctA);
            if (shouldSwap)
            {
               size_t tmp = indices[a];
               indices[a] = indices[b];
               indices[b] = tmp;
            }
         }
      }

      Serial.println();
      String targetLine = "  Target: " + String((int)round(targetRate)) + "/s";
      textViewer.addLine("");
      textViewer.addLine(targetLine);

      const SerialTable::Column columns[] = {
         { "Rank", 6 },
         { "Resistor (Pin)", 18 },
         { "Delay", 8 },
         { "Buffer", 8 },
         { "StdDev %", 10 },
      };
      SerialTable table("Best Configurations", columns, sizeof(columns) / sizeof(columns[0]));
      String headerText = table.printHeader();
      textViewer.addText(headerText);

      constexpr size_t TOP_RANKING_COUNT = 3;
      size_t printCount = (indexCount < TOP_RANKING_COUNT) ? indexCount : TOP_RANKING_COUNT;
      for (size_t rank = 0; rank < printCount; rank++)
      {
         const TestRunResult& r = results[indices[rank]];
         uint8_t resistorPin = chargePinFromResistorLabel(r.resistorLabel);
         String stdDevPct = "----";
         if (isfinite(r.chargeStdDevMicros) && isfinite(r.averageChargeTimeMicros) && r.averageChargeTimeMicros != 0.0f)
         {
            float pct = (r.chargeStdDevMicros / r.averageChargeTimeMicros) * 100.0f;
            stdDevPct = String(pct, 2) + " %";
         }

         String rowText = table.printRow(
            (unsigned long)(rank + 1),
            String(r.resistorLabel ? r.resistorLabel : "?") + " (" + String((unsigned long)resistorPin) + ")",
            (unsigned long)r.dischargeDelayMicros,
            (unsigned long)r.bufferSize,
            stdDevPct);
         textViewer.addText(rowText);
      }

      textViewer.addLine("");
   }
}

///
/// <summary>Prints a single aggregate statistics row if any samples were collected.</summary>
/// <param name="table">Table to print the row to</param>
/// <param name="label">Row label</param>
/// <param name="avgStats">Aggregated average-charge-time statistics</param>
/// <param name="stdDevStats">Aggregated StdDev statistics</param>
///
void printAggregateRow(SerialTable& table, const char* label, const Stats& avgStats, const Stats& stdDevStats)
{
   if (avgStats.count() == 0)
   {
      return;
   }

   float rangeMicros = avgStats.max() - avgStats.min();
   float avgMicros = avgStats.get();
   float stdDevPercent = (isfinite(stdDevStats.get()) && avgMicros != 0.0f) ? (stdDevStats.get() / avgMicros) * 100.0f : NAN;
   String rowText = table.printRow(
      label,
      SerialTable::fixed(avgMicros, 3),
      SerialTable::fixed(stdDevStats.get(), 3),
      SerialTable::fixed(stdDevPercent, 2),
      SerialTable::fixed(rangeMicros, 3));
   textViewer.addText(rowText);
}

///
/// <summary>Prints aggregate charge-time statistics grouped by resistor value.</summary>
/// <param name="results">Array of all test results</param>
/// <param name="count">Number of results in the array</param>
///
void printAggregateByResistor(const TestRunResult* results, size_t count)
{
   const SerialTable::Column columns[] = {
      { "Value", 10 },
      { "Avg(us)", 12 },
      { "StdDev(us)", 12 },
      { "StdDev %", 10 },
      { "Range(us)", 12 },
   };
   SerialTable table("By Resistor", columns, sizeof(columns) / sizeof(columns[0]));
   String headerText = table.printHeader();
   textViewer.addText(headerText);

   for (size_t resistorIndex = 0; resistorIndex < RESISTOR_OPTION_COUNT; resistorIndex++)
   {
      Stats avgStats;
      Stats stdDevStats;
      for (size_t i = 0; i < count; i++)
      {
         const TestRunResult& r = results[i];
         if (r.resistorLabel == nullptr || strcmp(r.resistorLabel, RESISTOR_OPTIONS[resistorIndex].label) != 0)
         {
            continue;
         }

         if (isfinite(r.averageChargeTimeMicros))
         {
            avgStats.add(r.averageChargeTimeMicros);
         }
         if (isfinite(r.chargeStdDevMicros))
         {
            stdDevStats.add(r.chargeStdDevMicros);
         }
      }

      printAggregateRow(table, RESISTOR_OPTIONS[resistorIndex].label, avgStats, stdDevStats);
   }

   textViewer.addLine("");
}

///
/// <summary>Prints aggregate charge-time statistics grouped by target effective rate.</summary>
/// <param name="results">Array of all test results</param>
/// <param name="count">Number of results in the array</param>
///
void printAggregateByTargetRate(const TestRunResult* results, size_t count)
{
   const SerialTable::Column columns[] = {
      { "Value", 10 },
      { "Avg(us)", 12 },
      { "StdDev(us)", 12 },
      { "StdDev %", 10 },
      { "Range(us)", 12 },
   };
   SerialTable table("By Target Rate", columns, sizeof(columns) / sizeof(columns[0]));
   String headerText = table.printHeader();
   textViewer.addText(headerText);

   for (size_t rateIndex = 0; rateIndex < TARGET_EFFECTIVE_RATE_COUNT; rateIndex++)
   {
      float targetRate = TARGET_EFFECTIVE_RATES[rateIndex];
      Stats avgStats;
      Stats stdDevStats;

      for (size_t i = 0; i < count; i++)
      {
         const TestRunResult& r = results[i];
         if (!isfinite(r.targetEffectiveRate) || fabs(r.targetEffectiveRate - targetRate) >= 0.01f)
         {
            continue;
         }

         if (isfinite(r.averageChargeTimeMicros))
         {
            avgStats.add(r.averageChargeTimeMicros);
         }
         if (isfinite(r.chargeStdDevMicros))
         {
            stdDevStats.add(r.chargeStdDevMicros);
         }
      }

      constexpr size_t LABEL_BUFFER_SIZE = 16;
      char label[LABEL_BUFFER_SIZE] = "";
      snprintf(label, sizeof(label), "%d/s", (int)round(targetRate));
      printAggregateRow(table, label, avgStats, stdDevStats);
   }

   textViewer.addLine("");
}

///
/// <summary>Prints aggregate charge-time statistics grouped into buffer-size buckets.</summary>
/// <param name="results">Array of all test results</param>
/// <param name="count">Number of results in the array</param>
///
void printAggregateByBufferSize(const TestRunResult* results, size_t count)
{
   const SerialTable::Column columns[] = {
      { "Value", 10 },
      { "N", 4 },
      { "Avg(us)", 12 },
      { "StdDev(us)", 12 },
      { "StdDev %", 10 },
      { "Range(us)", 12 },
   };
   SerialTable table("By Buffer Size", columns, sizeof(columns) / sizeof(columns[0]));
   String headerText = table.printHeader();
   textViewer.addText(headerText);

   if (count == 0)
   {
      textViewer.addLine("");
      return;
   }

   size_t minBufferSize = results[0].bufferSize;
   size_t maxBufferSize = results[0].bufferSize;
   for (size_t i = 1; i < count; i++)
   {
      if (results[i].bufferSize < minBufferSize)
      {
         minBufferSize = results[i].bufferSize;
      }
      if (results[i].bufferSize > maxBufferSize)
      {
         maxBufferSize = results[i].bufferSize;
      }
   }

   const size_t BUCKET_COUNT = 5;
   size_t span = (maxBufferSize - minBufferSize) + 1;
   for (size_t bucket = 0; bucket < BUCKET_COUNT; bucket++)
   {
      size_t start = minBufferSize + ((span * bucket) / BUCKET_COUNT);
      size_t end = minBufferSize + ((span * (bucket + 1)) / BUCKET_COUNT) - 1;
      if (bucket == BUCKET_COUNT - 1)
      {
         end = maxBufferSize;
      }
      if (end < start)
      {
         end = start;
      }

      Stats avgStats;
      Stats stdDevStats;
      size_t n = 0;

      for (size_t i = 0; i < count; i++)
      {
         const TestRunResult& r = results[i];
         if (r.bufferSize < start || r.bufferSize > end)
         {
            continue;
         }

         n++;
         if (isfinite(r.averageChargeTimeMicros))
         {
            avgStats.add(r.averageChargeTimeMicros);
         }
         if (isfinite(r.chargeStdDevMicros))
         {
            stdDevStats.add(r.chargeStdDevMicros);
         }
      }

      if (n == 0)
      {
         continue;
      }

      float rangeMicros = avgStats.max() - avgStats.min();
      float avgMicros = avgStats.get();
      float stdDevPercent = (isfinite(stdDevStats.get()) && avgMicros != 0.0f) ? (stdDevStats.get() / avgMicros) * 100.0f : NAN;
      constexpr size_t LABEL_BUFFER_SIZE = 16;
      char label[LABEL_BUFFER_SIZE] = "";
      snprintf(label, sizeof(label), "%lu-%lu", (unsigned long)start, (unsigned long)end);
      String rowText = table.printRow(
         label,
         (unsigned long)n,
         SerialTable::fixed(avgMicros, 3),
         SerialTable::fixed(stdDevStats.get(), 3),
         SerialTable::fixed(stdDevPercent, 2),
         SerialTable::fixed(rangeMicros, 3));
      textViewer.addText(rowText);
   }

   textViewer.addLine("");
}

///
/// <summary>Prints all aggregate statistics groupings (by resistor, target rate, and buffer size).</summary>
/// <param name="results">Array of all test results</param>
/// <param name="count">Number of results in the array</param>
///
void printAggregateStatistics(const TestRunResult* results, size_t count)
{
   textViewer.addLine("");
   textViewer.addLine("------- Aggregate Statistics -------");

   printAggregateByResistor(results, count);
   printAggregateByTargetRate(results, count);
   printAggregateByBufferSize(results, count);
}

void loop()
{
   if (!bestConfigLoaded)
   {
      bestConfigLoaded = loadBestConfiguration();
   }

   if (viewingResults)
   {
      if (arduino.buttonA.wasPressed())
      {
         viewingResults = false;
         arduino.clearDisplay();
         table.forceRedraw();
         measurementsTable.forceRedraw();
         forceDisplayUpdate = true;
         return;
      }

      int32_t scrollDelta = arduino.encoderA.delta();
      if (scrollDelta != 0)
      {
         textViewer.scrollBy(scrollDelta);
      }

      if (textViewer.needsRedraw())
      {
         textViewer.draw();

         arduino.setCursorY(arduino.height() - arduino.charH());
         arduino.printlnR("Button A: return to main display", Color::GRAY);
      }
      return;
   }

   updateChargeStats();

   if (chargeScatterSeries != nullptr)
   {
      chargeScatterSeries->add(capacitorSensor.chargeTimeMicros());
   }

   // Edit the resistor/discharge/buffer fields: Encoder A selects a field, Encoder B
   // adjusts it. Any change is immediately applied to the sensor and persisted, so the
   // charge time/rate readouts below update live as the values change.
   int32_t selectDelta = arduino.encoderA.delta();
   int32_t adjustDelta = arduino.encoderB.delta();
   bool fieldsChanged = (selectDelta != 0 || adjustDelta != 0);
   table.selectNext(selectDelta);
   table.adjustSelected(adjustDelta);

   if (arduino.encoderB.button.wasPressed())
   {
      // reset() already persists the defaults, so just re-apply them below.
      table.reset();
      applyFields();
      forceDisplayUpdate = true;
   }
   else if (fieldsChanged)
   {
      applyFields();
      table.save();
      forceDisplayUpdate = true;
   }

   if (arduino.buttonB.wasPressed())
   {
      switch (plotState)
      {
      case PlotState::OFF:
         plotState = PlotState::ON;
         break;
      case PlotState::ON:
         plotState = PlotState::ON_WITH_STATS;
         break;
      case PlotState::ON_WITH_STATS:
      default:
         plotState = PlotState::OFF;
         break;
      }

      if (chargeScatterSeries != nullptr)
      {
         bool showStats = (plotState == PlotState::ON_WITH_STATS);
         chargeScatterSeries->showMovingAverage = showStats;
         chargeScatterSeries->showStdDevBand = showStats;
         chargeScatterPlot->invalidate();
      }

      forceDisplayUpdate = true;
   }

   if (arduino.buttonA.wasPressed())
   {
      TestType selectedTestType = (TestType)constrain(testType, 0L, (long)(TEST_TYPE_COUNT - 1));
      switch (selectedTestType)
      {
      case TestType::BUFFER_SIZE_SWEEP:
         runRollingSweepTest();
         break;

      case TestType::DISCHARGE_TIME_SWEEP:
         runDischargeSweepTest();
         break;

      case TestType::RAW_DATA_CAPTURE:
         runRawDataCaptureTest();
         break;

      case TestType::OPTIMIZE:
      default:
         runOptimizedSweepTest();
         break;
      }
   }

   if ((displayTimer.ready() || forceDisplayUpdate) && !viewingResults)
   {
      forceDisplayUpdate = false;

      arduino.setCursor(0, 0);
      arduino.setTextSize(DEFAULT_HEADING_SIZE);
      arduino.println("Capacitor Tuning", Color::HEADING);

      arduino.setTextSize(DEFAULT_TEXT_SIZE);
      arduino.moveCursorY(8);

      measuredChargeTimeMicros = capacitorSensor.chargeTimeMicros();
      measuredStdDevMicros = chargeStats.stdDev();
      measuredRangeMicros = chargeStats.range();
      measuredRawRate = capacitorSensor.rate();
      measuredEffectiveRate = (capacitorSensor.bufferSize() > 0) ? (measuredRawRate / (float)capacitorSensor.bufferSize()) : 0;

      int16_t fieldsTop = arduino.getCursorY();
      table.setPosition(0, fieldsTop);
      table.draw();

      constexpr int16_t TABLE_GAP_PIXELS = 10;
      int16_t measurementsLeft = arduino.width() / 2 + TABLE_GAP_PIXELS / 2;
      measurementsTable.setPosition(measurementsLeft, fieldsTop);
      measurementsTable.draw();

      int16_t fieldsBottom = fieldsTop + max(table.height(), measurementsTable.height());

      constexpr size_t FOOTER_LINE_COUNT = 1;
      int16_t footerTop = arduino.height() - FOOTER_LINE_COUNT * arduino.charH();

      constexpr int16_t PLOT_GAP_PIXELS = 8;
      int16_t plotTop = fieldsBottom + PLOT_GAP_PIXELS;
      Rect16 plotRect{ 0, static_cast<uint16_t>(plotTop), static_cast<uint16_t>(arduino.width()),
                       static_cast<uint16_t>(footerTop - plotTop) };

      if (plotState != PlotState::OFF)
      {
         ensureChargeScatterPlot(plotRect);
         chargeScatterPlot->render();
      }
      else if (previousPlotState != PlotState::OFF)
      {
         // Plot was just turned off: erase its previous footprint.
         arduino.fillRect(plotRect.x, plotRect.y, plotRect.width, plotRect.height, Color::BLACK);
      }
      previousPlotState = plotState;

      arduino.setCursorY(footerTop);
      arduino.printlnR("Button A: run selected test   Button B: toggle plot", Color::SUB_LABEL);
   }
}

