//
// Captures finite sensor values for a fixed run window (or until the sample cap is reached), stores
// them in RAM, and reports serial/display summaries.
//
// On startup, a prompt screen is shown where Encoder A adjusts the target sample rate (starting at
// 100/s); steps are 10/s below 100/s and 100/s at or above 100/s. Press Encoder A's button to start
// the capture using the selected rate.
//
// Capture flow:
// 1) Initialize display, serial, and sensor.
// 2) Prompt for the target sample rate via Encoder A; start capture on button press.
// 3) Sample at up to the selected target rate and store finite values in RAM.
// 4) Stop when MAX_SAMPLES are stored or SAMPLING_DURATION_S elapses.
//
// Output flow:
// - While collecting, the Playground display shows live capture progress plus target/actual sample rate.
// - After capture, the display renders a value histogram with min/max labels plus Sigma%/effective-rate
//   rows for multiple averaging window sizes, along with the target/actual sample rate.
// - Serial summary includes run metrics, value stats, target/actual sample rate, and a value histogram.
//
// Sensor mode:
// - Reads from TestSensor (configurable via TestSensor.h using alias).
//
// Typical usage: flash the sketch, dial in the desired sample rate with Encoder A, press Encoder A's
// button to start, and allow capture to complete, then review serial and display outputs to compare
// stability (StdDev%, range) across averaging sizes.
//

#include "ESP32_S3_Playground.h"
#include "Histogram.h"
#include "SensorCapture.h"
#include "Timer.h"
#include "SerialX.h"
#include "SerialHistogram.h"
#include "HistogramPlot.h"
#include "DisplayTable.h"
#include "RollingRate.h"
#include "Util.h"
#include <Wire.h>
#include "TestSensor.h"

// ----------- The Board
ESP32_S3_Playground arduino;
TestSensor sensor;

// ----------- Capture Settings
constexpr unsigned long DEFAULT_SAMPLING_DURATION_S = 10;
constexpr uint16_t DEFAULT_SAMPLE_RATE_PER_SEC = 100;
constexpr uint16_t MIN_SAMPLE_RATE_PER_SEC = 10;
constexpr uint16_t MAX_SAMPLE_RATE_PER_SEC = 1000;
constexpr uint16_t SAMPLE_RATE_STEP_LOW = 10;
constexpr uint16_t SAMPLE_RATE_STEP_HIGH = 100;
constexpr size_t DEFAULT_MAX_SAMPLES = 1000;
constexpr size_t MIN_MAX_SAMPLES = 100;
constexpr size_t MAX_MAX_SAMPLES = 5000;
constexpr size_t SAMPLE_STEP = 100;
constexpr unsigned long MIN_SAMPLING_DURATION_S = 5;
constexpr unsigned long MAX_SAMPLING_DURATION_S = 60;
constexpr unsigned long DURATION_STEP_S = 5;
constexpr unsigned long DEFAULT_WARMUP_PERIOD_S = 0;
constexpr unsigned long MAX_WARMUP_PERIOD_S = 30;
constexpr unsigned long WARMUP_STEP_S = 1;

uint16_t targetSampleRate = DEFAULT_SAMPLE_RATE_PER_SEC;
size_t maxSamples = DEFAULT_MAX_SAMPLES;
unsigned long samplingDurationS = DEFAULT_SAMPLING_DURATION_S;
unsigned long warmupPeriodS = DEFAULT_WARMUP_PERIOD_S;
RollingRate actualSampleRate;

// Setup page selection state
enum SetupItem { SAMPLE_RATE = 0, MAX_SAMPLES_ITEM = 1, SAMPLING_DURATION = 2, WARMUP_PERIOD = 3 };
SetupItem selectedSetupItem = SAMPLE_RATE;

SensorCapture* sensorCapture = nullptr;
bool captureStarted = false;
bool captureFinalized = false;
unsigned long captureStartMs = 0;

///
/// <summary>
/// Creates a new SensorCapture with current settings.
/// </summary>
///
void createSensorCapture()
{
   if (sensorCapture != nullptr)
   {
      delete sensorCapture;
   }
   sensorCapture = new SensorCapture(
      maxSamples,
      samplingDurationS * 1000UL,
      1000UL / targetSampleRate);
}

// ----------- Display Items
constexpr uint16_t DISPLAY_UPDATE_RATE_PER_SEC = 5;
RateTimer displayRefreshTimer(DISPLAY_UPDATE_RATE_PER_SEC);
bool collectingViewInitialized = false;
bool promptViewInitialized = false;
Format progressPercentFormat("###%", Format::Alignment::LEFT);
Format collectingSamplesFormat("####/1000", Format::Alignment::LEFT);
Format targetRateFormat("####/s", Format::Alignment::LEFT);
Format actualRateFormat("####.#/s", Format::Alignment::LEFT);
Format collectingTimeFormat("##/10s", Format::Alignment::LEFT);
DisplayTable collectingTable(&arduino, 0, 0);

// ----------- Analysis Settings
constexpr size_t HISTOGRAM_BINS = 20;
constexpr uint8_t CHART_MIN_MAX_SIGNIFICANT_DIGITS = 3;
constexpr size_t BUFFER_SIZES[] = { 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 };
constexpr size_t NUM_BUFFER_SIZES = sizeof(BUFFER_SIZES) / sizeof(BUFFER_SIZES[0]);

/// <summary>
/// Draws a histogram panel on the Playground display.
/// </summary>
/// <param name="title">Title text drawn above the panel, or empty for none.</param>
/// <param name="histogram">Histogram data to render.</param>
/// <param name="sectionLeft">Left X coordinate of the panel.</param>
/// <param name="sectionWidth">Width of the panel in pixels.</param>
/// <param name="sectionTop">Top Y coordinate of the panel.</param>
/// <param name="sectionHeight">Height of the panel in pixels.</param>
/// <param name="barColor">Color used to draw histogram bars.</param>
/// <param name="axisLabelColor">Color used to draw axis labels (min/max text).</param>
void drawHistogramOnPlayground(const char* title, const Histogram& histogram, int16_t sectionLeft, int16_t sectionWidth, int16_t sectionTop, int16_t sectionHeight, Color barColor, Color axisLabelColor)
{
   arduino.setTextSize(2);
   arduino.setCursor(sectionLeft, sectionTop);
   arduino.println(title, Color::LABEL);

   int16_t chartTop = arduino.getCursorY() + 1;
   int16_t adjustedHeight = sectionHeight - (chartTop - sectionTop);
   HistogramPlot plot(&arduino, histogram, sectionLeft, sectionWidth, chartTop, adjustedHeight, barColor, CHART_MIN_MAX_SIGNIFICANT_DIGITS, axisLabelColor);
   plot.render();
}

/// <summary>
/// Computes the effective rate for a given averaging block size using the current target sample rate.
/// </summary>
/// <param name="sampleSize">Averaging block size (N).</param>
/// <returns>Effective rate in samples/sec, or NaN when sampleSize is zero.</returns>
float computeEffectiveRateHz(size_t sampleSize)
{
   return (sampleSize > 0) ? (static_cast<float>(targetSampleRate) / static_cast<float>(sampleSize)) : NAN;
}

/// <summary>
/// Renders histogram and N/range analysis table on the Playground display.
/// </summary>
void renderHistogramsOnPlayground()
{
   arduino.clearDisplay();

   Histogram valueHistogram(sensorCapture->values(), sensorCapture->count(), HISTOGRAM_BINS);

   arduino.setTextSize(2);
   arduino.setCursor(0, 0);
   String headerText = String(sensorCapture->count()) + " Samples";
   arduino.println(headerText.c_str(), Color::HEADING);

   String rateText = "Target " + String(targetSampleRate) + "/s  Actual " + String(actualSampleRate.get(), 1) + "/s";
   arduino.println(rateText.c_str(), Color::LABEL);

   int16_t top = arduino.getCursorY();
   int16_t messageHeight = arduino.charH() + 2;
   int16_t availableHeight = (int16_t)arduino.height() - top - messageHeight;
   int16_t totalWidth = (int16_t)arduino.width();
   constexpr int16_t sectionGap = 5;
   constexpr int16_t tableWidth = 160;
   int16_t leftWidth = totalWidth - sectionGap - tableWidth;
   int16_t x = leftWidth + sectionGap;

   drawHistogramOnPlayground("", valueHistogram, 0, leftWidth, top, availableHeight, Color::GREEN, Color::WHITE);

   arduino.setTextSize(2);
   Format numSamplesFormat("####", Format::Alignment::RIGHT);
   Format sigmaPercentFormat("###.##%", Format::Alignment::RIGHT);
   Format hzFormat(" ####", Format::Alignment::RIGHT);

   Stats rawStats = sensorCapture->computeBasicStats();
   float rawAvg = rawStats.get();
   float rawStdDev = rawStats.stdDev();
   size_t rawCount = rawStats.count();

   arduino.setCursor(x, 0);
   arduino.println("   N Sigma%   Hz", Color::VALUE3);

   arduino.setCursorX(x);
   float rawSigmaPercent = NAN;
   if (isfinite(rawAvg) && (fabsf(rawAvg) > 0.0f) && isfinite(rawStdDev))
   {
      rawSigmaPercent = (rawStdDev / fabsf(rawAvg)) * 100.0f;
   }

   if ((rawCount > 0) && isfinite(rawSigmaPercent))
   {
      arduino.print(" Raw", Color::LABEL);
      arduino.print(rawSigmaPercent, sigmaPercentFormat, Color::VALUE2);
      arduino.println(computeEffectiveRateHz(1), hzFormat, Color::VALUE2);
   }
   else
   {
      arduino.println(" Raw   n/a   n/a", Color::GRAY);
   }

   constexpr size_t BUFFER_SIZE_FOR_DISPLAY[] = { 10, 20, 50, 100 };
   for (size_t i = 0; i < (sizeof(BUFFER_SIZE_FOR_DISPLAY) / sizeof(BUFFER_SIZE_FOR_DISPLAY[0])); i++)
   {
      arduino.moveCursorY(-1);
      size_t sampleSize = BUFFER_SIZE_FOR_DISPLAY[i];
      float effectiveRateHz = computeEffectiveRateHz(sampleSize);

      Stats avgSeriesStats = sensorCapture->computeAverageSeriesStats(sampleSize);
      float avgMean = avgSeriesStats.get();
      float avgStdDev = avgSeriesStats.stdDev();
      size_t averageCount = avgSeriesStats.count();

      float avgSigmaPercent = NAN;
      if (isfinite(avgMean) && (fabsf(avgMean) > 0.0f) && isfinite(avgStdDev))
      {
         avgSigmaPercent = (avgStdDev / fabsf(avgMean)) * 100.0f;
      }

      arduino.setCursorX(x);
      if ((averageCount > 0) && isfinite(avgSigmaPercent))
      {
         arduino.print(sampleSize, numSamplesFormat, Color::LABEL);
         arduino.print(avgSigmaPercent, sigmaPercentFormat, Color::VALUE);
         arduino.println(effectiveRateHz, hzFormat, Color::VALUE);
      }
      else
      {
         std::string rowText = numSamplesFormat.toString((double)sampleSize) + "   n/a   n/a";
         arduino.println(rowText, Color::GRAY);
      }
   }
}

/// <summary>
/// Computes and prints capture statistics and histogram data to Serial.
/// </summary>
void printCaptureSummary()
{
   Stats basicStats = sensorCapture->computeBasicStats();

   float valueAvg = basicStats.get();
   float valueStdDev = basicStats.stdDev();
   float valueMin = basicStats.min();
   float valueMax = basicStats.max();
   float valueRange = SensorCapture::computeRange(valueMin, valueMax);
   float valueStdDevPercent = NAN;
   if (isfinite(valueAvg) && (fabsf(valueAvg) > 0.0f) && isfinite(valueStdDev))
   {
      valueStdDevPercent = (valueStdDev / fabsf(valueAvg)) * 100.0f;
   }

   Serial.println();
   Serial.println("Capture Summary");
   SerialX::print("Capture ms", 20);
   SerialX::println(samplingDurationS * 1000UL, 20);
   SerialX::print("Target Rate", 20);
   SerialX::println(String(targetSampleRate) + "/s", 20);
   SerialX::print("Actual Rate", 20);
   SerialX::println(String(actualSampleRate.get(), 1) + "/s", 20);
   SerialX::print("Samples", 20);
   SerialX::println(sensorCapture->count(), 20);
   SerialX::print("Avg", 20);
   SerialX::println(valueAvg, 3, 20);
   SerialX::print("StdDev", 20);
   SerialX::println(valueStdDev, 3, 20);
   SerialX::print("StdDev%", 20);
   SerialX::println(isfinite(valueStdDevPercent) ? String(valueStdDevPercent, 2) + "%" : "n/a", 20);
   SerialX::print("Min", 20);
   SerialX::println(valueMin, 3, 20);
   SerialX::print("Max", 20);
   SerialX::println(valueMax, 3, 20);
   SerialX::print("Range", 20);
   SerialX::println(valueRange, 3, 20);
   Serial.println();

   Histogram histogram(sensorCapture->values(), sensorCapture->count(), HISTOGRAM_BINS);
   SerialHistogram::print("Sensor Value Histogram", histogram, 3);

   sensorCapture->printFirstAndLastToSerial(10, 3);
}

/// <summary>
/// Prints post-capture block-average analysis for configured sample sizes.
/// </summary>
void printAveragingAnalysis()
{
   Serial.println("Averaging Analysis");

   SerialX::print("Num Samples", 12);
   SerialX::print("Range", 10);
   SerialX::print("StdDev", 10);
   SerialX::print("StdDev%", 10);
   SerialX::println("Eff Rate", 8);

   SerialX::print("-----------", 12);
   SerialX::print("-----", 10);
   SerialX::print("------", 10);
   SerialX::print("-------", 10);
   SerialX::println("--------", 8);

   for (size_t analysisIndex = 0; analysisIndex < NUM_BUFFER_SIZES; analysisIndex++)
   {
      size_t sampleSize = BUFFER_SIZES[analysisIndex];
      float effectiveRate = (sampleSize > 0) ? (static_cast<float>(targetSampleRate) / static_cast<float>(sampleSize)) : NAN;

      Stats avgSeriesStats = sensorCapture->computeAverageSeriesStats(sampleSize);
      float avgRange = SensorCapture::computeRange(avgSeriesStats.min(), avgSeriesStats.max());
      float avgStdDev = avgSeriesStats.stdDev();
      float avgMean = avgSeriesStats.get();
      size_t averageCount = avgSeriesStats.count();

      float avgStdDevPercent = NAN;
      if (isfinite(avgMean) && (fabsf(avgMean) > 0.0f) && isfinite(avgStdDev))
      {
         avgStdDevPercent = (avgStdDev / fabsf(avgMean)) * 100.0f;
      }

      SerialX::print(sampleSize, 12);
      if (averageCount == 0)
      {
         SerialX::print("n/a", 10);
         SerialX::print("n/a", 10);
         SerialX::print("n/a", 10);
      }
      else
      {
         SerialX::print(avgRange, 3, 10);
         SerialX::print(avgStdDev, 3, 10);
         SerialX::print(isfinite(avgStdDevPercent) ? String(avgStdDevPercent, 2) + "%" : "n/a", 10);
      }

      SerialX::println(isfinite(effectiveRate) ? String(effectiveRate, 1) + "/s" : "n/a", 8);
   }

   Serial.println();
}

/// <summary>
/// Initializes the display table used by the collecting-progress screen.
/// </summary>
void initializeCollectingTable()
{
   arduino.setTextSize(3);
   int16_t collectingTableY = arduino.charH();

   arduino.setTextSize(2);
   collectingTableY += arduino.charH();

   collectingTable = DisplayTable(&arduino, 0, collectingTableY);
   collectingTable.addRow("Samples", collectingSamplesFormat, Color::LABEL, Color::VALUE);
   collectingTable.addRow("Time", collectingTimeFormat, Color::LABEL, Color::VALUE);
   collectingTable.addRow("Progress", progressPercentFormat, Color::LABEL, Color::VALUE);
   collectingTable.addRow("Target Rate", targetRateFormat, Color::LABEL, Color::VALUE);
   collectingTable.addRow("Actual Rate", actualRateFormat, Color::LABEL, Color::VALUE);
}

/// <summary>
/// Updates collecting status text shown on the Playground display.
/// Refresh is throttled to DISPLAY_UPDATE_RATE_PER_SEC unless forceRefresh is true.
/// </summary>
/// <param name="forceRefresh">When true, bypasses the refresh-rate throttle and redraws immediately.</param>
void updateDisplay(bool forceRefresh = false)
{
   if (sensorCapture->isCaptureComplete())
   {
      return;
   }

   unsigned long nowMs = millis();
   if (forceRefresh)
   {
      displayRefreshTimer.reset();
   }
   else if (!displayRefreshTimer.ready())
   {
      return;
   }

   if (forceRefresh || !collectingViewInitialized)
   {
      arduino.clearDisplay();
      arduino.setCursor(0, 0);
      arduino.setTextSize(3);
      arduino.println("Sensor Averaging", Color::HEADING);

      arduino.setTextSize(2);
      arduino.println("Collecting data", Color::VALUE);
      collectingViewInitialized = true;
   }

   size_t count = sensorCapture->count();
   unsigned long elapsedSeconds = (nowMs - captureStartMs) / 1000UL;
   if (elapsedSeconds > samplingDurationS)
   {
      elapsedSeconds = samplingDurationS;
   }

   float samplePercent = (maxSamples > 0) ? ((static_cast<unsigned long>(count) * 100.0f) / maxSamples) : 0.0f;
   float timePercent = (samplingDurationS > 0) ? ((elapsedSeconds * 100.0f) / samplingDurationS) : 0.0f;
   bool samplesAreLimiting = (samplePercent >= timePercent);

   Color sampleColor = samplesAreLimiting ? Color::VALUE : Color::GRAY;
   Color timeColor = !samplesAreLimiting ? Color::VALUE : Color::GRAY;

   float progressPercent = max(samplePercent, timePercent);
   if (progressPercent > 100.0f)
   {
      progressPercent = 100.0f;
   }

   collectingTable.updateValue(0, count, sampleColor);
   collectingTable.updateValue(1, elapsedSeconds, timeColor);
   collectingTable.updateValue(2, progressPercent, Color::VALUE);
   collectingTable.updateValue(3, targetSampleRate, Color::VALUE);
   collectingTable.updateValue(4, actualSampleRate.get(), Color::VALUE);
   collectingTable.draw();
}

/// <summary>
/// Finalizes capture and prints summaries.
/// </summary>
void finishCapture()
{
   if (captureFinalized)
   {
      return;
   }

   captureFinalized = true;
   printCaptureSummary();
   printAveragingAnalysis();
   renderHistogramsOnPlayground();
}

/// <summary>
/// Adjusts the target sample rate by one encoder step, using 10/s steps below 100/s and
/// 100/s steps at or above 100/s, clamped to [MIN_SAMPLE_RATE_PER_SEC, MAX_SAMPLE_RATE_PER_SEC].
/// </summary>
/// <param name="steps">Signed number of encoder steps to apply.</param>
void adjustTargetSampleRate(int32_t steps)
{
   int32_t direction = (steps > 0) ? 1 : -1;
   int32_t remainingSteps = abs(steps);

   for (int32_t i = 0; i < remainingSteps; i++)
   {
      uint16_t stepSize = (targetSampleRate < 100) ? SAMPLE_RATE_STEP_LOW : SAMPLE_RATE_STEP_HIGH;
      int32_t newRate = static_cast<int32_t>(targetSampleRate) + (direction * stepSize);
      newRate = constrain(newRate, MIN_SAMPLE_RATE_PER_SEC, MAX_SAMPLE_RATE_PER_SEC);
      targetSampleRate = static_cast<uint16_t>(newRate);
   }
}

///
/// <summary>
/// Adjusts the maximum samples setting by encoder steps, clamped to [MIN_MAX_SAMPLES, MAX_MAX_SAMPLES].
/// </summary>
/// <param name="steps">Signed number of encoder steps to apply.</param>
///
void adjustMaxSamples(int32_t steps)
{
   int32_t direction = (steps > 0) ? 1 : -1;
   int32_t remainingSteps = abs(steps);

   for (int32_t i = 0; i < remainingSteps; i++)
   {
      int32_t newValue = static_cast<int32_t>(maxSamples) + (direction * SAMPLE_STEP);
      newValue = constrain(newValue, MIN_MAX_SAMPLES, MAX_MAX_SAMPLES);
      maxSamples = static_cast<size_t>(newValue);
   }
}

///
/// <summary>
/// Adjusts the sampling duration setting by encoder steps, clamped to [MIN_SAMPLING_DURATION_S, MAX_SAMPLING_DURATION_S].
/// </summary>
/// <param name="steps">Signed number of encoder steps to apply.</param>
///
void adjustSamplingDuration(int32_t steps)
{
   int32_t direction = (steps > 0) ? 1 : -1;
   int32_t remainingSteps = abs(steps);

   for (int32_t i = 0; i < remainingSteps; i++)
   {
      int32_t newValue = static_cast<int32_t>(samplingDurationS) + (direction * DURATION_STEP_S);
      newValue = constrain(newValue, MIN_SAMPLING_DURATION_S, MAX_SAMPLING_DURATION_S);
      samplingDurationS = static_cast<unsigned long>(newValue);
   }
}

///
/// <summary>
/// Adjusts the warmup period setting by encoder steps, clamped to [0, MAX_WARMUP_PERIOD_S].
/// </summary>
/// <param name="steps">Signed number of encoder steps to apply.</param>
///
void adjustWarmupPeriod(int32_t steps)
{
   int32_t direction = (steps > 0) ? 1 : -1;
   int32_t remainingSteps = abs(steps);

   for (int32_t i = 0; i < remainingSteps; i++)
   {
      int32_t newValue = static_cast<int32_t>(warmupPeriodS) + (direction * WARMUP_STEP_S);
      newValue = constrain(newValue, 0, MAX_WARMUP_PERIOD_S);
      warmupPeriodS = static_cast<unsigned long>(newValue);
   }
}

/// <summary>
/// Draws the startup prompt screen showing configurable options with the selected one highlighted.
/// EncoderA selects the option, EncoderB adjusts the value.
/// </summary>
void drawPromptScreen()
{
   arduino.clearDisplay();
   arduino.setCursor(0, 0);
   arduino.setTextSize(3);
   arduino.println("Setup", Color::HEADING);

   arduino.setTextSize(2);

   // Sample Rate option
   Color rateColor = (selectedSetupItem == SAMPLE_RATE) ? Color::VALUE2 : Color::LABEL;
   String rateText = "Rate: " + String(targetSampleRate) + "/s";
   arduino.println(rateText.c_str(), rateColor);

   // Max Samples option
   Color samplesColor = (selectedSetupItem == MAX_SAMPLES_ITEM) ? Color::VALUE2 : Color::LABEL;
   String samplesText = "Max Samples: " + String(maxSamples);
   arduino.println(samplesText.c_str(), samplesColor);

   // Sampling Duration option
   Color durationColor = (selectedSetupItem == SAMPLING_DURATION) ? Color::VALUE2 : Color::LABEL;
   String durationText = "Duration: " + String(samplingDurationS) + "s";
   arduino.println(durationText.c_str(), durationColor);

   // Warmup Period option
   Color warmupColor = (selectedSetupItem == WARMUP_PERIOD) ? Color::VALUE2 : Color::LABEL;
   String warmupText = "Warmup: " + String(warmupPeriodS) + "s";
   arduino.println(warmupText.c_str(), warmupColor);

   arduino.println();
   arduino.println("Enc A: Select", Color::VALUE);
   arduino.println("Enc B: Adjust", Color::VALUE);
   arduino.println("Button A: Start", Color::VALUE);
}


void setup()
{
   SerialX::begin(115200, 2000);
   Wire.begin();
   arduino.begin();

   sensor.begin();
   initializeCollectingTable();

   Util::printBoardInfo();

   createSensorCapture();

   drawPromptScreen();
   promptViewInitialized = true;
   Serial.println("Turn Encoder A to select, Encoder B to adjust, Button A to start.");
}

void loop()
{
   if (!captureStarted)
   {
      // EncoderA cycles through setup items
      int32_t deltaA = arduino.encoderA.delta();
      if (deltaA != 0)
      {
         int32_t newItem = static_cast<int32_t>(selectedSetupItem) + (deltaA > 0 ? 1 : -1);
         newItem = (newItem + 4) % 4; // Wrap around (0-3)
         selectedSetupItem = static_cast<SetupItem>(newItem);
         drawPromptScreen();
      }

      // EncoderB adjusts the selected item's value
      int32_t deltaB = arduino.encoderB.delta();
      if (deltaB != 0)
      {
         switch (selectedSetupItem)
         {
            case SAMPLE_RATE:
               adjustTargetSampleRate(deltaB);
               break;
            case MAX_SAMPLES_ITEM:
               adjustMaxSamples(deltaB);
               break;
            case SAMPLING_DURATION:
               adjustSamplingDuration(deltaB);
               break;
            case WARMUP_PERIOD:
               adjustWarmupPeriod(deltaB);
               break;
         }
         drawPromptScreen();
      }

      if (arduino.buttonA.wasPressed())
      {
         captureStarted = true;
         createSensorCapture();
         sensorCapture->setSamplingInterval(1000UL / targetSampleRate);
         sensorCapture->reset();
         actualSampleRate.reset();
         captureStartMs = millis();
         updateDisplay(true);
         Serial.println("Capture started...");
      }

      return;
   }

   if (captureFinalized)
   {
      if (arduino.buttonA.wasPressed())
      {
         captureStarted = false;
         captureFinalized = false;
         drawPromptScreen();
         Serial.println("Turn Encoder A to set sample rate, press Button A to start.");
      }

      return;
   }

   SensorCaptureState states = sensorCapture->update();
   SensorCaptureState valueStates = SENSOR_CAPTURE_STATE_NONE;

   if (sensorCapture->readyForValue())
   {
      float sensorValue = sensor.get();
      valueStates = sensorCapture->addValue(sensorValue);

      if ((valueStates & SENSOR_CAPTURE_STATE_VALUE_STORED) != 0)
      {
         actualSampleRate.tick();
         updateDisplay();
      }

      if ((valueStates & SENSOR_CAPTURE_STATE_COMPLETED) != 0)
      {
         finishCapture();
      }
   }

   if ((states & SENSOR_CAPTURE_STATE_COMPLETED) != 0)
   {
      finishCapture();
   }
}

