// -------------------------------------------------------------------------------------------------
// Sensor_Noise_Display
//
// This sketch characterizes sensor noise over multiple rolling windows and shows both serial and
// on-device summaries of stability. It continuously ingests sensor readings, tracks rolling average
// and standard deviation behavior, and reports rate/variation metrics for several sample sizes.
//
// Behavior summary:
// - Reads capacitor sensor events from a queue (or direct temperature reads in temp mode).
// - Tracks per-window rolling statistics (N1, N10, N100, N500 by default).
// - Periodically prints a serial table with sensor-level and rolling-window noise metrics.
// - Continuously updates Feather display with effective rate, average, stddev, and average range.
//
// Notes:
// - Capacitor mode is the default and uses tryDequeue() so burst events are not collapsed.
// - Display and serial refresh are timer-driven to avoid over-updating output paths.
// -------------------------------------------------------------------------------------------------
#include "Feather.h"
#include "SerialX.h"
#include "RollingStats.h"
#include "RollingRate.h"
#include "Stats.h"
#include "Timer.h"

#define SENSOR_MODE_CAPACITOR 1

#if SENSOR_MODE_CAPACITOR
#include "CapacitorSensor.h"
#else
#include "TempSensor.h"
#endif

//
// This sketch continuously reads sensor values, shows rolling averages
// for multiple sample windows, and reports variability metrics.
//
#if SENSOR_MODE_CAPACITOR
constexpr size_t NUM_ROLLING_SAMPLES_FOR_DISPLAYED_VALUE_RANGE = 5000;
#else
constexpr size_t NUM_ROLLING_SAMPLES_FOR_DISPLAYED_VALUE_RANGE = 100;
#endif

constexpr uint16_t SAMPLE_RATE_WINDOW_SIZE = 200;
constexpr unsigned long DISPLAY_INTERVAL_MS = 100;
constexpr unsigned long SERIAL_PRINT_INTERVAL_MS = 5000;

class Test
{
private:
   const char* _label = "";
   RollingStats _stats;
   RollingStats _averageStdDev = RollingStats(NUM_ROLLING_SAMPLES_FOR_DISPLAYED_VALUE_RANGE);
   RollingStats _averageRange = RollingStats(NUM_ROLLING_SAMPLES_FOR_DISPLAYED_VALUE_RANGE);
   RollingStats _stdDevRange = RollingStats(NUM_ROLLING_SAMPLES_FOR_DISPLAYED_VALUE_RANGE);
   uint32_t _samplesCollected = 0;

public:
   Test(const char* label, size_t sampleSize)
      : _label(label), _stats(sampleSize)
   {}

   void set(float value)
   {
      _samplesCollected++;
      _stats.set(value);

      if (_samplesCollected < _stats.size())
      {
         return;
      }

      float avg = _stats.average();
      if (!isfinite(avg))
      {
         return;
      }

      _averageStdDev.set(avg);
      _averageRange.set(avg);

      float sigma = _averageStdDev.stdDev();
      if (isfinite(sigma))
      {
         _stdDevRange.set(sigma);
      }
   }

   const char* label() const
   {
      return _label;
   }

   float average() const
   {
      return _stats.average();
   }

   size_t sampleSize() const
   {
      return _stats.size();
   }

   bool enoughSamplesCollected() const
   {
      return _samplesCollected >= _stats.size();
   }

   float stdDev() const
   {
      if (!enoughSamplesCollected())
      {
         return NAN;
      }

      return _averageStdDev.stdDev();
   }

   float avgRange() const
   {
      if (!enoughSamplesCollected())
      {
         return NAN;
      }

      return _averageRange.range();
   }

   float stdDevRange() const
   {
      if (!enoughSamplesCollected())
      {
         return NAN;
      }

      return _stdDevRange.range();
   }

   float averageMin() const
   {
      return enoughSamplesCollected() ? _averageRange.min() : NAN;
   }

   float averageMax() const
   {
      return enoughSamplesCollected() ? _averageRange.max() : NAN;
   }

   float stdDevMin() const
   {
      return enoughSamplesCollected() ? _stdDevRange.min() : NAN;
   }

   float stdDevMax() const
   {
      return enoughSamplesCollected() ? _stdDevRange.max() : NAN;
   }

   };

#if SENSOR_MODE_CAPACITOR
constexpr uint8_t CHARGE_PIN = 5;
constexpr uint8_t SENSE_PIN = 6;
#endif

Feather feather;
#if SENSOR_MODE_CAPACITOR
CapacitorSensor sensor(CHARGE_PIN, SENSE_PIN, 350);
#else
TempSensor sensor;
#endif
RollingRate sampleRate(SAMPLE_RATE_WINDOW_SIZE);
Timer displayTimer(DISPLAY_INTERVAL_MS);
Timer serialTimer(SERIAL_PRINT_INTERVAL_MS);
Test tests[] =
{
   { "  N1", 1 },
   { " N10", 10 },
   { "N100", 100 },
   { "N500", 500 }
};
constexpr uint8_t NUM_TESTS = sizeof(tests) / sizeof(tests[0]);
uint32_t sampleCount = 0;
Stats serialSensorStats;

Format windowFormat(4, Format::Alignment::RIGHT);
Format valueFormat("###.#", Format::Alignment::RIGHT);
Format sigmaFormat("##.##", Format::Alignment::RIGHT);
Format rangeFormat("##.#", Format::Alignment::RIGHT);
Format effectiveRateFormat("####", Format::Alignment::RIGHT);
Format rateFormat("####/s", Format::Alignment::RIGHT);

void setup()
{
   SerialX::begin(115200, 1000);
   feather.begin();

   sensor.begin();
}

float readSensor()
{
#if SENSOR_MODE_CAPACITOR
   return (float)sensor.chargeTimeMicros();
#else
   return sensor.readTemperatureF();
#endif
}

float readSensorRate()
{
   return sampleRate.get();
}

void processSensorValue(float value)
{
   serialSensorStats.add(value);

   sampleCount++;
   for (uint8_t i = 0; i < NUM_TESTS; i++)
   {
      tests[i].set(value);
   }
}

void printSerialHeader()
{
   Serial.println();
   SerialX::print("Test", 8);
   SerialX::print("Rate", 10);

   SerialX::print("Sensor", 10);
   SerialX::print("Range", 10);
   SerialX::print("Min", 10);
   SerialX::print("Max", 10);
   SerialX::print("  ", 4);

   SerialX::print("Avg", 10);
   SerialX::print("Range", 10);
   SerialX::print("Min", 10);
   SerialX::print("Max", 10);
   SerialX::print("  ", 4);

   SerialX::print("StdDev", 10);
   SerialX::print("Range", 10);
   SerialX::print("Min", 10);
   SerialX::println("Max", 10);

   SerialX::print("----", 8);
   SerialX::print("--------", 10);

   SerialX::print("--------", 10);
   SerialX::print("--------", 10);
   SerialX::print("--------", 10);
   SerialX::print("--------", 10);
   SerialX::print("  ", 4);

   SerialX::print("--------", 10);
   SerialX::print("--------", 10);
   SerialX::print("--------", 10);
   SerialX::print("--------", 10);
   SerialX::print("  ", 4);

   SerialX::print("--------", 10);
   SerialX::print("--------", 10);
   SerialX::print("--------", 10);
   SerialX::println("--------", 10);
}

void printSerialValues(uint16_t samplesPerSecond)
{
   printSerialHeader();

   String sensorRate = String(samplesPerSecond) + "/s";

   float sensorAvg = serialSensorStats.get();
   float sensorMin = serialSensorStats.min();
   float sensorMax = serialSensorStats.max();
   float sensorRange = (isfinite(sensorMin) && isfinite(sensorMax)) ? (sensorMax - sensorMin) : NAN;
   float sensorStdDev = serialSensorStats.stdDev();

   SerialX::print("Sensor", 8);
   SerialX::print(sensorRate, 10);
   SerialX::print(isfinite(sensorAvg) ? String(sensorAvg, 2) : "----", 10);
   SerialX::print(isfinite(sensorRange) ? String(sensorRange, 3) : "----", 10);
   SerialX::print(isfinite(sensorMin) ? String(sensorMin, 3) : "----", 10);
   SerialX::print(isfinite(sensorMax) ? String(sensorMax, 3) : "----", 10);
   SerialX::print("  ", 4);
   SerialX::print(isfinite(sensorAvg) ? String(sensorAvg, 2) : "----", 10);
   SerialX::print(isfinite(sensorRange) ? String(sensorRange, 3) : "----", 10);
   SerialX::print(isfinite(sensorMin) ? String(sensorMin, 3) : "----", 10);
   SerialX::print(isfinite(sensorMax) ? String(sensorMax, 3) : "----", 10);
   SerialX::print("  ", 4);
   SerialX::print(isfinite(sensorStdDev) ? String(sensorStdDev, 3) : "----", 10);
   SerialX::print("----", 10);
   SerialX::print("----", 10);
   SerialX::println("----", 10);

   for (uint8_t i = 0; i < NUM_TESTS; i++)
   {
      SerialX::print(tests[i].label(), 8);

      float effectiveRateValue = (tests[i].sampleSize() > 0)
         ? ((float)samplesPerSecond / tests[i].sampleSize())
         : NAN;
      String effectiveRate = isfinite(effectiveRateValue)
         ? (String((int)round(effectiveRateValue)) + "/s")
         : "----";
      SerialX::print(effectiveRate, 10);

      SerialX::print("----", 10);
      SerialX::print("----", 10);
      SerialX::print("----", 10);
      SerialX::print("----", 10);
      SerialX::print("  ", 4);

      if (tests[i].enoughSamplesCollected())
      {
         SerialX::print(String(tests[i].average(), 2), 10);
         SerialX::print(String(tests[i].avgRange(), 3), 10);
         SerialX::print(String(tests[i].averageMin(), 3), 10);
         SerialX::print(String(tests[i].averageMax(), 3), 10);
         SerialX::print("  ", 4);

         SerialX::print(String(tests[i].stdDev(), 3), 10);
         SerialX::print(String(tests[i].stdDevRange(), 3), 10);
         SerialX::print(String(tests[i].stdDevMin(), 3), 10);
         SerialX::println(String(tests[i].stdDevMax(), 3), 10);
      }
      else
      {
         SerialX::print("----", 10);
         SerialX::print("----", 10);
         SerialX::print("----", 10);
         SerialX::print("----", 10);
         SerialX::print("  ", 4);

         SerialX::print("----", 10);
         SerialX::print("----", 10);
         SerialX::print("----", 10);
         SerialX::println("----", 10);
      }
   }

   serialSensorStats.reset();
}

void loop()
{
   bool receivedSample = false;

#if SENSOR_MODE_CAPACITOR
   uint32_t chargeTime = 0;
   while (sensor.tryDequeue(chargeTime))
   {
      receivedSample = true;
      sampleRate.tick();
      sampleRate.pause();

      processSensorValue((float)chargeTime);

      sampleRate.resume();
   }
#else
   sampleRate.tick();
   sampleRate.pause();

   processSensorValue(readSensor());

   sampleRate.resume();
   receivedSample = true;
#endif

   if (!receivedSample)
   {
      return;
   }

   if (serialTimer.ready())
   {
      float sensorRate = readSensorRate();
      uint16_t samplesPerSecond = isfinite(sensorRate) ? (uint16_t)round(sensorRate) : 0;
      printSerialValues(samplesPerSecond);
   }

   if (!displayTimer.ready())
   {
      return;
   }

   feather.setCursor(0, 0);
   feather.setTextSize(3);
   feather.println("Sensor Noise", Color::HEADING);
   feather.moveCursorY(2);

   feather.setTextSize(2);
   float sensorRate = readSensorRate();

   feather.print("Num", windowFormat, Color::WHITE);
   feather.print(" ", Color::WHITE);
   feather.print("Rate", effectiveRateFormat, Color::WHITE);
   feather.print(" ", Color::WHITE);
   feather.print("Avg", valueFormat, Color::WHITE);
   feather.print(" ", Color::WHITE);
   feather.print("Std", sigmaFormat, Color::WHITE);
   feather.print(" ", Color::WHITE);
   feather.println("Rng", rangeFormat, Color::WHITE);

   for (uint8_t i = 0; i < NUM_TESTS; i++)
   {
      feather.print(tests[i].label(), windowFormat, Color::LABEL);
      feather.print(" ", Color::LABEL);

      float effectiveRate = (tests[i].sampleSize() > 0)
         ? (sensorRate / tests[i].sampleSize())
         : NAN;

      if (isfinite(effectiveRate))
      {
         feather.print(effectiveRate, effectiveRateFormat, Color::VALUE);
      }
      else
      {
         feather.print("---", Color::GRAY);
      }
      feather.print(" ");

      if (tests[i].enoughSamplesCollected())
      {
         feather.print(tests[i].average(), valueFormat, Color::VALUE2);
         feather.print(" ");
         feather.print(tests[i].stdDev(), sigmaFormat, Color::VALUE3);
         feather.print(" ");
         feather.println(tests[i].avgRange(), rangeFormat, Color::VALUE3);
      }
      else
      {
         feather.print("-----", Color::GRAY);
         feather.print(" ", Color::GRAY);
         feather.print("-----", Color::GRAY);
         feather.print(" ", Color::GRAY);
         feather.println("----", Color::GRAY);
      }
   }

   uint16_t samplesPerSecond = isfinite(sensorRate) ? (uint16_t)round(sensorRate) : 0;

   feather.setTextSize(2);
   feather.setCursorY(-feather.charH());
   feather.print("Samples: ", Color::GRAY);
   feather.print(sampleCount, Color::GRAY);
   feather.printR(samplesPerSecond, rateFormat, Color::GRAY);
}
