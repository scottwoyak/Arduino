// -------------------------------------------------------------------------------------------------
// Capacitor_Noise_Display
//
// This sketch continuously samples a capacitor sensor and displays noise metrics over SAMPLE_TIME_MS
// using TimedStats.
//
// Data flow:
// - CapacitorSensor events are consumed from the queue via tryDequeue() so intermediate readings are
//   not collapsed.
// - TimedStats tracks average/range/count/stddev over the last SAMPLE_TIME_MS.
//
// Display flow:
// - GUI refreshes every DISPLAY_INTERVAL_MS.
// - Screen shows Avg, Range, StdDev, sample count, and raw sensor rate.
// -------------------------------------------------------------------------------------------------

#include <Arduino.h>
#include "CapacitorSensor.h"
#include "Feather.h"
#include "TimedStats.h"
#include "Timer.h"

constexpr uint16_t SAMPLE_TIME_MS = 1000;
constexpr uint16_t DISPLAY_INTERVAL_MS = 1.5*SAMPLE_TIME_MS;

constexpr uint8_t CHARGE_PIN = 5;
constexpr uint8_t SENSE_PIN = 6;
constexpr uint16_t DISCHARGE_DELAY_MICROS = 350;

Feather feather;
CapacitorSensor sensor(CHARGE_PIN, SENSE_PIN, DISCHARGE_DELAY_MICROS);
Timer displayTimer(DISPLAY_INTERVAL_MS);

TimedStats stats(SAMPLE_TIME_MS);

Format valueFormat("###.# us", Format::Alignment::RIGHT);
Format rangeFormat("###.# us", Format::Alignment::RIGHT);
Format stdDevFormat("##.## us", Format::Alignment::RIGHT);
Format countFormat("#####", Format::Alignment::RIGHT);

void setup()
{
   feather.begin();
   sensor.begin();
}

void loop()
{
   float chargeTimeMicros = 0;
   while (sensor.tryDequeue(chargeTimeMicros))
   {
      stats.set(chargeTimeMicros);
   }

   if (!displayTimer.ready())
   {
      return;
   }

   float avg = stats.average();
   float rng = stats.range();
   size_t count = stats.count();
   float sd = stats.stdDev();

   feather.setCursor(0, 0);
   feather.setTextSize(3);
   feather.println("Capacitor Noise", Color::HEADING);
   feather.moveCursorY(6);

   feather.println("   Avg: ", avg, valueFormat);
   feather.println(" Range: ", rng, rangeFormat);
   feather.println("StdDev: ", sd, stdDevFormat);
   feather.println("     N: ", count, countFormat);
}
