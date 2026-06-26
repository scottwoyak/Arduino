// -------------------------------------------------------------------------------------------------
// Capacitor_Noise_Display
//
// This sketch continuously samples a capacitor sensor and displays noise metrics over a one-second
// rolling time window using TimedStats.
//
// Data flow:
// - CapacitorSensor events are consumed from the queue via tryDequeue() so intermediate readings are
//   not collapsed.
// - TimedStats tracks rolling average/range/count over the last WINDOW_MS.
// - TimedStdDev tracks rolling standard deviation over the same window.
//
// Display flow:
// - GUI refreshes every DISPLAY_INTERVAL_MS (100ms).
// - Screen shows Avg, Range, StdDev, sample count in window, and raw sensor rate.
// -------------------------------------------------------------------------------------------------

#include <Arduino.h>
#include "CapacitorSensor.h"
#include "Feather.h"
#include "TimedStats.h"
#include "../libraries/Woyak/TimedStdDev.h"
#include "Timer.h"

constexpr uint16_t DISPLAY_INTERVAL_MS = 100;
constexpr uint16_t WINDOW_MS = 1000;

constexpr uint8_t CHARGE_PIN = 5;
constexpr uint8_t SENSE_PIN = 6;
constexpr uint16_t DISCHARGE_DELAY_MICROS = 350;

Feather feather;
CapacitorSensor sensor(CHARGE_PIN, SENSE_PIN, DISCHARGE_DELAY_MICROS);
Timer displayTimer(DISPLAY_INTERVAL_MS);

TimedStats windowStats(WINDOW_MS);
TimedStdDev windowStdDev(WINDOW_MS);

Format valueFormat("###.# us", Format::Alignment::RIGHT);
Format rangeFormat("###.# us", Format::Alignment::RIGHT);
Format stdDevFormat("##.## us", Format::Alignment::RIGHT);
Format rateFormat("####/s", Format::Alignment::RIGHT);

void setup()
{
   feather.begin();
   sensor.begin();
}

void loop()
{
   uint32_t chargeTimeMicros = 0;
   while (sensor.tryDequeue(chargeTimeMicros))
   {
      float value = (float)chargeTimeMicros;
      windowStats.set(value);
      windowStdDev.set(value);
   }

   if (!displayTimer.ready())
   {
      return;
   }

   float avg = windowStats.average();
   float rng = windowStats.range();
   size_t count = windowStats.count();

   float sd = windowStdDev.get();

   feather.setCursor(0, 0);
   feather.setTextSize(3);
   feather.println("Cap Noise", Color::HEADING);
   feather.moveCursorY(6);

   feather.setTextSize(2);
   feather.print("Avg:   ", Color::LABEL);
   feather.println(avg, valueFormat, Color::VALUE);

   feather.print("Range: ", Color::LABEL);
   feather.println(rng, rangeFormat, Color::VALUE2);

   feather.print("StdDev:", Color::LABEL);
   feather.println(sd, stdDevFormat, Color::VALUE3);

   feather.print("N:     ", Color::LABEL);
   feather.println((unsigned long)count, Color::WHITE);

   feather.print("Rate:  ", Color::SUB_LABEL);
   feather.println(sensor.rate(), rateFormat, Color::SUB_LABEL);
}
