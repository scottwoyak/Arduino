#pragma once

#include <Arduino.h>
#if defined(ARDUINO_ARCH_ESP32)
#include <esp_cpu.h>
#endif

class Tick
{
private:
   static constexpr double MICROS_PER_MILLI = 1000.0;

#if defined(ARDUINO_ARCH_ESP32)
   /// <summary>
   /// Gets CPU cycles per microsecond using compile-time CPU frequency.
   /// Avoids runtime frequency API calls in timing hot paths.
   /// </summary>
   static uint32_t _cyclesPerMicro()
   {
#if defined(F_CPU)
      return static_cast<uint32_t>(F_CPU / 1000000UL);
#else
      return 240U;
#endif
   }
#endif

public:
   /// <summary>
   /// Gets the current raw tick value.
   /// On ESP32 this returns the CPU cycle counter.
   /// </summary>
   static uint32_t now()
   {
#if defined(ARDUINO_ARCH_ESP32)
      return esp_cpu_get_cycle_count();
#else
      return micros();
#endif
   }

   /// <summary>
   /// Gets the number of ticks elapsed between two raw tick values.
   /// </summary>
   static uint32_t span(uint32_t start, uint32_t end)
   {
      return (end >= start) ? (end - start) : (0xFFFFFFFFU - start + end + 1);
   }

   /// <summary>
   /// Converts elapsed ticks to microseconds.
   /// </summary>
   static double elapsedMicros(uint32_t start, uint32_t end)
   {
#if defined(ARDUINO_ARCH_ESP32)
      return static_cast<double>(span(start, end)) / static_cast<double>(_cyclesPerMicro());
#else
      return static_cast<double>(span(start, end));
#endif
   }

   /// <summary>
   /// Converts elapsed ticks from the provided start value to now into microseconds.
   /// </summary>
   static double elapsedMicros(uint32_t start)
   {
      return elapsedMicros(start, now());
   }

   /// <summary>
   /// Converts elapsed ticks to milliseconds.
   /// </summary>
   static double elapsedMillis(uint32_t start, uint32_t end)
   {
      return elapsedMicros(start, end) / MICROS_PER_MILLI;
   }

   /// <summary>
   /// Converts elapsed ticks from the provided start value to now into milliseconds.
   /// </summary>
   static double elapsedMillis(uint32_t start)
   {
      return elapsedMillis(start, now());
   }
};
