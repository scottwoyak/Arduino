//
// Continuously samples either a capacitor sensor or temperature sensor and displays noise metrics.
//
// Active sensor is sampled every SAMPLE_INTERVAL_MS. TimedStats tracks average, range, count,
// and standard deviation over the last SAMPLE_TIME_MS (sliding window). Display refreshes every
// DISPLAY_INTERVAL_MS showing all metrics plus raw sensor rate.
//

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
constexpr uint8_t SENSE_PIN = 5;
constexpr uint8_t CHARGE_PIN_1M = 6;
constexpr uint8_t CHARGE_PIN_470K = 9;
constexpr uint8_t CHARGE_PIN_100K = 10;
constexpr uint8_t CHARGE_PIN_47K = 11;
constexpr uint8_t CHARGE_PINS[] = { CHARGE_PIN_1M, CHARGE_PIN_470K, CHARGE_PIN_100K, CHARGE_PIN_47K };
constexpr const char* CHARGE_PIN_LABELS[] = { "1M", "470K", "100K", "47K" };
constexpr size_t CHARGE_PIN_COUNT = sizeof(CHARGE_PINS) / sizeof(CHARGE_PINS[0]);
constexpr uint16_t DISCHARGE_DELAY_MICROS = 500;
#endif

Feather feather;

#if SENSOR_TYPE == SENSOR_TYPE_CAPACITOR
CapacitorTestSensor sensor(CHARGE_PINS, CHARGE_PIN_LABELS, CHARGE_PIN_COUNT, SENSE_PIN, DISCHARGE_DELAY_MICROS);
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

Format valueFormat("####.#");
Format rangeFormat("####.#");
Format stdDevFormat("####.##");
Format stdDevPercentFormat("##.##%", 7);
Format countFormat("######");
Format rateFormat("####/s");

void printSerialSummary()
{
   unsigned long expectedSamples = SAMPLE_TIME_MS / SAMPLE_INTERVAL_MS;
   String sensorUnit = sensor.unit();

   Serial.println();
   Serial.println("Serial Metrics Summary");
   SerialX::println("- Sampling source: active sensor", 0);
   SerialX::println(String("- Data points are collected every: ") + SAMPLE_INTERVAL_MS + " ms", 0);
   SerialX::println(String("- Stats are averaged over: ") + SAMPLE_TIME_MS + " ms (sliding period)", 0);
   SerialX::println(String("- Expected samples per full period: ~") + expectedSamples, 0);
   Serial.println("- Num Samples: finite samples currently retained in the active stats period");
   Serial.println(String("- Avg(") + sensorUnit + "): mean of samples in the active stats period");
   Serial.println(String("- Range(") + sensorUnit + "): max-min of samples in the active stats period");
   Serial.println(String("- StdDev(") + sensorUnit + "): population standard deviation in active period");
}

void printSerialValues(TimedStats& timedStats)
{
   String sensorUnit = sensor.unit();

   float avg = timedStats.average();
   float minValue = timedStats.min();
   float maxValue = timedStats.max();
   float rng = (isfinite(minValue) && isfinite(maxValue)) ? (maxValue - minValue) : NAN;
   size_t count = timedStats.count();
   size_t maxSamplesPerPeriod = SAMPLE_TIME_MS / SAMPLE_INTERVAL_MS;
   count = min(count, maxSamplesPerPeriod);
   float sd = timedStats.stdDev();
   float sdPercent = (isfinite(avg) && (fabsf(avg) > 0.0f) && isfinite(sd)) ? ((sd / fabsf(avg)) * 100.0f) : NAN;

   if (!serialHeaderPrinted || (serialRowsPrinted % 25 == 0))
   {
      Serial.println();
      SerialX::print("Num Samples", 14);
      SerialX::print(String("Avg(") + sensorUnit + ")", 12);
      SerialX::print(String("Range(") + sensorUnit + ")", 12);
      SerialX::print(String("StdDev(") + sensorUnit + ")", 12);
      SerialX::println("StdDev%", 10);

      SerialX::print("-----------", 14);
      SerialX::print("-------", 12);
      SerialX::print("---------", 12);
      SerialX::print("------", 12);
      SerialX::println("-------", 10);
      serialHeaderPrinted = true;
   }

   SerialX::print(count, 14);
   SerialX::print(avg, 3, 12);
   SerialX::print(rng, 3, 12);
   SerialX::print(sd, 3, 12);
   SerialX::println(isfinite(sdPercent) ? String(sdPercent, 2) + "%" : "n/a", 10);
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
   size_t maxSamplesPerPeriod = SAMPLE_TIME_MS / SAMPLE_INTERVAL_MS;
   count = min(count, maxSamplesPerPeriod);
   float sd = stats.stdDev();
   float sdPercent = (isfinite(avg) && (fabsf(avg) > 0.0f) && isfinite(sd)) ? ((sd / fabsf(avg)) * 100.0f) : NAN;

   printSerialValues(stats);

   feather.setCursor(0, 0);
   feather.setTextSize(3);
   feather.println("Sensor Noise", Color::HEADING);
   feather.moveCursorY(6);

   feather.setTextSize(2);
#if SENSOR_TYPE == SENSOR_TYPE_CAPACITOR
   feather.print("        Res: ", Color::LABEL);
   feather.println(sensor.chargePinLabel(), Color::VALUE2);
#endif
   feather.println("        Avg: ", avg, valueFormat);
   feather.println("      Range: ", rng, rangeFormat);
   feather.println("     StdDev: ", sd, stdDevFormat);
   feather.println("    StdDev%: ", sdPercent, stdDevPercentFormat);
   feather.println("          N: ", count, countFormat);

   float rawRate = sensor.rawRate();
   if (isfinite(rawRate))
   {
      feather.println("     Rate: ", rawRate, rateFormat, Color::GRAY);
   }
   else
   {
      feather.println("     Rate: n/a", Color::GRAY);
   }

#if SENSOR_TYPE == SENSOR_TYPE_CAPACITOR
   if (feather.buttonA.wasPressed() && sensor.supportsChargePinRotation())
   {
      sensor.rotateChargePin();
      stats.reset();
      serialHeaderPrinted = false;
      serialRowsPrinted = 0;
      Serial.println();
      Serial.println(String("Switched charge resistor to ") + sensor.chargePinLabel());
   }
#endif
}
