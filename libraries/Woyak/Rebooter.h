#pragma once

#include <time.h>
#include <Arduino.h>

#include "Util.h"

///
/// <summary>
/// Reboots the device on a fixed calendar-day schedule, for long-term stability.
/// Currently reboots once per day; kept as its own class so the interval can be
/// changed (e.g. every N days) without affecting callers. Uses an ESP32 FreeRTOS
/// software timer to self-drive the check, so callers don't need to call loop().
/// </summary>
///
class Rebooter
{
private:
   /// <summary>Day-of-year recorded by begin(); -1 until then.</summary>
   int _startDay = -1;

   /// <summary>How often the calendar-day check runs.</summary>
   static constexpr uint32_t CHECK_INTERVAL_MS = 60UL * 1000UL;

   ///
   /// <summary>
   /// Reboots the device (via Util::reset()) once the calendar day has advanced past
   /// the day recorded by begin(). Does nothing if begin() has not been called.
   /// </summary>
   ///
   void _check()
   {
      if (_startDay < 0)
      {
         return;
      }

      time_t now = time(nullptr);
      if (localtime(&now)->tm_yday != _startDay)
      {
         Serial.println("Performing scheduled daily reboot");
         Util::reset();
      }
   }

   static void _timerCallback(TimerHandle_t xTimer)
   {
      Rebooter* rebooter = static_cast<Rebooter*>(pvTimerGetTimerID(xTimer));
      rebooter->_check();
   }

public:
   ///
   /// <summary>
   /// Records the current day (via NTP-synced wall-clock time) as the reboot baseline,
   /// then starts an internal timer that periodically checks for the calendar day
   /// advancing. Call once from setup(), after WiFi/time sync (e.g. after initWifi()
   /// or an InfluxDB begin() that syncs NTP).
   /// </summary>
   ///
   void begin()
   {
      time_t now = time(nullptr);
      _startDay = localtime(&now)->tm_yday;

      TimerHandle_t timerHandle = xTimerCreate(
         "RebooterTimer",                    // only used for debugging
         pdMS_TO_TICKS(CHECK_INTERVAL_MS),    // tick interval
         pdTRUE,                              // auto-reload
         this,                                // user data
         _timerCallback);                     // callback function

      if (timerHandle != nullptr)
      {
         xTimerStart(timerHandle, 0); // Start the timer
      }
   }
};
