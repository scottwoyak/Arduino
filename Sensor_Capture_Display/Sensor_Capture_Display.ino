#include "Feather.h"
#include "SerialX.h"
#include "Histogram.h"
#include "Stats.h"
#include "StdDev.h"
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
constexpr size_t MAX_SAMPLES = 10000;
// Number of bins used for value and interval histograms.
constexpr size_t HISTOGRAM_BINS = 20;
// Significant digits used for chart min/max labels on the Feather display.
constexpr uint8_t CHART_MIN_MAX_SIGNIFICANT_DIGITS = 3;
// Delay between serial dump rows to avoid overwhelming the serial link.
constexpr unsigned long PRINT_INTERVAL_MS = 2;

#if SENSOR_MODE_CAPACITOR
// Digital pin used to charge the capacitor-based sensor.
constexpr uint8_t CHARGE_PIN = 5;
// Digital pin used to read/discharge the capacitor-based sensor.
constexpr uint8_t SENSE_PIN = 6;
// Charge-time threshold used by CapacitorSensor change detection.
constexpr uint16_t CAPACITOR_CHANGE_THRESHOLD_US = 350;
// Unit label used in summaries and chart labeling for capacitor mode.
constexpr const char* SENSOR_VALUE_UNIT = "us";
CapacitorSensor sensor(CHARGE_PIN, SENSE_PIN, CAPACITOR_CHANGE_THRESHOLD_US);
#else
// Unit label used in summaries and chart labeling for temperature mode.
constexpr const char* SENSOR_VALUE_UNIT = "F";
TempSensor sensor;
#endif

Feather feather;
float samples[MAX_SAMPLES];
unsigned long sampleTimesMicros[MAX_SAMPLES];
size_t sampleCount = 0;

Timer captureTimer(SAMPLE_DURATION_MS);
Timer printTimer(PRINT_INTERVAL_MS);

bool captureComplete = false;
bool dumpInProgress = false;
size_t dumpIndex = 0;

float readSensor()
{
#if SENSOR_MODE_CAPACITOR
   return (float)sensor.chargeTimeMicros();
#else
   return sensor.readTemperatureF();
#endif
}

bool sampleAvailable()
{
#if SENSOR_MODE_CAPACITOR
   return sensor.hasChanged();
#else
   return true;
#endif
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

void printStatsRow(const char* label, const Stats& stats, const StdDev& stddev, uint8_t decimals)
{
   float minValue = stats.min();
   float maxValue = stats.max();
   float range = (isfinite(minValue) && isfinite(maxValue)) ? (maxValue - minValue) : NAN;

   SerialX::print(String(label), 20);
   SerialX::print(stats.get(), decimals, 12);
   SerialX::print(stddev.get(), decimals, 12);
   SerialX::print(minValue, decimals, 12);
   SerialX::print(maxValue, decimals, 12);
   SerialX::println(range, decimals, 12);
}

void printHistogramBins(const char* title, const char* unit, const Histogram<HISTOGRAM_BINS>& histogram, uint8_t decimals)
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

   if (unit != nullptr)
   {
      SerialX::print("Unit", 12);
      SerialX::println(String(unit), 12);
   }

   Serial.println();
}

void printHistogram(const char* title, const char* unit, const float* values, size_t count, uint8_t decimals)
{
   Histogram<HISTOGRAM_BINS> histogram;
   for (size_t i = 0; i < count; i++)
   {
      histogram.add(values[i]);
   }

   printHistogramBins(title, unit, histogram, decimals);
}

void printIntervalHistogram(uint8_t decimals)
{
   Histogram<HISTOGRAM_BINS> histogram;
   for (size_t i = 1; i < sampleCount; i++)
   {
      histogram.add((float)(sampleTimesMicros[i] - sampleTimesMicros[i - 1]));
   }

   printHistogramBins("Sample Interval Histogram (us)", "us", histogram, decimals);
}

void buildValueHistogram(Histogram<HISTOGRAM_BINS>& histogram)
{
   for (size_t i = 0; i < sampleCount; i++)
   {
      histogram.add(samples[i]);
   }
}

void buildIntervalHistogram(Histogram<HISTOGRAM_BINS>& histogram)
{
   for (size_t i = 1; i < sampleCount; i++)
   {
      histogram.add((float)(sampleTimesMicros[i] - sampleTimesMicros[i - 1]));
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

   drawHistogramOnFeather("Value", valueHistogram, leftX, columnWidth, top, availableHeight, Color::VALUE2);
   drawHistogramOnFeather("Micros", intervalHistogram, rightX, columnWidth, top, availableHeight, Color::VALUE3);

   feather.setCursor(0, top + availableHeight + 1);
   feather.println("Button A to dump to Serial", Color::GRAY);
}

void printCaptureSummary()
{
   Stats valueStats;
   StdDev valueStdDev;

   for (size_t i = 0; i < sampleCount; i++)
   {
      valueStats.add(samples[i]);
      valueStdDev.add(samples[i]);
   }

   Stats intervalStats;
   StdDev intervalStdDev;

   for (size_t i = 1; i < sampleCount; i++)
   {
      unsigned long dtMicros = sampleTimesMicros[i] - sampleTimesMicros[i - 1];
      intervalStats.add((float)dtMicros);
      intervalStdDev.add((float)dtMicros);
   }

   float overallRateHz = NAN;
   float captureSpanMs = NAN;
   if (sampleCount > 1)
   {
      unsigned long captureSpanMicros = sampleTimesMicros[sampleCount - 1] - sampleTimesMicros[0];
      if (captureSpanMicros > 0)
      {
         captureSpanMs = captureSpanMicros / 1000.0f;
         overallRateHz = ((sampleCount - 1) * 1000000.0f) / captureSpanMicros;
      }
   }

   long overallRatePerSec = isfinite(overallRateHz) ? lroundf(overallRateHz) : 0;
   String sensorMetric = String("Sensor value (") + SENSOR_VALUE_UNIT + ")";

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
   printStatsRow(sensorMetric.c_str(), valueStats, valueStdDev, 3);
   printStatsRow("Sample interval (us)", intervalStats, intervalStdDev, 2);
   Serial.println();

   String valueHistogramTitle = String("Sensor Value Histogram (") + SENSOR_VALUE_UNIT + ")";
   printHistogram(valueHistogramTitle.c_str(), SENSOR_VALUE_UNIT, samples, sampleCount, 3);
   printIntervalHistogram(2);
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
   SerialX::begin(115200, 1000);
   feather.begin();
   sensor.begin();

   updateDisplay();
   Serial.println("Sampling started...");
}

void loop()
{
   if (!captureComplete)
   {
      if ((sampleCount >= MAX_SAMPLES) || captureTimer.ready())
      {
         finishCapture();
      }
      else if (sampleAvailable())
      {
         samples[sampleCount] = readSensor();
         sampleTimesMicros[sampleCount] = micros();
         sampleCount++;
      }
   }

   if (captureComplete && feather.buttonA.wasPressed() && !dumpInProgress)
   {
      startDump();
   }

   if (dumpInProgress && printTimer.ready())
   {
      if (dumpIndex < sampleCount)
      {
         unsigned long deltaMicros = 0;
         if (dumpIndex > 0)
         {
            deltaMicros = sampleTimesMicros[dumpIndex] - sampleTimesMicros[dumpIndex - 1];
         }

         SerialX::print((unsigned long)dumpIndex, 8);
         SerialX::print(deltaMicros, 12);
         SerialX::println(samples[dumpIndex], 3, 12);
         dumpIndex++;
      }
      else
      {
         dumpInProgress = false;
         Serial.println("Dump complete");
      }
   }
}
