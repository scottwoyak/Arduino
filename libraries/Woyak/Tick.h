#pragma once

#include <Arduino.h>
#if defined(ARDUINO_ARCH_ESP32)
#include <esp_timer.h>
#endif

class Tick
{
private:
   static constexpr double MICROS_PER_MILLI = 1000.0;

public:
   static uint64_t now()
   {
#if defined(ARDUINO_ARCH_ESP32)
      return static_cast<uint64_t>(esp_timer_get_time());
#else
      return static_cast<uint64_t>(micros());
#endif
   }

   static uint64_t span(uint64_t start, uint64_t end)
   {
      return end - start;
   }

   static double elapsedMicros(uint64_t start, uint64_t end)
   {
      return static_cast<double>(span(start, end));
   }

   static double elapsedMicros(uint64_t start)
   {
      return elapsedMicros(start, now());
   }

   static double elapsedMillis(uint64_t start, uint64_t end)
   {
      return elapsedMicros(start, end) / MICROS_PER_MILLI;
   }

   static double elapsedMillis(uint64_t start)
   {
      return elapsedMillis(start, now());
   }
};
