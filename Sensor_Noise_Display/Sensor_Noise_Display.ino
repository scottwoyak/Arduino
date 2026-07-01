//
// Continuously samples a temperature sensor and displays short-window noise metrics.
//
// Detailed behavior:
// - Sampling runs continuously (each loop iteration) and finite temperature readings are pushed
//   into TimedStats.
// - TimedStats maintains a sliding window of SAMPLE_TIME_MS and computes Avg, Range, StdDev,
//   and StdDev% from values currently retained in that window.
// - Display refreshes at DISPLAY_INTERVAL_MS and renders a compact single-column summary:
//   Sample Time, Samples (count), Avg, Range, and StdDev with percent.
// - Serial metrics are throttled to SERIAL_PRINT_INTERVAL_MS so monitoring remains readable.
//
// Startup diagnostics:
// - Initializes Wire/Feather/TempSensor.
// - If the sensor fails to initialize or first read is invalid, an I2C scan is printed to Serial.
//
// Typical usage:
// - Run the sketch with the target temperature sensor connected.
// - Let readings settle, then watch StdDev/StdDev% to evaluate sensor noise stability.
//

#include <Arduino.h>
#include <Wire.h>
#include "Feather.h"
#include "SerialX.h"
#include "TimedStats.h"
#include "Timer.h"
#include "TempSensor.h"
#include "I2C.h"

constexpr uint16_t SAMPLE_TIME_MS = 1000;
constexpr uint16_t DISPLAY_INTERVAL_MS = 200;
constexpr uint16_t SERIAL_PRINT_INTERVAL_MS = 5000;

Feather feather;
TempSensor sensor;
Timer displayTimer(DISPLAY_INTERVAL_MS);
Timer serialPrintTimer(SERIAL_PRINT_INTERVAL_MS);
TimedStats stats(SAMPLE_TIME_MS);
bool serialHeaderPrinted = false;
uint16_t serialRowsPrinted = 0;

Format valueFormat("####.##");
Format rangeFormat("###.##");
Format stdDevFormat("####.##");
Format stdDevPercentFormat("##.##%", 7);
Format countFormat("######");
Format sampleTimeFormat("#### ms");

void printSerialSummary()
{
   Serial.println();
   Serial.println("Serial Metrics Summary");
   SerialX::println("- Sampling source: active sensor", 0);
   SerialX::println("- Data points are collected continuously", 0);
   SerialX::println(String("- Stats are averaged over: ") + SAMPLE_TIME_MS + " ms (sliding period)", 0);
   Serial.println("- Num Samples: finite samples currently retained in the active stats period");
   Serial.println("- Avg: mean of samples in the active stats period");
   Serial.println("- Range: max-min of samples in the active stats period");
   Serial.println("- StdDev: population standard deviation in active period");
}

void printSerialValues(TimedStats& timedStats)
{
   float avg = timedStats.average();
   float minValue = timedStats.min();
   float maxValue = timedStats.max();
   float rng = (isfinite(minValue) && isfinite(maxValue)) ? (maxValue - minValue) : NAN;
   size_t count = timedStats.count();
   float sd = timedStats.stdDev();
   float sdPercent = (isfinite(avg) && (fabsf(avg) > 0.0f) && isfinite(sd)) ? ((sd / fabsf(avg)) * 100.0f) : NAN;

   if (!serialHeaderPrinted || (serialRowsPrinted % 10 == 0))
   {
      Serial.println();
      SerialX::print("Num Samples", 14);
      SerialX::print("Avg", 12);
      SerialX::print("Range", 12);
      SerialX::print("StdDev", 12);
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
   Wire.begin();
   feather.begin();

   bool sensorReady = sensor.begin();
   printSerialSummary();

   if (!sensorReady)
   {
      Serial.println("Temperature sensor initialization failed.");
      Serial.println("I2C scan:");
      I2C::scan();
   }

   float firstValue = sensor.readTemperatureF();
   if (!isfinite(firstValue))
   {
      Serial.println("Temperature sensor did not return a valid value.");
      Serial.println("I2C scan:");
      I2C::scan();
   }
}

void loop()
{
   float sensorValue = sensor.readTemperatureF();
   if (isfinite(sensorValue))
   {
      stats.set(sensorValue);
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
   float sd = stats.stdDev();
   float sdPercent = (isfinite(avg) && (fabsf(avg) > 0.0f) && isfinite(sd)) ? ((sd / fabsf(avg)) * 100.0f) : NAN;

   if (serialPrintTimer.ready())
   {
      printSerialValues(stats);
   }

   feather.setCursor(0, 0);
   feather.setTextSize(3);
   feather.println("Sensor Noise", Color::HEADING);
   feather.moveCursorY(5);

   feather.setTextSize(2);
   int16_t sectionTop = feather.getCursorY();
   int16_t rowHeight = feather.charH();

   feather.setCursor(0, sectionTop);
   feather.println("Sampling Time: ", SAMPLE_TIME_MS, sampleTimeFormat, Color::VALUE);

   feather.setCursor(0, sectionTop + rowHeight);
   feather.println("      Samples: ", count, countFormat, Color::VALUE2);

   if (count == 0 || !isfinite(avg))
   {
      feather.setCursor(0, sectionTop + (rowHeight * 2));
      feather.println("  No valid data", Color::RED);
      feather.setCursor(0, sectionTop + (rowHeight * 3));
      feather.println("  Check sensor", Color::VALUE2);
      feather.setCursor(0, sectionTop + (rowHeight * 4));
      feather.println("  and I2C wiring", Color::VALUE2);
      return;
   }

   feather.setCursor(0, sectionTop + (rowHeight * 2));
   feather.println("          Avg: ", avg, valueFormat, Color::VALUE2);

   feather.setCursor(0, sectionTop + (rowHeight * 3));
   feather.println("        Range: ", rng, rangeFormat, Color::VALUE2);

   feather.setCursor(0, sectionTop + (rowHeight * 4));
   feather.print("       StdDev: ", Color::LABEL);

   String stdDevValue = String(stdDevFormat.toString(sd).c_str());
   stdDevValue.trim();

   if (isfinite(sdPercent))
   {
      String sdPercentValue = String(stdDevPercentFormat.toString(sdPercent).c_str());
      sdPercentValue.trim();
      feather.println(stdDevValue + ", " + sdPercentValue, Color::VALUE2);
   }
   else
   {
      feather.println(stdDevValue + ", n/a", Color::VALUE2);
   }
}

