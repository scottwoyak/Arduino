//
// Capacitor sensor tuner: displays real-time measurements and runs the selected test on Button A.
//
// Combines live monitoring with automated tests for CapacitorSensor. During normal runtime, the
// display shows active resistor selection, discharge delay, deferred processing period, buffer
// size, test type, average charge time, sample rate, and effective output rate.
//
// Test Type field selects which test Button A runs:
// - Optimize: covers resistor/charge pin options (e.g., 1M, 470K, 100K, 47K) and target effective
//   output rates (samples per second) using the fixed default discharge delay; the deferred
//   processing period is fixed at 500 microseconds. For each test case, the sensor is configured
//   (discharge delay + deferred period + buffer size), a fixed number of samples is captured, and
//   stats are computed (average charge time, variation, StdDev, raw rate, and effective rate).
//   Results are printed immediately to Serial in a fixed-width table, then ranked later by lowest
//   StdDev % per target rate. After the sweep, a "best configurations" summary is printed for each
//   target rate, and the best configuration for 30/s is automatically applied and persisted for
//   continued live viewing.
// - Buffer Size Sweep: collects 10,000 raw charge-time samples at the current resistor/discharge
//   configuration (updating the display only with the collected count to minimize interference),
//   then prints a table of range and StdDev for several rolling buffer sizes computed from the
//   same raw data.
//
// Button B is currently unused.
//
// Serial commands:
// - Send 'L' to reload and apply the last saved best configuration.
//
// Live editable fields (Encoder A selects a field, Encoder B adjusts it, Encoder B Button resets
// to defaults) are shown directly on the live monitoring screen, so changing a value immediately
// re-applies it to the sensor and the resulting charge time/rate readouts update live:
// - Resistor: which charge pin/resistor is active (1M, 470K, 100K, 47K).
// - Discharge: discharge delay in microseconds.
// - Buffer: rolling-average buffer size.
// - Test Type: which test Button A runs (Optimize or Buffer Size Sweep).
// Values are persisted to Preferences as they change and reloaded automatically on startup.
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
#include "SetupTable.h"
#include "SerialTable.h"
#include "Stats.h"
#include "TimedStats.h"
#include "SerialX.h"
#include "SetupField.h"
#include "DisplayTextView.h"
#include "Timer.h"

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
Format effectiveRateFormat("###/s");
Format sampleRateFormat("######/s");
Format resistorLabelFormat(4);
constexpr size_t DISPLAY_INTERVAL_MS = 200;
Timer displayTimer(DISPLAY_INTERVAL_MS);
bool forceDisplayUpdate = false; // Set true to force an immediate redraw outside the normal interval (e.g. on encoder changes).

// ----------- Test Output Log Viewer
DisplayTextView textViewer(&arduino, Rect16{ 0, 0, 0, 0 }, DEFAULT_TEXT_SIZE, Color::WHITE, Color::DARKGRAY);
bool viewingResults = false;

// ----------- Test Sweep Parameters
constexpr unsigned long STATS_PERIOD_MS = 2000;
constexpr float TARGET_EFFECTIVE_RATES[] = { 15.0f, 30.0f, 50.0f };
constexpr size_t TARGET_EFFECTIVE_RATE_COUNT = sizeof(TARGET_EFFECTIVE_RATES) / sizeof(TARGET_EFFECTIVE_RATES[0]);
constexpr float PRIMARY_TARGET_EFFECTIVE_RATE = 30.0f;

constexpr uint32_t FIXED_DEFERRED_PROCESSING_PERIOD_MICROS = 500; // 500 us allows 2000 samples/s for the internal averaging.

// ----------- Rolling-Size Sweep Test Parameters
constexpr size_t ROLLING_SWEEP_SAMPLE_COUNT = 10000;
constexpr size_t ROLLING_SWEEP_SIZES[] = { 1, 10, 20, 50, 100, 200, 500, 1000 };
constexpr size_t ROLLING_SWEEP_SIZE_COUNT = sizeof(ROLLING_SWEEP_SIZES) / sizeof(ROLLING_SWEEP_SIZES[0]);
constexpr size_t ROLLING_SWEEP_DISPLAY_UPDATE_INTERVAL = 200; // Only update the display every N samples to minimize interference.

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
constexpr size_t MAX_TEST_RESULTS = RESISTOR_OPTION_COUNT * TARGET_EFFECTIVE_RATE_COUNT;

// ----------- Preferences Keys
const char PREF_NAMESPACE[] = "cap_tune";
const char PREF_VALID_KEY[] = "valid";
const char PREF_CHARGE_PIN_KEY[] = "cpin";
const char PREF_DISCHARGE_KEY[] = "dly";
const char PREF_BUFFER_KEY[] = "buf";

// ----------- Live Editable Fields (Encoder A selects a field, Encoder B adjusts it)
// Changing any of these immediately re-applies the configuration to the live sensor.
long liveResistorIndex = 0;
long liveDischargeDelayMicros = CapacitorSensor::DEFAULT_DISCHARGE_DELAY_MICROS;
long liveBufferSize = (long)CapacitorSensor::DEFAULT_BUFFER_SIZE;
long liveTestType = 0;

Format liveResistorFormat(8, Format::Alignment::LEFT);
Format liveDischargeFormat("#### us", Format::Alignment::LEFT);
Format liveBufferFormat("###", Format::Alignment::LEFT);
Format liveTestTypeFormat(20, Format::Alignment::LEFT);

// ----------- Test Type Selection
/// <summary>Test types selectable via the Test Type live field, run by pressing Button A.</summary>
enum class TestType
{
   OPTIMIZE = 0,
   BUFFER_SIZE_SWEEP = 1,
   DISCHARGE_TIME_SWEEP = 2,
};

constexpr const char* TEST_TYPE_LABELS[] = { "Optimize", "Buffer Size Sweep", "Discharge Time Sweep" };
constexpr size_t TEST_TYPE_COUNT = sizeof(TEST_TYPE_LABELS) / sizeof(TEST_TYPE_LABELS[0]);

///
/// <summary>
/// Resistor-selection setup field that steps through RESISTOR_OPTIONS by index and displays
/// the resistor's label instead of a raw index number.
/// </summary>
///
class ResistorSetupField : public IntSetupField
{
public:
   using IntSetupField::IntSetupField;

   void adjust(int32_t direction) override
   {
      long count = (long)RESISTOR_OPTION_COUNT;
      long newValue = (*_value + (direction > 0 ? 1 : -1) + count) % count;
      *_value = newValue;
   }

   std::string valueText() override
   {
      long index = constrain(*_value, 0L, (long)(RESISTOR_OPTION_COUNT - 1));
      return _format.toString(RESISTOR_OPTIONS[index].label);
   }
};

///
/// <summary>
/// Test-type-selection setup field that steps through TEST_TYPE_LABELS by index and displays
/// the test type's label instead of a raw index number.
/// </summary>
///
class TestTypeSetupField : public IntSetupField
{
public:
   using IntSetupField::IntSetupField;

   void adjust(int32_t direction) override
   {
      long count = (long)TEST_TYPE_COUNT;
      long newValue = (*_value + (direction > 0 ? 1 : -1) + count) % count;
      *_value = newValue;
   }

   std::string valueText() override
   {
      long index = constrain(*_value, 0L, (long)(TEST_TYPE_COUNT - 1));
      return _format.toString(TEST_TYPE_LABELS[index]);
   }
};

ResistorSetupField resistorField("Resistor", &liveResistorIndex,
   0, (long)(RESISTOR_OPTION_COUNT - 1), 1, 0, liveResistorFormat);
IntSetupField delayField("Discharge Time", &liveDischargeDelayMicros,
   50, 2000, 50, (long)CapacitorSensor::DEFAULT_DISCHARGE_DELAY_MICROS, liveDischargeFormat);
IntSetupField bufferSizeField("Averaging Buffer Size", &liveBufferSize,
   (long)MIN_TARGET_BUFFER_SIZE, (long)MAX_TARGET_BUFFER_SIZE, 5, (long)CapacitorSensor::DEFAULT_BUFFER_SIZE, liveBufferFormat);
TestTypeSetupField testTypeField("Test Type", &liveTestType,
   0, (long)(TEST_TYPE_COUNT - 1), 1, 0, liveTestTypeFormat);

SetupField* liveFields[] = { &resistorField, &delayField, &bufferSizeField, &testTypeField };
SetupTable liveFieldTable(&arduino, PREF_NAMESPACE, liveFields, 4);

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
TimedStats liveChargeStats(STATS_PERIOD_MS);
uint32_t liveChargeStatsLastCounter = 0;

///
/// <summary>Feeds newly-processed charge time samples into the live 500ms stats window.</summary>
///
void updateLiveChargeStats()
{
   uint32_t counter = capacitorSensor.counter();
   if (counter == liveChargeStatsLastCounter)
   {
      return;
   }

   liveChargeStatsLastCounter = counter;
   float chargeTime = capacitorSensor.chargeTimeMicros();
   if (isfinite(chargeTime))
   {
      liveChargeStats.set(chargeTime);
   }
}

///
/// <summary>Applies a resistor/discharge/buffer configuration to the capacitor sensor.</summary>
/// <param name="chargePin">Charge pin to select</param>
/// <param name="dischargeDelayMicros">Discharge delay in microseconds</param>
/// <param name="deferredProcessingPeriodMicros">Deferred processing period in microseconds</param>
/// <param name="bufferSize">Rolling average buffer size (falls back to the sensor default when zero)</param>
///
void applyConfiguration(uint8_t chargePin, uint16_t dischargeDelayMicros, uint32_t deferredProcessingPeriodMicros, size_t bufferSize)
{
   capacitorSensor.setChargePin(chargePin);
   capacitorSensor.setDischargeDelayMicros(dischargeDelayMicros);
   capacitorSensor.setDeferredProcessingPeriodMicros(deferredProcessingPeriodMicros);
   capacitorSensor.setBufferSize(bufferSize > 0 ? bufferSize : CapacitorSensor::DEFAULT_BUFFER_SIZE);
   liveChargeStats.reset();
}

///
/// <summary>Updates the live editable fields (Resistor/Discharge/Buffer) to reflect a configuration
/// and persists the new values, so the on-screen fields stay in sync with the sensor.</summary>
/// <param name="chargePin">Charge pin the sensor was configured with</param>
/// <param name="dischargeDelayMicros">Discharge delay in microseconds the sensor was configured with</param>
/// <param name="bufferSize">Rolling average buffer size the sensor was configured with</param>
///
void syncLiveFields(uint8_t chargePin, uint16_t dischargeDelayMicros, size_t bufferSize)
{
   liveResistorIndex = (long)resistorIndexFromChargePin(chargePin);
   liveDischargeDelayMicros = (long)dischargeDelayMicros;
   liveBufferSize = (long)bufferSize;
   liveFieldTable.save();
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
/// Loads the best-known configuration from Preferences and applies it, printing a summary to Serial.
/// </summary>
///
void loadBestConfiguration()
{
   if (capacitorSensor.counter() == 0 || !isfinite(capacitorSensor.chargeTimeMicros()) || capacitorSensor.rate() <= 0.0f)
   {
      Serial.println("Saved config not applied: sensor is not ready yet.");
      return;
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
      return;
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

   applyConfiguration(chargePin, dischargeDelayMicros, FIXED_DEFERRED_PROCESSING_PERIOD_MICROS, (size_t)bufferSize);

   const SerialTable::Column columns[] = {
      { "Field", 18 },
      { "Value", 24 },
   };
   SerialTable table("Loaded Saved Best Configuration", columns, sizeof(columns) / sizeof(columns[0]));
   table.printHeader();
   table.printRow("Resistor Value", String(resistorLabel(chargePin)) + " (Pin " + String((unsigned long)chargePin) + ")");
   table.printRow("Discharge Delay", String((unsigned long)dischargeDelayMicros) + " us");
   table.printRow("Buffer Size", (unsigned long)bufferSize);
   table.printRow("Deferred Period", String((unsigned long)FIXED_DEFERRED_PROCESSING_PERIOD_MICROS) + " us");
   Serial.println();
}

///
/// <summary>Target rate to be tested during a calibration sweep.</summary>
///
struct TestCase
{
   float targetEffectiveRate = NAN;
};

///
/// <summary>Configuration and measured statistics captured from a single calibration test run.</summary>
///
struct TestRunResult
{
   const char* resistorLabel = nullptr;
   float targetEffectiveRate = NAN;
   uint32_t deferredProcessingPeriodMicros = 0;
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
   /// <returns>Test result data structure</returns>
   ///
   TestRunResult run(float targetEffectiveRate)
   {
      TestRunResult result;
      result.targetEffectiveRate = targetEffectiveRate;
      result.deferredProcessingPeriodMicros = FIXED_DEFERRED_PROCESSING_PERIOD_MICROS;

      // Stabilize the sensor at the fixed default discharge delay, then read the rate
      _sensor.setDischargeDelayMicros(CapacitorSensor::DEFAULT_DISCHARGE_DELAY_MICROS);
      _sensor.setDeferredProcessingPeriodMicros(FIXED_DEFERRED_PROCESSING_PERIOD_MICROS);
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
      unsigned long startMs = millis();
      while (millis() - startMs < STATS_PERIOD_MS)
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
      _sensor.setDeferredProcessingPeriodMicros(FIXED_DEFERRED_PROCESSING_PERIOD_MICROS);

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
   TestCase cases[TARGET_EFFECTIVE_RATE_COUNT];
   /// <summary>Number of test cases.</summary>
   size_t count = 0;
};

TestCaseParameters testParameters;

/// <summary>Applies the current live editable field values (resistor/discharge/buffer) to the sensor.</summary>
void applyLiveFields()
{
   long resistorIndex = constrain(liveResistorIndex, 0L, (long)(RESISTOR_OPTION_COUNT - 1));
   applyConfiguration(
      RESISTOR_OPTIONS[resistorIndex].chargePin,
      (uint16_t)liveDischargeDelayMicros,
      FIXED_DEFERRED_PROCESSING_PERIOD_MICROS,
      (size_t)liveBufferSize);
}

///
/// <summary>
/// Draws the heading and the given subheading for the log viewer, sizes the
/// viewer's rect to fill the remaining space above the footer prompt, and switches into
/// viewer mode.
/// </summary>
/// <param name="subheading">Subheading text describing which test's results are being shown (e.g. "Buffer Size Sweep Test Results...").</param>
///
void initializeResultsView(const String& subheading)
{
   arduino.clearDisplay();

   arduino.setCursor(0, 0);
   arduino.setTextSize(DEFAULT_HEADING_SIZE);
   arduino.println("Capacitor Tuning", Color::HEADING);

   arduino.setTextSize(DEFAULT_TEXT_SIZE);
   arduino.println(subheading, Color::LIGHTGRAY);
   arduino.moveCursorY(8);

   int16_t viewerTop = arduino.getCursorY();
   textViewer.setRect(Rect16{ 0, static_cast<uint16_t>(viewerTop), static_cast<uint16_t>(arduino.width()),
                             static_cast<uint16_t>(arduino.height() - arduino.charH() - viewerTop) });

   viewingResults = true;
}

///
/// <summary>
/// Collects ROLLING_SWEEP_SAMPLE_COUNT raw charge-time samples using the currently selected
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
   arduino.println(String("Buffer Size Sweep: ") + String(collected) + "/" + String(ROLLING_SWEEP_SAMPLE_COUNT), Color::HEADING);
   arduino.setTextSize(DEFAULT_TEXT_SIZE);
   arduino.moveCursorY(8);

   long resistorIndex = constrain(liveResistorIndex, 0L, (long)(RESISTOR_OPTION_COUNT - 1));
   arduino.println("    Resistor: ", RESISTOR_OPTIONS[resistorIndex].label, resistorLabelFormat, Color::VALUE);
   arduino.println("       Delay: ", (int)liveDischargeDelayMicros, chargeFormat, Color::VALUE);

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
            arduino.println(String("Buffer Size Sweep: ") + String(collected) + "/" + String(ROLLING_SWEEP_SAMPLE_COUNT), Color::HEADING);
            arduino.setTextSize(DEFAULT_TEXT_SIZE);
         }
      }
   }

   Serial.println();
   Serial.println("Rolling Sweep: computing stats...");

   const SerialTable::Column columns[] = {
      { "Rolling Size", 14 },
      { "Range(us)", 12 },
      { "StdDev(us)", 12 },
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
      String rowText = table.printRow(
         (unsigned long)rollingSize,
         SerialTable::fixed(rangeMicros, 3),
         SerialTable::fixed(rollingAverageStats.stdDev(), 3));
      textViewer.addText(rowText);
   }

   Serial.println();
   Serial.println("Rolling Sweep Complete");

   capacitorSensor.setBufferSize((size_t)liveBufferSize);
   initializeResultsView("Buffer Size Sweep Test Results...");
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

   // Use a buffer size of 1 so every raw charge-time reading is captured individually.
   capacitorSensor.setBufferSize(1);

   const size_t totalSamples = DISCHARGE_SWEEP_STEP_COUNT * DISCHARGE_SWEEP_SAMPLE_COUNT;
   size_t totalCollected = 0;

   arduino.clearDisplay();
   arduino.setCursor(0, 0);
   arduino.setTextSize(DEFAULT_HEADING_SIZE);
   int16_t headerRowY = arduino.getCursorY();
   arduino.println(String("Discharge Time Sweep: ") + String(totalCollected) + "/" + String(totalSamples), Color::HEADING);
   arduino.setTextSize(DEFAULT_TEXT_SIZE);
   arduino.moveCursorY(8);

   long resistorIndex = constrain(liveResistorIndex, 0L, (long)(RESISTOR_OPTION_COUNT - 1));
   arduino.println("    Resistor: ", RESISTOR_OPTIONS[resistorIndex].label, resistorLabelFormat, Color::VALUE);

   const SerialTable::Column columns[] = {
      { "Delay(us)", 12 },
      { "Range(us)", 12 },
      { "StdDev(us)", 12 },
   };
   SerialTable table(nullptr, columns, sizeof(columns) / sizeof(columns[0]));
   String headerText = table.printHeader();
   textViewer.addText(headerText);

   for (uint16_t dischargeDelayMicros = DISCHARGE_SWEEP_MIN_MICROS; dischargeDelayMicros <= DISCHARGE_SWEEP_MAX_MICROS; dischargeDelayMicros += DISCHARGE_SWEEP_STEP_MICROS)
   {
      capacitorSensor.setDischargeDelayMicros(dischargeDelayMicros);

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
               arduino.println(String("Discharge Time Sweep: ") + String(totalCollected) + "/" + String(totalSamples), Color::HEADING);
               arduino.setTextSize(DEFAULT_TEXT_SIZE);
            }
         }
      }

      Stats chargeStats;
      for (size_t i = 0; i < DISCHARGE_SWEEP_SAMPLE_COUNT; i++)
      {
         chargeStats.add(samples[i]);
      }

      float rangeMicros = chargeStats.max() - chargeStats.min();
      String rowText = table.printRow(
         (unsigned long)dischargeDelayMicros,
         SerialTable::fixed(rangeMicros, 3),
         SerialTable::fixed(chargeStats.stdDev(), 3));
      textViewer.addText(rowText);
   }

   Serial.println();
   Serial.println("Discharge Sweep Complete");

   capacitorSensor.setDischargeDelayMicros((uint16_t)liveDischargeDelayMicros);
   initializeResultsView("Discharge Time Sweep Test Results...");
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

         TestRun testRun(capacitorSensor);
         TestRunResult result = testRun.run(testParameters.cases[i].targetEffectiveRate);
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
         CapacitorSensor::DEFAULT_DISCHARGE_DELAY_MICROS,
         best->deferredProcessingPeriodMicros,
         best->bufferSize);
      syncLiveFields(
         bestChargePin,
         CapacitorSensor::DEFAULT_DISCHARGE_DELAY_MICROS,
         best->bufferSize);
      saveBestConfiguration(
         bestChargePin,
         CapacitorSensor::DEFAULT_DISCHARGE_DELAY_MICROS,
         best->bufferSize);
   }
   else
   {
      applyConfiguration(
         RESISTOR_OPTIONS[0].chargePin,
         CapacitorSensor::DEFAULT_DISCHARGE_DELAY_MICROS,
         FIXED_DEFERRED_PROCESSING_PERIOD_MICROS,
         CapacitorSensor::DEFAULT_BUFFER_SIZE);
   }

   initializeResultsView("Optimization Sweep Test Results...");
}

void setup()
{
   SerialX::begin();
   arduino.begin();
   capacitorSensor.begin();

   arduino.setTextSize(DEFAULT_TEXT_SIZE);

   // Temporarily disabled: skip loading previously saved field values from Preferences,
   // so the sketch always starts from the compiled-in defaults.
   // liveFieldTable.load();
   applyLiveFields();

   size_t caseIndex = 0;
   for (size_t rateIndex = 0; rateIndex < TARGET_EFFECTIVE_RATE_COUNT; rateIndex++)
   {
      float targetRate = TARGET_EFFECTIVE_RATES[rateIndex];

      testParameters.cases[caseIndex].targetEffectiveRate = targetRate;
      caseIndex++;
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
   Serial.println();
   Serial.println("------- Best Configurations (Top 3 Lowest StdDev %) -------");
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
      Serial.print("  Target: ");
      Serial.print((int)round(targetRate));
      Serial.println("/s");
      String targetLine = "  Target: " + String((int)round(targetRate)) + "/s";
      textViewer.addLine("");
      textViewer.addLine(targetLine);

      const SerialTable::Column columns[] = {
         { "Rank", 8 },
         { "Resistor (Pin)", 18 },
         { "Buffer", 8 },
         { "StdDev %", 10 },
      };
      SerialTable table("Best Configurations", columns, sizeof(columns) / sizeof(columns[0]));
      String headerText = table.printHeader();
      textViewer.addText(headerText);

      size_t printCount = (indexCount < 3) ? indexCount : 3;
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
            (unsigned long)r.bufferSize,
            stdDevPct);
         textViewer.addText(rowText);
      }

      Serial.println();
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
      { "Value", 14 },
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

   Serial.println();
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
      { "Value", 14 },
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

      char label[16] = "";
      snprintf(label, sizeof(label), "%d/s", (int)round(targetRate));
      printAggregateRow(table, label, avgStats, stdDevStats);
   }

   Serial.println();
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
      { "Value", 14 },
      { "N", 6 },
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
      Serial.println();
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
      char label[16] = "";
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

   Serial.println();
   textViewer.addLine("");
}

///
/// <summary>Prints all aggregate statistics groupings (by resistor, target rate, and buffer size).</summary>
/// <param name="results">Array of all test results</param>
/// <param name="count">Number of results in the array</param>
///
void printAggregateStatistics(const TestRunResult* results, size_t count)
{
   Serial.println();
   Serial.println("------- Aggregate Statistics -------");
   textViewer.addLine("");
   textViewer.addLine("------- Aggregate Statistics -------");

   printAggregateByResistor(results, count);
   printAggregateByTargetRate(results, count);
   printAggregateByBufferSize(results, count);
}

void loop()
{
   if (Serial.available() > 0)
   {
      char command = (char)Serial.read();
      if (command == 'L' || command == 'l')
      {
         loadBestConfiguration();
      }
   }

   if (viewingResults)
   {
      if (arduino.buttonA.wasPressed())
      {
         viewingResults = false;
         arduino.clearDisplay();
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

   updateLiveChargeStats();

   // Live-edit the resistor/discharge/buffer fields: Encoder A selects a field, Encoder B
   // adjusts it. Any change is immediately applied to the sensor and persisted, so the
   // charge time/rate readouts below update live as the values change.
   bool fieldsChanged = liveFieldTable.poll();

   if (arduino.encoderB.button.wasPressed())
   {
      // reset() already persists the defaults, so just re-apply them below.
      liveFieldTable.reset();
      applyLiveFields();
      forceDisplayUpdate = true;
   }
   else if (fieldsChanged)
   {
      applyLiveFields();
      liveFieldTable.save();
      forceDisplayUpdate = true;
   }

   if (arduino.buttonB.wasPressed())
   {
      // Currently unused.
   }

   if (arduino.buttonA.wasPressed())
   {
      TestType selectedTestType = (TestType)constrain(liveTestType, 0L, (long)(TEST_TYPE_COUNT - 1));
      switch (selectedTestType)
      {
      case TestType::BUFFER_SIZE_SWEEP:
         runRollingSweepTest();
         break;

      case TestType::DISCHARGE_TIME_SWEEP:
         runDischargeSweepTest();
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
      int16_t fieldsTop = arduino.getCursorY();
      liveFieldTable.draw(0, fieldsTop, 21);
      arduino.setCursor(0, fieldsTop + liveFieldTable.height());

      float rawRate = capacitorSensor.rate();
      float effectiveRate = (capacitorSensor.bufferSize() > 0) ? (rawRate / (float)capacitorSensor.bufferSize()) : 0;

      arduino.println("      Avg Charge Time: ", capacitorSensor.chargeTimeMicros(), chargeFormat, Color::VALUE2);
      arduino.println("               StdDev: ", liveChargeStats.stdDev(), chargeFormat, Color::VALUE2);
      arduino.println("                Range: ", liveChargeStats.range(), chargeFormat, Color::VALUE2);
      arduino.println("             Raw Rate: ", (int)round(rawRate), sampleRateFormat, Color::VALUE2);
      arduino.println("       Effective Rate: ", effectiveRate, effectiveRateFormat, Color::VALUE2);

      const char* instructions[] =
      {
         "Encoder A: select field",
         "Encoder B: adjust value",
         "Encoder B Button: reset field defaults",
         "Button A: run selected test",
      };
      constexpr size_t instructionCount = sizeof(instructions) / sizeof(instructions[0]);

      arduino.setCursorY(arduino.height() - instructionCount * arduino.charH());
      for (size_t i = 0; i < instructionCount; i++)
      {
         arduino.printlnR(instructions[i], Color::SUB_LABEL);
      }
   }
}

