// -------------------------------------------------------------------------------------------------
// Sensor_Capture_Display
//
// This sketch captures high-rate sensor data for a fixed run window (or until a max sample count is
// reached), stores each accepted data point in RAM, and reports both serial and on-device summaries.
//
// Capture flow:
// 1) Initialize display/serial/sensor and allow a brief sensor warm-up period.
// 2) Treat the first finite sample as a timing baseline (not stored), then store each subsequent
//    sample with its interval (micros since the previous accepted sample).
// 3) Stop when MAX_SAMPLES are stored or SAMPLE_DURATION_MS expires.
//
// Output flow:
// - Serial summary includes run metrics, value stats, interval stats, and two histograms.
// - Feather display renders side-by-side value and interval histograms with min/max labels.
// - Button A triggers a paced serial dump of stored points (index, interval micros, value).
//
// Sensor modes:
// - Capacitor mode (default) consumes queued sensor events from CapacitorSensor.
// - Temperature mode reads directly per loop iteration.
// -------------------------------------------------------------------------------------------------
#include "Feather.h"
#include "SerialX.h"
#include "Histogram.h"
#include "Stats.h"
#include "Timer.h"

// Selects capacitor-mode sampling (1) or temperature-mode sampling (0).
#define SENSOR_MODE_CAPACITOR 1

#if SENSOR_MODE_CAPACITOR
#include "CapacitorSensor.h"
#else
#include "TempSensor.h"
#endif

// Total sampling window duration before capture auto-completes.
constexpr unsigned long SAMPLE_DURATION_MS = 10000;
// Maximum number of samples stored in RAM during a run.
constexpr size_t MAX_SAMPLES = 100;
// Number of bins used for value and interval histograms.
constexpr size_t HISTOGRAM_BINS = 20;
// Significant digits used for chart min/max labels on the Feather display.
constexpr uint8_t CHART_MIN_MAX_SIGNIFICANT_DIGITS = 3;
// Delay between serial dump rows to avoid overwhelming the serial link.
constexpr unsigned long PRINT_INTERVAL_MS = 2;
// Delay after capture completion before auto-starting the serial dump.
constexpr unsigned long AUTO_DUMP_DELAY_MS = 2000;

#if SENSOR_MODE_CAPACITOR
// Digital pin used to charge the capacitor-based sensor.
constexpr uint8_t CHARGE_PIN = 5;
// Digital pin used to read/discharge the capacitor-based sensor.
constexpr uint8_t SENSE_PIN = 6;
// Charge-time threshold used by CapacitorSensor change detection.
constexpr uint16_t CAPACITOR_CHANGE_THRESHOLD_US = 350;
#endif

Feather feather;

/// <summary>
/// Encapsulates sensor-mode-specific behavior for capacitor and temperature capture modes.
/// </summary>
class TestSensor
{
private:
#if SENSOR_MODE_CAPACITOR
   CapacitorSensor _sensor;
#else
   TempSensor _sensor;
#endif

public:
   /// <summary>
   /// Initializes the underlying sensor instance for the configured mode.
   /// </summary>
   TestSensor()
#if SENSOR_MODE_CAPACITOR
      : _sensor(CHARGE_PIN, SENSE_PIN, CAPACITOR_CHANGE_THRESHOLD_US)
#endif
   {
   }

   /// <summary>
   /// Starts the underlying sensor.
   /// </summary>
   void begin()
   {
      _sensor.begin();
   }

   /// <summary>
   /// Gets the display/summary unit string for the active sensor mode.
   /// </summary>
   /// <returns>The unit string for sensor values.</returns>
   const char* unit() const
   {
#if SENSOR_MODE_CAPACITOR
      return "us";
#else
      return "F";
#endif
   }

   /// <summary>
   /// Processes available samples for the active sensor mode.
   /// </summary>
   /// <param name="maxSamples">Maximum number of samples to process in this call.</param>
   /// <param name="sampleHandler">Callback invoked for each sample value and sample-end micros timestamp.</param>
   void processSamples(size_t maxSamples, void (*sampleHandler)(float, unsigned long))
   {
#if SENSOR_MODE_CAPACITOR
      float queuedChargeMicros = 0;
      uint64_t queuedEndMicros = 0;

      size_t processed = 0;
      while ((processed < maxSamples) && _sensor.tryDequeue(queuedChargeMicros, queuedEndMicros))
      {
         sampleHandler(queuedChargeMicros, (unsigned long)queuedEndMicros);
         processed++;
      }
#else
      if (maxSamples == 0)
      {
         return;
      }

      sampleHandler(_sensor.readTemperatureF(), micros());
#endif
   }
};

TestSensor testSensor;

class DataPoint
{
public:
   float value;
   unsigned long micros;
};

DataPoint samples[MAX_SAMPLES];
size_t sampleCount = 0;

Timer captureTimer(SAMPLE_DURATION_MS);
Timer printTimer(PRINT_INTERVAL_MS);
Timer autoDumpTimer(AUTO_DUMP_DELAY_MS);

bool captureComplete = false;
bool dumpInProgress = false;
bool autoDumpPending = false;
size_t dumpIndex = 0;

unsigned long sampleStartMicros = 0;
bool hasAcceptedValue = false;

void processSample(float value, unsigned long sampleEndMicros)
{
   if (!isfinite(value))
   {
      sampleStartMicros = sampleEndMicros;
      return;
   }

   if (!hasAcceptedValue)
   {
      // Use the first accepted value only as baseline; start storing from the second.
      hasAcceptedValue = true;
      sampleStartMicros = sampleEndMicros;
      return;
   }

   unsigned long intervalMicros = sampleEndMicros - sampleStartMicros;

   samples[sampleCount].value = value;
   samples[sampleCount].micros = intervalMicros;
   sampleCount++;
   sampleStartMicros = sampleEndMicros;
}

String toSignificantString(float value, uint8_t significantDigits)
{
   if (!isfinite(value))
   {
      return "n/a";
   }

   if (value == 0.0f)
   {
      return "0";
   }

   float absValue = fabsf(value);
   int exponent = (int)floorf(log10f(absValue));
   int decimals = (int)significantDigits - 1 - exponent;
   if (decimals <= 0)
   {
      long truncated = (long)value;
      return String(truncated);
   }

   return String(value, (unsigned int)decimals);
}

void printStatsRow(const char* label, float average, float stdDev, float minValue, float maxValue, uint8_t decimals)
{
   float range = (isfinite(minValue) && isfinite(maxValue)) ? (maxValue - minValue) : NAN;

   SerialX::print(String(label), 20);
   SerialX::print(average, decimals, 12);
   SerialX::print(stdDev, decimals, 12);
   SerialX::print(minValue, decimals, 12);
   SerialX::print(maxValue, decimals, 12);
   SerialX::println(range, decimals, 12);
}

void printHistogramBins(const char* title, const Histogram<HISTOGRAM_BINS>& histogram, uint8_t decimals)
{
   if (histogram.count() == 0)
   {
      Serial.println(title);
      Serial.println("No data");
      Serial.println();
      return;
   }

   const float minValue = histogram.min();
   const float maxValue = histogram.max();
   const float span = maxValue - minValue;

   Serial.println(title);
   SerialX::print("Start", 12);
   SerialX::print("End", 12);
   SerialX::println("Count", 10);

   for (size_t i = 0; i < histogram.bins(); i++)
   {
      float startValue = minValue + (span * i) / histogram.bins();
      float endValue = minValue + (span * (i + 1)) / histogram.bins();
      if (i == histogram.bins() - 1)
      {
         endValue = maxValue;
      }

      SerialX::print(startValue, decimals, 12);
      SerialX::print(endValue, decimals, 12);
      SerialX::println((unsigned long)histogram.bin(i), 10);
   }

   SerialX::print("Total Samples", 24);
   SerialX::println((unsigned long)histogram.count(), 10);

   Serial.println();
}

void buildValueHistogram(Histogram<HISTOGRAM_BINS>& histogram)
{
   for (size_t i = 0; i < sampleCount; i++)
   {
      histogram.add(samples[i].value);
   }
}

void buildIntervalHistogram(Histogram<HISTOGRAM_BINS>& histogram)
{
   for (size_t i = 0; i < sampleCount; i++)
   {
      histogram.add((float)samples[i].micros);
   }
}

void drawHistogramOnFeather(const char* title, const Histogram<HISTOGRAM_BINS>& histogram, int16_t sectionLeft, int16_t sectionWidth, int16_t sectionTop, int16_t sectionHeight, Color barColor)
{
   if ((sectionHeight <= 16) || (sectionWidth <= 8))
   {
      return;
   }

   feather.setTextSize(2);
   feather.setCursor(sectionLeft, sectionTop);
   feather.println(title, Color::LABEL);

   int16_t chartTop = feather.getCursorY() + 1;
   int16_t labelHeight = feather.charH() + 2;
   int16_t sectionBottom = sectionTop + sectionHeight;
   int16_t chartBottom = sectionBottom - labelHeight;
   int16_t chartHeight = chartBottom - chartTop;
   if (chartHeight <= 2)
   {
      return;
   }

   feather.fillRect(sectionLeft, chartTop, sectionWidth, chartHeight, Color::BLACK);
   feather.fillRect(sectionLeft, chartBottom - 1, sectionWidth, 1, Color::DARKGRAY);

   uint32_t maxBin = 0;
   for (size_t i = 0; i < histogram.bins(); i++)
   {
      maxBin = std::max(maxBin, histogram.bin(i));
   }

   if (maxBin > 0)
   {
      for (size_t i = 0; i < histogram.bins(); i++)
      {
         int16_t x0 = sectionLeft + (int16_t)((i * sectionWidth) / histogram.bins());
         int16_t x1 = sectionLeft + (int16_t)(((i + 1) * sectionWidth) / histogram.bins());
         int16_t barWidth = std::max<int16_t>(1, x1 - x0);
         uint32_t binCount = histogram.bin(i);
         int16_t barHeight = (int16_t)((binCount * (uint32_t)(chartHeight - 1)) / maxBin);
         if ((binCount > 0) && (barHeight < 1))
         {
            barHeight = 1;
         }

         if (barHeight > 0)
         {
            feather.fillRect(x0, chartBottom - 1 - barHeight, barWidth, barHeight, barColor);
         }
      }
   }

   String minLabel = "n/a";
   String maxLabel = "n/a";
   if (histogram.count() > 0)
   {
      minLabel = toSignificantString(histogram.min(), CHART_MIN_MAX_SIGNIFICANT_DIGITS);
      maxLabel = toSignificantString(histogram.max(), CHART_MIN_MAX_SIGNIFICANT_DIGITS);
   }

   feather.setCursor(sectionLeft, chartBottom + 1);
   feather.print(minLabel, barColor);

   int16_t maxLabelX = sectionLeft + sectionWidth - feather.display.textWidth(maxLabel.c_str());
   if (maxLabelX < sectionLeft)
   {
      maxLabelX = sectionLeft;
   }
   feather.setCursor(maxLabelX, chartBottom + 1);
   feather.print(maxLabel, barColor);
}

void renderHistogramsOnFeather()
{
   feather.clearDisplay();

   Histogram<HISTOGRAM_BINS> valueHistogram;
   Histogram<HISTOGRAM_BINS> intervalHistogram;
   buildValueHistogram(valueHistogram);
   buildIntervalHistogram(intervalHistogram);

   feather.setTextSize(3);
   feather.setCursor(0, 0);
   feather.println("Results", Color::HEADING);

   feather.setTextSize(2);
   int16_t top = feather.getCursorY() + 2;
   int16_t messageHeight = feather.charH() + 2;
   int16_t availableHeight = (int16_t)feather.height() - top - messageHeight;
   int16_t totalWidth = (int16_t)feather.width();
   int16_t gap = 4;
   int16_t columnWidth = (totalWidth - gap) / 2;
   int16_t leftX = 0;
   int16_t rightX = leftX + columnWidth + gap;

   drawHistogramOnFeather("Values", valueHistogram, leftX, columnWidth, top, availableHeight, Color::VALUE2);
   drawHistogramOnFeather("Micros", intervalHistogram, rightX, columnWidth, top, availableHeight, Color::VALUE3);

   feather.setCursor(0, top + availableHeight + 1);
   feather.println("Button A to dump to Serial", Color::GRAY);
}

void printCaptureSummary()
{
   double valueSum = 0.0;
   double valueSumSquares = 0.0;
   float valueMin = NAN;
   float valueMax = NAN;
   size_t valueCount = 0;

   double intervalSum = 0.0;
   double intervalSumSquares = 0.0;
   float intervalMin = NAN;
   float intervalMax = NAN;
   size_t intervalCount = 0;

   uint64_t captureSpanMicros = 0;

   for (size_t i = 0; i < sampleCount; i++)
   {
      float value = samples[i].value;
      if (isfinite(value))
      {
         valueSum += value;
         valueSumSquares += static_cast<double>(value) * static_cast<double>(value);

         if (valueCount == 0)
         {
            valueMin = value;
            valueMax = value;
         }
         else
         {
            valueMin = min(valueMin, value);
            valueMax = max(valueMax, value);
         }

         valueCount++;
      }

      unsigned long dtMicros = samples[i].micros;
      float interval = static_cast<float>(dtMicros);
      if (isfinite(interval))
      {
         intervalSum += interval;
         intervalSumSquares += static_cast<double>(interval) * static_cast<double>(interval);

         if (intervalCount == 0)
         {
            intervalMin = interval;
            intervalMax = interval;
         }
         else
         {
            intervalMin = min(intervalMin, interval);
            intervalMax = max(intervalMax, interval);
         }

         intervalCount++;
      }

      captureSpanMicros += static_cast<uint64_t>(dtMicros);
   }

   float valueAvg = NAN;
   float valueStdDev = NAN;
   if (valueCount > 0)
   {
      const double count = static_cast<double>(valueCount);
      const double mean = valueSum / count;
      const double variance = max(0.0, (valueSumSquares / count) - (mean * mean));
      valueAvg = static_cast<float>(mean);
      valueStdDev = static_cast<float>(sqrt(variance));
   }

   float intervalAvg = NAN;
   float intervalStdDev = NAN;
   if (intervalCount > 0)
   {
      const double count = static_cast<double>(intervalCount);
      const double mean = intervalSum / count;
      const double variance = max(0.0, (intervalSumSquares / count) - (mean * mean));
      intervalAvg = static_cast<float>(mean);
      intervalStdDev = static_cast<float>(sqrt(variance));
   }

   float overallRateHz = NAN;
   float captureSpanMs = NAN;
   if ((sampleCount > 0) && (captureSpanMicros > 0))
   {
      captureSpanMs = static_cast<float>(captureSpanMicros / 1000.0);
      overallRateHz = static_cast<float>((static_cast<double>(sampleCount) * 1000000.0) / static_cast<double>(captureSpanMicros));
   }

   long overallRatePerSec = isfinite(overallRateHz) ? lroundf(overallRateHz) : 0;
   String sensorMetric = String("Sensor value (") + testSensor.unit() + ")";

   Serial.println();
   Serial.println("Capture Summary");
   SerialX::print("Metric", 20);
   SerialX::println("Value", 20);
   SerialX::print("Capture ms", 20);
   SerialX::println(captureSpanMs, 3, 20);
   SerialX::print("Samples", 20);
   SerialX::println((unsigned long)sampleCount, 20);
   SerialX::print("Overall rate", 20);
   SerialX::println(String(overallRatePerSec) + "/s", 20);
   Serial.println();

   SerialX::print("Metric", 20);
   SerialX::print("Avg", 12);
   SerialX::print("StdDev", 12);
   SerialX::print("Min", 12);
   SerialX::print("Max", 12);
   SerialX::println("Range", 12);
   printStatsRow(sensorMetric.c_str(), valueAvg, valueStdDev, valueMin, valueMax, 3);
   printStatsRow("Sample interval (us)", intervalAvg, intervalStdDev, intervalMin, intervalMax, 2);
   Serial.println();

   String valueHistogramTitle = String("Sensor Value Histogram (") + testSensor.unit() + ")";
   Histogram<HISTOGRAM_BINS> valueHistogram;
   Histogram<HISTOGRAM_BINS> intervalHistogram;
   buildValueHistogram(valueHistogram);
   buildIntervalHistogram(intervalHistogram);
   printHistogramBins(valueHistogramTitle.c_str(), valueHistogram, 3);
   printHistogramBins("Sample Interval Histogram (us)", intervalHistogram, 2);
}

void updateDisplay()
{
   feather.setCursor(0, 0);
   feather.setTextSize(3);
   feather.println("Sensor Capture", Color::HEADING);
   feather.moveCursorY(10);

   feather.setTextSize(2);
   if (!captureComplete)
   {
      feather.println("Collecting data", Color::VALUE);
      feather.println("Monitor Serial", Color::LABEL);
   }
   else
   {
      feather.println("Run complete", Color::VALUE);
      feather.println("Press A to dump", Color::SUB_LABEL);
   }
}

void finishCapture()
{
   if (captureComplete)
   {
      return;
   }

   captureComplete = true;
   autoDumpTimer = Timer(AUTO_DUMP_DELAY_MS);
   autoDumpPending = true;
   printCaptureSummary();
   renderHistogramsOnFeather();
}

void startDump()
{
   if (sampleCount == 0)
   {
      Serial.println("No samples captured");
      return;
   }

   dumpInProgress = true;
   dumpIndex = 0;

   Serial.println("Dump start");
   SerialX::print("Index", 8);
   SerialX::print("Delta(us)", 12);
   SerialX::println("Value", 12);
   SerialX::print("-----", 8);
   SerialX::print("---------", 12);
   SerialX::println("-----------", 12);
}

void setup()
{
   SerialX::begin(115200, 2000);
   feather.begin();
   testSensor.begin();

   updateDisplay();
   Serial.println("Sampling started...");

   // give the sensor some time to stabilize before starting the capture. Especially
   // important for the capacitor sensor.
   delay(100);
}

void loop()
{
   if (!captureComplete)
   {
      if ((sampleCount >= MAX_SAMPLES) || captureTimer.ready())
      {
         finishCapture();
      }
      else
      {
         size_t samplesRemaining = MAX_SAMPLES - sampleCount;
         testSensor.processSamples(samplesRemaining, processSample);
      }
   }

   if (captureComplete && autoDumpPending && !dumpInProgress && autoDumpTimer.ready())
   {
      autoDumpPending = false;
      startDump();
   }

   if (captureComplete && feather.buttonA.wasPressed() && !dumpInProgress)
   {
      autoDumpPending = false;
      startDump();
   }

   if (dumpInProgress && printTimer.ready())
   {
      if (dumpIndex < sampleCount)
      {
         unsigned long deltaMicros = samples[dumpIndex].micros;

         SerialX::print((unsigned long)dumpIndex, 8);
         SerialX::print(deltaMicros, 12);
         SerialX::println(samples[dumpIndex].value, 3, 12);
         dumpIndex++;
      }
      else
      {
         dumpInProgress = false;
         Serial.println("Dump complete");
      }
   }
}
