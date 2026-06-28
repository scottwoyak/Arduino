// -------------------------------------------------------------------------------------------------
//
// Continuously samples either a capacitor sensor or temperature sensor and displays/prints noise
// metrics over SAMPLE_TIME_MS using TimedStats.
//
// Data flow:
// - Active sensor is sampled every SAMPLE_INTERVAL_MS.
// - TimedStats tracks average/range/count/stddev over the last SAMPLE_TIME_MS.
//
// Display flow:
// - GUI refreshes every DISPLAY_INTERVAL_MS.
// - Screen shows Avg, Range, StdDev, sample count, and raw sensor rate.
// -------------------------------------------------------------------------------------------------

#include <Arduino.h>
#include "Feather.h"
#include "SerialX.h"
#include "TimedStats.h"
#include "Timer.h"
#include "TestSensor.h"

#define SENSOR_TYPE_CAPACITOR 1
#define SENSOR_TYPE_TEMP 2
#define SENSOR_TYPE SENSOR_TYPE_CAPACITOR

constexpr uint16_t SAMPLE_TIME_MS = 1000;
constexpr uint16_t SAMPLE_INTERVAL_MS = 10;
constexpr uint16_t DISPLAY_INTERVAL_MS = static_cast<uint16_t>(1.5f * SAMPLE_TIME_MS);

#if SENSOR_TYPE == SENSOR_TYPE_CAPACITOR
constexpr uint8_t CHARGE_PIN = 9;
constexpr uint8_t SENSE_PIN = 5;
constexpr uint16_t DISCHARGE_DELAY_MICROS = 500;
#endif

Feather feather;

#if SENSOR_TYPE == SENSOR_TYPE_CAPACITOR
CapacitorTestSensor sensor(CHARGE_PIN, SENSE_PIN, DISCHARGE_DELAY_MICROS);
#elif SENSOR_TYPE == SENSOR_TYPE_TEMP
TempTestSensor sensor;
#else
#error "Invalid SENSOR_TYPE. Use SENSOR_TYPE_CAPACITOR or SENSOR_TYPE_TEMP."
#endif
Timer sampleTimer(SAMPLE_INTERVAL_MS);
Timer displayTimer(DISPLAY_INTERVAL_MS);
TimedStats stats(SAMPLE_TIME_MS);
bool serialHeaderPrinted = false;
uint16_t serialRowsPrinted = 0;

Format valueFormat("###.#", Format::Alignment::RIGHT);
Format rangeFormat("###.#", Format::Alignment::RIGHT);
Format stdDevFormat("##.##", Format::Alignment::RIGHT);
Format countFormat("#####", Format::Alignment::RIGHT);
Format rateFormat("####/s", Format::Alignment::RIGHT);

/// <summary>
/// Prints a one-time serial legend describing the active sampling and stats window.
/// </summary>
void printSerialSummary()
{
   unsigned long expectedSamples = SAMPLE_TIME_MS / SAMPLE_INTERVAL_MS;
   String sensorUnit = sensor.unit();

   Serial.println();
   Serial.println("Serial Metrics Summary");
   SerialX::println(String("- Sampling source: ") + sensor.sourceDescription(), 0);
   SerialX::println(String("- Data points are collected every: ") + SAMPLE_INTERVAL_MS + " ms", 0);
   SerialX::println(String("- Stats are averaged over: ") + SAMPLE_TIME_MS + " ms (sliding window)", 0);
   SerialX::println(String("- Expected samples per full window: ~") + expectedSamples, 0);
   Serial.println("- Num Samples: finite samples currently retained in the active stats window");
   Serial.println(String("- Avg(") + sensorUnit + "): mean of samples in the active stats window");
   Serial.println(String("- Range(") + sensorUnit + "): max-min of samples in the active stats window");
   Serial.println(String("- StdDev(") + sensorUnit + "): population standard deviation in active window");
}

/// <summary>
/// Prints a periodic serial row containing the latest windowed statistics.
/// </summary>
/// <param name="avg">Window average value.</param>
/// <param name="rng">Window range value.</param>
/// <param name="sd">Window standard deviation value.</param>
/// <param name="count">Finite sample count in the active window.</param>
void printSerialValues(float avg, float rng, float sd, size_t count)
{
   String sensorUnit = sensor.unit();

   if (!serialHeaderPrinted || (serialRowsPrinted % 25 == 0))
   {
      Serial.println();
      SerialX::print("Num Samples", 18);
      SerialX::print(String("Avg(") + sensorUnit + ")", 12);
      SerialX::print(String("Range(") + sensorUnit + ")", 12);
      SerialX::println(String("StdDev(") + sensorUnit + ")", 12);

      SerialX::print("-----------", 18);
      SerialX::print("-------", 12);
      SerialX::print("---------", 12);
      SerialX::println("------", 12);
      serialHeaderPrinted = true;
   }

   SerialX::print(count, 18);
   SerialX::print(avg, 3, 12);
   SerialX::print(rng, 3, 12);
   SerialX::println(sd, 3, 12);
   serialRowsPrinted++;
}

void setup()
{
   SerialX::begin();
   feather.begin();
   sensor.begin();
   printSerialSummary();
}

void loop()
{
   if (sampleTimer.ready())
   {
      float sensorValue = sensor.readValue();
      if (isfinite(sensorValue))
      {
         stats.set(sensorValue);
      }
   }

   if (!displayTimer.ready())
   {
      return;
   }

   float avg = stats.average();
   float minValue = stats.min();
   float maxValue = stats.max();
   float rng = (isfinite(minValue) && isfinite(maxValue)) ? (maxValue - minValue) : NAN;
   size_t count = stats.count();
   size_t maxSamplesPerWindow = SAMPLE_TIME_MS / SAMPLE_INTERVAL_MS;
   count = min(count, maxSamplesPerWindow);
   float sd = stats.stdDev();

   printSerialValues(avg, rng, sd, count);

   feather.setCursor(0, 0);
   feather.setTextSize(3);
   feather.println(sensor.title(), Color::HEADING);
   feather.moveCursorY(6);

   feather.println("   Avg: ", avg, valueFormat);
   feather.println(" Range: ", rng, rangeFormat);
   feather.println("StdDev: ", sd, stdDevFormat);
   feather.println("     N: ", count, countFormat);

   float rawRate = sensor.rawRate();
   feather.print("  Rate: ", Color::GRAY);
   if (isfinite(rawRate))
   {
      feather.println(rawRate, rateFormat, Color::GRAY);
   }
   else
   {
      feather.println(" n/a", Color::GRAY);
   }
}
